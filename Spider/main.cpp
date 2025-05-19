#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <pqxx/pqxx>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <regex>
#include <locale>
#include <algorithm>

//Создаем пространства имен для упрощения работы
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Глобальные переменные
std::queue<std::pair<std::string, int>> url_queue; // Очередь URL с уровнем глубины
std::mutex queue_mutex; // Мьютекс для синхронизации доступа к очереди
std::condition_variable cv; // Условная переменная для уведомления потоков

// Инициализируем SSL контекст в начале программы:
net::io_context ioc;
boost::asio::ssl::context ctx(boost::asio::ssl::context::sslv23_client);

// Настройки из конфигурационного файла
std::string db_host, db_port, db_name, db_user, db_password;
std::string start_url;
int depth;

// Функция для создания таблиц в базе данных
void create_tables() {
    try {
        pqxx::connection C("host=" + db_host + " port=" + db_port + " dbname=" + db_name + " user=" + db_user + " password=" + db_password);
        pqxx::work W(C);

        // Создание таблицы документов
        W.exec0("CREATE TABLE IF NOT EXISTS documents ("
                 "id SERIAL PRIMARY KEY,"
                 "url TEXT NOT NULL UNIQUE"
                 ");");

        // Создание таблицы слов
        W.exec0("CREATE TABLE IF NOT EXISTS words ("
                 "id SERIAL PRIMARY KEY,"
                 "word TEXT NOT NULL UNIQUE"
                 ");");

        // Создание промежуточной таблицы для связи многие-ко-многим с частотой слов
        W.exec0("CREATE TABLE IF NOT EXISTS document_word_frequency ("
                 "document_id INT REFERENCES documents(id),"
                 "word_id INT REFERENCES words(id),"
                 "frequency INT NOT NULL,"
                 "PRIMARY KEY (document_id, word_id)"
                 ");");

        W.commit();
    } catch (const pqxx::sql_error &e) {
        std::cerr << "Ошибка базы данных: " << e.what() << "\n";
    }
}

