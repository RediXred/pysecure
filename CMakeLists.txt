cmake_minimum_required(VERSION 3.15)
project(PySecure)

set(CMAKE_CXX_STANDARD 17)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)

find_package(SQLite3 REQUIRED)

add_executable(pysec src/main.cpp src/db.cpp)
target_link_libraries(pysec PRIVATE Python3::Python SQLite::SQLite3)

pybind11_add_module(analyzer src/bindings.cpp src/db.cpp)
target_link_libraries(analyzer PRIVATE SQLite::SQLite3)
