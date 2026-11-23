# Tests for app/core/AccountManager.
set(ACCOUNT_TEST_DEPS
    ${CMAKE_SOURCE_DIR}/app/core/AccountManager.cpp
)

function(add_modelbench_account_test name src)
    add_executable(${name} ${src} ${ACCOUNT_TEST_DEPS})
    target_link_libraries(${name} PRIVATE ${COMMON_TEST_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE
        HAVE_LLAMA_CPP=0 MODELFIT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_modelbench_account_test(test_account_manager test_account_manager.cpp)