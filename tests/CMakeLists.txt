# Tests
find_package(Qt6 REQUIRED COMPONENTS Test)

set(COMMON_TEST_DEPS
    ${CMAKE_SOURCE_DIR}/app/hardware/HardwareDetector.cpp
    ${CMAKE_SOURCE_DIR}/app/hardware/BackendDetector.cpp
    ${CMAKE_SOURCE_DIR}/app/skills/SkillsManager.cpp
    ${CMAKE_SOURCE_DIR}/app/mcp/MCPClient.cpp
    ${CMAKE_SOURCE_DIR}/app/mcp/MCPServerManager.cpp
    ${CMAKE_SOURCE_DIR}/app/mcp/ToolCalling.cpp
    ${CMAKE_SOURCE_DIR}/app/inference/LlamaBackend.cpp
    ${CMAKE_SOURCE_DIR}/app/inference/ChatTemplate.cpp
    ${CMAKE_SOURCE_DIR}/app/inference/ContextManager.cpp
    ${CMAKE_SOURCE_DIR}/app/core/Paths.cpp
    ${CMAKE_SOURCE_DIR}/app/core/Logging.cpp
    ${CMAKE_SOURCE_DIR}/app/core/PathSecurity.cpp
)

set(COMMON_TEST_LIBS Qt6::Core Qt6::Test Qt6::Network Qt6::Sql Qt6::Concurrent)

function(add_modelfit_test name src)
    add_executable(${name} ${src} ${COMMON_TEST_DEPS})
    target_link_libraries(${name} PRIVATE ${COMMON_TEST_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE HAVE_LLAMA_CPP=0 MODELFIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_modelfit_test(test_hardware unit/test_hardware.cpp)
add_modelfit_test(test_skills_parser unit/test_skills_parser.cpp)
add_modelfit_test(test_mcp_jsonrpc unit/test_mcp_jsonrpc.cpp)
add_modelfit_test(test_inference inference/test_inference.cpp)
add_modelfit_test(test_mcp_client mcp/test_mcp_client.cpp)
add_modelfit_test(test_tool_calling mcp/test_tool_calling.cpp)
