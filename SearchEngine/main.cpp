#include <iostream>
#include <string>
#include <vector>
#include <pqxx/pqxx>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <regex>

namespace beast = boost::beast;       
namespace http = beast::http;           
using tcp = boost::asio::ip::tcp;       

// Настройки из конфигурационного файла.
std::string db_host, db_port, db_name, db_user, db_password;
int server_port;


// Функция выполнения SQL-запроса для поиска документов.
// Возвращает список пар: ID документа и его URL, соответствующих поисковым словам
std::vector<std::pair<int, std::string>> search_documents(const std::vector<std::string>& search_words) {
    std::vector<std::pair<int, std::string>> results;

    if (search_words.empty()) return results;

    try {
        pqxx::connection C("host=" + db_host + " port=" + db_port + " dbname=" + db_name + " user=" + db_user + " password=" + db_password);
        pqxx::work W(C);

        // Создаем параметры для IN-запрос 
        // В цикле формируется строка вида $1, $2, $3, ..., где $i — параметры подготовленного запроса.
        // Это делается для безопасной передачи переменных в SQL-запрос через параметры.
        std::string placeholders;
        for (size_t i=0; i<search_words.size(); ++i) {
            placeholders += "$" + std::to_string(i+1);
            if (i != search_words.size() -1)
                placeholders += ", ";
        }

        // Формируем SQL-запрос с параметрами
        std::string sql = 
        "SELECT DISTINCT d.id, d.url "
        "FROM documents d "
        "JOIN document_word_frequency dwf ON d.id = dwf.document_id "
        "JOIN words w ON dwf.word_id = w.id "
        "WHERE w.word IN (" + placeholders + ");";

        // Подготовка запроса
        C.prepare("search_words", sql);

        // Выполнение подготовленного запроса с параметрами
        // Параметры передаем как аргументы функции exec_prepared
        // Важно: параметры должны быть в том же порядке, что и плейсхолдеры
        std::vector<std::string> params = search_words; // просто копируем

        auto r = W.exec("search_words");

        for (const auto& row : r) {
            int doc_id = row[0].as<int>();
            std::string url = row[1].as<std::string>();
            results.emplace_back(doc_id, url);
        }

        W.commit();
    } catch (const pqxx::sql_error& e) {
        std::cerr << "SQL error: " << e.what() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }

    return results;
}

// Простая функция для генерации HTML страницы поиска
std::string generate_search_form() {
    return "<html>\n"
           "<head><title>Поиск</title></head>\n"
           "<body>\n"
           "<h1>Поиск по базе</h1>\n"
           "<form method=\"POST\" action=\"/\">\n" //данные будут отправлены методом POST
           "<input type=\"text\" name=\"query\" maxlength=\"100\"/>\n" //query - имя поля, по которому сервер сможет получить введенное значение.
           "<button type=\"submit\">Найти</button>\n" //при нажатии кнопка отправит форму
           "</form>\n"
           "</body>\n"
           "</html>";
}

// Генерация страницы с результатами поиска
std::string generate_results_page(const std::vector<std::pair<int,std::string>>& docs,
    bool no_results=false) {
std::string html = "<html><head><title>Результаты поиска</title></head><body>";
if (no_results) {
html += "<h2>Результаты не найдены</h2>";
} else {
html += "<h2>Результаты:</h2><ul>";
for (const auto& doc : docs) {
html += "<li><a href=\"" + doc.second + "\">" + doc.second + "</a></li>";
}
html += "</ul>";
}
html += "</body></html>";
return html;
}

// Обработка HTTP-запросов
template<class Body, class Allocator> //позволяет работать с разными типами тел сообщений и аллокаторами памяти
http::response<http::string_body> handle_request(const http::request<Body>& req) {
    try {
        if (req.method() == http::verb::get) {
            // Обработка GET-запроса. Возвращаем страницу формы поиска
            auto res_req = http::response<http::string_body>{http::status::ok, 0};
            res_req.body() = generate_search_form();
            res_req.prepare_payload();
            res_req.set(http::field::content_type, "text/html");
            return res_req;
        } else if (req.method() == http::verb::post) {
            // Обработка POST-запроса: извлечение параметра query из тела формы
            std::string body_str(req.body()); // Конвертируем тело запроса в стандартную строку
            std::regex re("query=([^&]+)"); //Регулярное выражение для поиска параметра query
            std::smatch match; //объект для хранения результатов поиска
            std::vector<std::string> search_words; //контейнера для слов поиска пользователя

            if (std::regex_search(body_str, match, re)) {
                std::string query_raw = match[1];
                // Нужно ли добавить url-декодинг?
                // Разделияем на слова по пробелам:
                std::istringstream iss(query_raw); 
                std::string word;
                while (iss >> word && search_words.size() < 4) { 
                    search_words.push_back(word); //Собираем до 4 слов пользователя в контейнер
                }
            }

            if (search_words.empty()) {
                auto res_req = http::response<http::string_body>{http::status::ok, 0};
                res_req.body() = generate_search_form();
                res_req.prepare_payload();
                res_req.set(http::field::content_type, "text/html");
                return res_req;
            }

            auto res_docs = search_documents(search_words);

            if (res_docs.empty()) {
                auto res_req = http::response<http::string_body>{http::status::ok, 0};
                res_req.body() = generate_results_page(res_docs, true);
                res_req.prepare_payload();
                res_req.set(http::field::content_type, "text/html");
                return res_req;
            } else {
                auto res_req = http::response<http::string_body>{http::status::ok, 0};
                res_req.body() = generate_results_page(res_docs);
                res_req.prepare_payload();
                res_req.set(http::field::content_type, "text/html");
                return res_req;
            }
        } else {
            auto res = http::response<http::string_body>{http::status::method_not_allowed, 0}; // 0 — длина тела
            res.body() = "Method Not Allowed";
            res.prepare_payload();
            res.set(http::field::content_type, "text/plain");
            return res;
        }
    } catch(const std::exception & e) {
        http::response<http::string_body> res{http::status::internal_server_error, 0};
        res.body() = "<h1>Внутренняя ошибка</h1>";
        res.prepare_payload();
        res.set(http::field::content_type, "text/html");
        return res;
    }
}


int main() {

   // Загружаем настройки из config.ini
   boost::property_tree::ptree pt;
   boost::property_tree::ini_parser::read_ini("config.ini", pt);

   db_host = pt.get<std::string>("database.host");
   db_port = pt.get<std::string>("database.port");
   db_name = pt.get<std::string>("database.dbname");
   db_user = pt.get<std::string>("database.user");
   db_password = pt.get<std::string>("database.password");
   server_port = pt.get<int>("server.server_port");

   try{
    boost::asio::io_context ioc{1};
    tcp::acceptor acceptor{ioc,tcp::endpoint(tcp::v4(), server_port)};
    
    for (;;) {
        tcp::socket socket{ioc};
        acceptor.accept(socket);

        beast::flat_buffer buffer;

        try{
            http::request<http::string_body> req;
            http::read(socket, buffer, req);

            auto res = handle_request<http::string_body, std::allocator<void>>(req); // Передаем конфигурацию

            http::write(socket, res);
        } catch (...) {}
        socket.close();
    }
    
    // Нет необходимости закрывать соединение — оно закрывается при выходе из main()
    
  } catch(const std::exception & e){
    std::cerr << "Ошибка: "<< e.what() << "\n";
    return 1;
  }

  return 0;

}
