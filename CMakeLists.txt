cmake_minimum_required(VERSION 3.27.7)
project(SearchSystem)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Using C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")

set(PostgreSQL_ADDITIONAL_VERSIONS "14.11")
set(CMAKE_OSX_ARCHITECTURES "arm64")

set(Boost_DIR "/opt/homebrew/Cellar/boost/1.88.0/lib/cmake/Boost-1.88.0")
set(BOOST_ROOT "/opt/homebrew/Cellar/boost/1.88.0")
set(BOOST_INCLUDEDIR "/opt/homebrew/Cellar/boost/1.88.0/include")
set(BOOST_LIBRARYDIR "/opt/homebrew/Cellar/boost/1.88.0/lib")
set(Boost_URL_DIR "/opt/homebrew/Cellar/boost/1.88.0/lib/cmake/boost_url-1.88.0")
set(OPENSSL_ROOT_DIR "/opt/homebrew/Cellar/openssl@3/3.5.0")
set(OPENSSL_INCLUDE_DIR "/opt/homebrew/Cellar/openssl@3/3.5.0/include")

set(SPIDER_SOURCES
    Spider/main.cpp
)
set(SEARCH_ENGINE_SOURCES
    SearchEngine/main.cpp
)
add_executable(SpiderExecutable ${SPIDER_SOURCES})
add_executable(SearchEngineExecutable ${SEARCH_ENGINE_SOURCES})

find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(Boost REQUIRED COMPONENTS system filesystem url)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
target_link_libraries(SpiderExecutable Boost::system Boost::filesystem Boost::url PostgreSQL::PostgreSQL PkgConfig::PQXX ${OPENSSL_LIBRARIES})
target_link_libraries(SearchEngineExecutable Boost::system Boost::filesystem Boost::url PostgreSQL::PostgreSQL PkgConfig::PQXX ${OPENSSL_LIBRARIES})

target_include_directories(
    SpiderExecutable PRIVATE
    ${OPENSSL_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
)
target_include_directories(
    SearchEngineExecutable PRIVATE
    ${OPENSSL_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
)