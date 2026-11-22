# Inference tests. These need a real GGUF file; set MODELFIT_TEST_MODEL to run
# the model-backed tests. Without it (or without llama.cpp) they skip.

add_executable(modelfit_inference_tests
    test_inference.cpp
    ${CMAKE_SOURCE_DIR}/app/inference/LlamaBackend.cpp
    ${CMAKE_SOURCE_DIR}/app/inference/ChatTemplate.cpp
    ${CMAKE_SOURCE_DIR}/app/inference/ContextManager.cpp
)

target_include_directories(modelfit_inference_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/app
)

target_link_libraries(modelfit_inference_tests PRIVATE
    Qt6::Core
    Qt6::Test
)

if(MODELFIT_ENABLE_LLAMA_CPP AND TARGET llama)
    target_compile_definitions(modelfit_inference_tests PRIVATE HAVE_LLAMA_CPP=1)
    target_link_libraries(modelfit_inference_tests PRIVATE llama)
    target_include_directories(modelfit_inference_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include
        ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/ggml/include
    )
endif()

add_test(NAME modelfit_inference_tests COMMAND modelfit_inference_tests)
set_tests_properties(modelfit_inference_tests PROPERTIES LABELS "inference")
