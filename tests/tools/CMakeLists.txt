# Tests for app/tools/**. These fragments are picked up once the top-level
# tests/CMakeLists.txt adds `add_subdirectory(tools)`.
set(TOOL_TEST_DEPS
    ${CMAKE_SOURCE_DIR}/app/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/ToolApproval.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/FileEditHistory.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/FileSystemTools.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/TerminalTool.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/GitTools.cpp
)

function(add_modelbench_tool_test name src)
    add_executable(${name} ${src} ${TOOL_TEST_DEPS})
    target_link_libraries(${name} PRIVATE ${COMMON_TEST_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/app)
    target_compile_definitions(${name} PRIVATE
        HAVE_LLAMA_CPP=0 MODELFIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_modelbench_tool_test(test_tool_registry test_tool_registry.cpp)
add_modelbench_tool_test(test_builtin_tools test_builtin_tools.cpp)
