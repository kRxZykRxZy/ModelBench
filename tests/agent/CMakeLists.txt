# Tests for app/agent/**. Add `add_subdirectory(agent)` from tests/CMakeLists.txt.
set(AGENT_TEST_DEPS
    ${CMAKE_SOURCE_DIR}/app/agent/AgentMemory.cpp
    ${CMAKE_SOURCE_DIR}/app/agent/AgentPlanner.cpp
    ${CMAKE_SOURCE_DIR}/app/agent/AgentOrchestrator.cpp
    ${CMAKE_SOURCE_DIR}/app/storage/VectorStore.cpp
    ${CMAKE_SOURCE_DIR}/app/storage/CodeIndexer.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/ToolApproval.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/FileEditHistory.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/FileSystemTools.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/TerminalTool.cpp
    ${CMAKE_SOURCE_DIR}/app/tools/GitTools.cpp
)

function(add_modelbench_agent_test name src)
    add_executable(${name} ${src} ${AGENT_TEST_DEPS})
    target_link_libraries(${name} PRIVATE ${COMMON_TEST_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/app)
    target_compile_definitions(${name} PRIVATE
        HAVE_LLAMA_CPP=0 MODELFIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_modelbench_agent_test(test_agent test_agent.cpp)
