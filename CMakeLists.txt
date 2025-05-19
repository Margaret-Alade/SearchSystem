cmake_minimum_required(VERSION 3.27.7)
project(SearchSystem)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "Using C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")
set(PostgreSQL_ADDITIONAL_VERSIONS "14.11")
set(CMAKE_OSX_ARCHITECTURES "arm64")
set(SPIDER_SOURCES
    Spider/main.cpp
)
set(SEARCH_ENGINE_SOURCES
    SearchEngine/main.cpp
)
add_executable(SpiderExecutable ${SPIDER_SOURCES})
add_executable(SearchEngineExecutable ${SEARCH_ENGINE_SOURCES})
find_package(PostgreSQL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
target_link_libraries(SpiderExecutable Boost::system Boost::filesystem PostgreSQL::PostgreSQL PkgConfig::PQXX)
target_link_libraries(SearchEngineExecutable Boost::system Boost::filesystem PostgreSQL::PostgreSQL PkgConfig::PQXX)