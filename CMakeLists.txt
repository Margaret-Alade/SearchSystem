cmake_minimum_required(VERSION 3.27.7)
project(SearchSystem)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PostgreSQL_ADDITIONAL_VERSIONS "14.11")
set(SPIDER_SOURCES
    Spider/main.cpp
)
add_executable(SpiderExecutable ${SPIDER_SOURCES})
find_package(PostgreSQL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
target_link_libraries(SpiderExecutable Boost::system Boost::filesystem PostgreSQL::PostgreSQL PkgConfig::PQXX)