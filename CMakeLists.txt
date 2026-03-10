cmake_minimum_required(VERSION 3.16)
project(simple_db LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(simple_db_lib
    src/db/Database.cpp
    src/storage/Pager.cpp
    src/tree/BPlusTree.cpp
    src/util/DebugPrinter.cpp
    src/util/InvariantChecker.cpp
)

target_include_directories(simple_db_lib PUBLIC src)

add_executable(simple_db main.cpp)
target_link_libraries(simple_db PRIVATE simple_db_lib)

add_executable(simple_db_tests tests/database_tests.cpp)
target_link_libraries(simple_db_tests PRIVATE simple_db_lib)

enable_testing()
add_test(NAME simple_db_tests COMMAND simple_db_tests)
