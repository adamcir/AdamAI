cmake_minimum_required(VERSION 3.20)
project(AdamAI VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

add_library(adam_core STATIC
    src/core/chat.c
    src/backends/ollama.c
)
target_include_directories(adam_core PUBLIC include)
target_compile_definitions(adam_core PUBLIC _POSIX_C_SOURCE=200809L)
target_link_libraries(adam_core PUBLIC PkgConfig::CURL PkgConfig::JSONC Threads::Threads)

add_executable(AdamAI main.c src/ui/tui.c src/ui/editor_layout.c)
target_include_directories(AdamAI PRIVATE include ${CURSES_INCLUDE_DIRS})
target_compile_definitions(AdamAI PRIVATE _XOPEN_SOURCE=700 _XOPEN_SOURCE_EXTENDED=1)
target_link_libraries(AdamAI PRIVATE adam_core ${CURSES_LIBRARIES})

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(adam_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(AdamAI PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(adam_core_test tests/test_core.c)
    target_link_libraries(adam_core_test PRIVATE adam_core)
    add_test(NAME core COMMAND adam_core_test)

    add_executable(adam_editor_test tests/test_editor_layout.c src/ui/editor_layout.c)
    target_include_directories(adam_editor_test PRIVATE include)
    target_compile_definitions(adam_editor_test PRIVATE _XOPEN_SOURCE=700)
    add_test(NAME editor_wrapping COMMAND adam_editor_test)
endif()
