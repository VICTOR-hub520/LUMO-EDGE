cmake_minimum_required(VERSION 3.25)
project(LumoEdge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

add_executable(lumo-edge
    src/core.cpp
    src/database.cpp
    src/main.cpp
)
target_include_directories(lumo-edge PRIVATE include)
target_link_libraries(lumo-edge PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
    Threads::Threads
)
target_compile_definitions(lumo-edge PRIVATE "LUMO_EDGE_VERSION=\"${PROJECT_VERSION}\"")
if(WIN32)
    target_link_libraries(lumo-edge PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(lumo-edge PRIVATE /W4 /permissive-)
else()
    target_compile_options(lumo-edge PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(lumo-edge-tests
        src/core.cpp
        tests/core_tests.cpp
    )
    target_include_directories(lumo-edge-tests PRIVATE include)
    if(MSVC)
        target_compile_options(lumo-edge-tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(lumo-edge-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME lumo-edge-core COMMAND lumo-edge-tests)

    add_executable(lumo-edge-database-tests
        src/database.cpp
        tests/database_tests.cpp
    )
    target_include_directories(lumo-edge-database-tests PRIVATE include)
    target_link_libraries(lumo-edge-database-tests PRIVATE SQLite3::SQLite3)
    if(MSVC)
        target_compile_options(lumo-edge-database-tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(lumo-edge-database-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME lumo-edge-database COMMAND lumo-edge-database-tests)
endif()
