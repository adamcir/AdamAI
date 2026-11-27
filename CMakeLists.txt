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
    src/core/chat_store.c
    src/tools/web_fetch.c
    src/backends/web_links.c
    src/backends/model_discovery.c
    src/tools/fs_tool.c
    src/agent/tool_registry.c
    src/backends/ollama.c
)
target_include_directories(adam_core PUBLIC include)
target_compile_definitions(adam_core PUBLIC _POSIX_C_SOURCE=200809L)
target_link_libraries(adam_core PUBLIC PkgConfig::CURL PkgConfig::JSONC Threads::Threads)

add_executable(AdamAI main.c src/ui/tui.c src/ui/editor_layout.c src/ui/text_wrap.c src/ui/text_selection.c src/ui/clipboard.c)
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

    add_executable(adam_clipboard_test tests/test_clipboard.c src/ui/clipboard.c)
    target_include_directories(adam_clipboard_test PRIVATE include)
    add_test(NAME system_clipboard COMMAND adam_clipboard_test)

    add_executable(adam_text_selection_test tests/test_text_selection.c src/ui/text_selection.c)
    target_include_directories(adam_text_selection_test PRIVATE include)
    target_compile_definitions(adam_text_selection_test PRIVATE _XOPEN_SOURCE=700)
    add_test(NAME text_selection COMMAND adam_text_selection_test)

    add_executable(adam_text_wrap_test tests/test_text_wrap.c src/ui/text_wrap.c)
    target_include_directories(adam_text_wrap_test PRIVATE include)
    add_test(NAME text_wrap COMMAND adam_text_wrap_test)

    add_executable(adam_chat_store_test tests/test_chat_store.c)
    target_link_libraries(adam_chat_store_test PRIVATE adam_core)
    add_test(NAME chat_store COMMAND adam_chat_store_test)

    add_executable(adam_agent_tools_test tests/test_agent_tools.c)
    target_link_libraries(adam_agent_tools_test PRIVATE adam_core)
    add_test(NAME agent_tools COMMAND adam_agent_tools_test)

    add_executable(adam_fs_tool_test tests/test_fs_tool.c)
    target_link_libraries(adam_fs_tool_test PRIVATE adam_core)
    add_test(NAME fs_tool COMMAND adam_fs_tool_test)

    add_executable(adam_model_discovery_test tests/test_model_discovery.c)
    target_link_libraries(adam_model_discovery_test PRIVATE adam_core)
    add_test(NAME model_discovery COMMAND adam_model_discovery_test)

    add_executable(adam_web_links_test tests/test_web_links.c)
    target_link_libraries(adam_web_links_test PRIVATE adam_core)
    add_test(NAME web_links COMMAND adam_web_links_test)

    add_executable(adam_web_fetch_test tests/test_web_fetch.c)
    target_link_libraries(adam_web_fetch_test PRIVATE adam_core)
    add_test(NAME web_fetch COMMAND adam_web_fetch_test)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_executable(adam_model_tags_http_test tests/test_model_tags_http.c)
        target_link_libraries(adam_model_tags_http_test PRIVATE adam_core)
        add_test(NAME ollama_tags_http
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_ollama_tags.py
                         $<TARGET_FILE:adam_model_tags_http_test>)

        add_executable(adam_web_loop_test tests/test_ollama_web_loop.c)
        target_link_libraries(adam_web_loop_test PRIVATE adam_core)
        add_test(NAME ollama_web_tool_loop
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_ollama.py
                         $<TARGET_FILE:adam_web_loop_test>)

        add_executable(adam_fs_loop_test tests/test_ollama_fs_loop.c)
        target_link_libraries(adam_fs_loop_test PRIVATE adam_core)
        add_test(NAME ollama_fs_tool_loop
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_fs_ollama.py
                         $<TARGET_FILE:adam_fs_loop_test>)

        add_executable(adam_permissions_test tests/test_workspace_web_approvals.c)
        target_link_libraries(adam_permissions_test PRIVATE adam_core)
        add_test(NAME workspace_and_web_approvals
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_workspace_web.py
                         $<TARGET_FILE:adam_permissions_test>)
        # Existing approval test explicitly enables the optional strict mode.
        set_tests_properties(workspace_and_web_approvals
                             PROPERTIES ENVIRONMENT "ADAM_WEB_CONFIRM=1")
    endif()
endif()