// Функция для загрузки страницы
std::string load_page(const std::string& url, int redirect_count = 0) {
    const int max_redirects = 5; // Максимальное число редиректов
    if (redirect_count > max_redirects) {
        std::cerr << "Превышено число редиректов для URL: " << url << std::endl;
        return "";
    }
    std::string result_data; // сюда будем накапливать скачанный контент
    try {
        // Разбор URL
        auto scheme_end = url.find("://");
        std::string scheme = "http";
        std::string host;
        std::string target;

        if (scheme_end != std::string::npos) {
            scheme = url.substr(0, scheme_end);
            auto rest = url.substr(scheme_end + 3);
            auto path_pos = rest.find('/');
            if (path_pos != std::string::npos) {
                host = rest.substr(0, path_pos);
                target = rest.substr(path_pos);
            } else {
                host = rest;
                target = "/";
            }
        } else {
            host = url;
            target = "/";
        }

        if (scheme == "https") {
            // Создаем SSL поток
            boost::asio::ssl::stream<tcp::socket> stream(ioc, ctx);

            // Разрешение
            boost::asio::ip::tcp::resolver resolver(ioc);
            auto results = resolver.resolve(host, "443");

            // Подключение
            net::connect(stream.next_layer(), results.begin(), results.end());

           // Задаем имя хоста SNI для успешного установления связи
            if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            {
                boost::system::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
                throw boost::system::system_error{ec};
            }
             // Выполнение рукопожатия SSL
            stream.handshake(boost::asio::ssl::stream_base::client);

            // Формируем HTTP-запрос
            http::request<http::string_body> req{http::verb::get,target,11};
            req.set(http::field::host,host);
            req.set(http::field::user_agent,"Boost.Beast");

            try {
                // Отправляем запрос
                http::write(stream, req);

                // Получаем ответ
                beast::flat_buffer buffer;
                http::response<http::string_body> res;
                http::read(stream, buffer, res);

                if (res.result_int() >= 300 && res.result_int() < 400) { //статус код в диапозоне 300-399 указывает на редирект
                    // Обработка редиректа
                    auto location_iter = res.find(http::field::location); //ищем поле location с url
                    if (location_iter != res.end()) {
                        std::string new_url(location_iter->value().data(), location_iter->value().size()); //делаем строку из location
    
                        // Если редирект относительный, нужно его корректировать
                        if (new_url.find("://") == std::string::npos) {
                            // Относительный путь — добавляем схему и хост
                            new_url = scheme + "://" + host + new_url;
                        }
                        // Закрываем соединение перед рекурсивным вызовом
                        beast::error_code ec;
                        stream.shutdown(ec);
                        if (ec && ec != beast::error_code(boost::asio::error::operation_aborted)) {
                            throw beast::error_code(ec);
                        }
                        return load_page(new_url, redirect_count + 1);
                    }
                }

                result_data = res.body();

                // Закрываем соединение
                beast::error_code ec;
                stream.shutdown(ec);
                if (ec && ec != beast::error_code(boost::asio::error::operation_aborted)) {
                    throw beast::error_code(ec);
                }
            } catch (const beast::error_code& ec) {
                // Обработка ошибок чтения/завершения соединения
                if (ec != boost::asio::error::operation_aborted && ec != beast::errc::connection_reset) {
                    throw; // перекидываем остальные ошибки
                }
                // В случае разрыва соединения — возвращаем то, что есть
                return result_data;
            }

        } else {
            // HTTP без TLS
            boost::asio::ip::tcp::resolver resolver(ioc);
            
            auto results=resolver.resolve(host,"80");
            boost::beast::tcp_stream stream(ioc);
            net::connect(stream.socket(),results.begin(),results.end());
             
            http::request<http::string_body>req{http::verb::get,target,11};
            req.set(http::field::host,host);
            req.set(http::field::user_agent,"Boost.Beast");
            try {
                
                http::write(stream,req);
             
                beast::flat_buffer buffer;
                http::response<http::string_body>res;
                http::read(stream,buffer,res);

                if (res.result_int() >= 300 && res.result_int() < 400) { 
                    auto location_iter = res.find(http::field::location); 
                    if (location_iter != res.end()) {
                        std::string new_url(location_iter->value().data(), location_iter->value().size()); 
                        if (new_url.find("://") == std::string::npos) {
                            new_url = "http://" + host + new_url;
                        }
                        beast::error_code ec;
                        stream.socket().shutdown(tcp::socket::shutdown_both ,ec);
                        if (ec && ec != beast::error_code(boost::asio::error::operation_aborted)) {
                            throw beast::error_code(ec);
                        }
                        return load_page(new_url, redirect_count+1);
                    }
                }

                result_data = res.body();
             
                beast::error_code ec;
                stream.socket().shutdown(tcp::socket::shutdown_both,ec);
                if (ec && ec != beast::error_code(boost::asio::error::operation_aborted)) {
                    throw beast::error_code(ec);
                }
            } catch (const beast::error_code& ec) {
                if (ec != boost::asio::error::operation_aborted && ec != beast::errc::connection_reset) {
                    throw; // остальные ошибки — пробрасываем дальше
                }
                return result_data; // возвращаем что есть при ошибке соединения
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
    }
    return result_data;
}

// Функция для извлечения ссылок из HTML-контента
std::vector<std::string> extract_links(const std::string& html) {
    std::vector<std::string> links;
    std::regex link_regex("<a\\s+(?:[^>]*?\\s+)?href=\"([^\"]*)\"");
    auto links_begin = std::sregex_iterator(html.begin(), html.end(), link_regex);
    auto links_end = std::sregex_iterator();

    for (std::sregex_iterator i = links_begin; i != links_end; ++i) {
        links.push_back((*i)[1].str());
    }
    
    return links;
}

// Индексатор: очищает текст, сохраняет частотность слов в базу данных   
void index_page(const std::string& url, const std::string& html_content) {
    // Очищаем HTML-теги
    std::string text = std::regex_replace(html_content, std::regex("<[^>]+>"), " ");
    
    // Удаляем знаки препинания и лишние пробелы
    text = std::regex_replace(text, std::regex("([^\\w\\s])"), " ");
    text = std::regex_replace(text, std::regex("(\\s+)"), " ");
    // Приводим текст к нижнему регистру
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);

    // Разделяем текст на слова и считаем частоту
    std::istringstream iss(text);
    std::map<std::string, int> word_count;
    std::string word;

    while (iss >> word) {
        if (word.length() >= 3 && word.length() <= 32) { // Фильтруем слова по длине
            word_count[word]++;
        }
    }

    // Сохраняем данные в базу данных
    try {
        pqxx::connection C("host=" + db_host + " port=" + db_port + " dbname=" + db_name + " user=" + db_user + " password=" + db_password);
        pqxx::work W(C);

        // Вставляем URL в таблицу документов и получаем его ID
        W.exec0("INSERT INTO documents (url) VALUES (" + W.quote(url) + ") ON CONFLICT DO NOTHING");
        W.commit();

        int document_id = 0;
        {
            pqxx::work W2(C);
            document_id = W2.query_value<int>("SELECT id FROM documents WHERE url = " + W2.quote(url));
            W2.commit();
        }
        
        for (const auto& pair : word_count) {
            pqxx::work W3(C);
            W3.exec0("INSERT INTO words (word) VALUES (" + W3.quote(pair.first) + ") ON CONFLICT DO NOTHING");
            W3.commit();

            int word_id = 0;
            {
                pqxx::work W4(C);
                word_id = W4.query_value<int>("SELECT id FROM words WHERE word = " + W4.quote(pair.first));
                W4.commit();
            }

            pqxx::work W5(C);
            W5.exec0("INSERT INTO document_word_frequency (document_id, word_id, frequency) VALUES ("
                     + pqxx::to_string(document_id) + ", "
                     + pqxx::to_string(word_id) + ", "
                     + pqxx::to_string(pair.second) +
                     ") ON CONFLICT DO NOTHING");
            W5.commit();
        }
        
    } catch (const pqxx::sql_error &e) {
       std::cerr << "Ошибка базы данных: " << e.what() << "\n";
   }
}    

void worker() {
    while (true) {
        std::unique_lock lock(queue_mutex);
        cv.wait(lock, [] { return !url_queue.empty(); });
 
        auto [url, current_depth] = url_queue.front();
        url_queue.pop();
        lock.unlock();

        if (current_depth > depth) {
            continue; // Пропускаем URL, если текущая глубина больше заданной
        }
 
        // Загружаем страницу и индексируем её содержимое
        auto html_content = load_page(url);
        index_page(url, html_content);
 
        // Извлекаем ссылки из загруженной страницы и добавляем их в очередь
        auto links = extract_links(html_content);
        lock.lock();
        for (const auto& link : links) {
            url_queue.push({link, current_depth + 1}); // Добавляем новые ссылки в очередь с увеличенной глубиной
        }
    }
 }

 
 // Основная функция
int main() {

    // Загружаем настройки из config.ini
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini("config.ini", pt);

    db_host = pt.get<std::string>("database.host");
    db_port = pt.get<std::string>("database.port");
    db_name = pt.get<std::string>("database.dbname");
    db_user = pt.get<std::string>("database.user");
    db_password = pt.get<std::string>("database.password");

    start_url = pt.get<std::string>("start.start_url");
    depth = pt.get<int>("start.depth");

    create_tables(); // Вызов функции создания таблицы

    const int num_threads = 4; // Количество потоков

    // Запускаем пул потоков
    for (int i = 0; i < num_threads; ++i) {
        std::thread(worker).detach();
    }
 
    // Начальная ссылка для обхода с начальной глубиной 0
    url_queue.push({start_url, 0});
 
    while (true) { 
        cv.notify_one(); 
        std::this_thread::sleep_for(std::chrono::seconds(1)); 
    }
 
    return 0;

}