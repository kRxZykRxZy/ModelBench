cmake_minimum_required(VERSION 3.21)

# Use ccache if available to speed up rebuilds.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

project(ModelFit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Widgets Quick QuickControls2 Network Sql Concurrent Test)

option(MODELFIT_ENABLE_LLAMA_CPP "Build with embedded llama.cpp" ON)

set(MODELFIT_SOURCES
    app/main.cpp
    app/Application.cpp
    app/core/Paths.cpp
    app/core/Logging.cpp
    app/core/PathSecurity.cpp
    app/core/Theme.cpp
    app/storage/Database.cpp
    app/storage/Repositories.cpp
    app/storage/SecretStorage.cpp
    app/hardware/HardwareDetector.cpp
    app/hardware/BackendDetector.cpp
    app/hardware/PerformanceEstimator.cpp
    app/models/HuggingFaceClient.cpp
    app/models/ModelRegistry.cpp
    app/models/ModelDownloader.cpp
    app/chat/ConversationManager.cpp
    app/chat/ChatOrchestrator.cpp
    app/inference/LlamaBackend.cpp
    app/inference/ChatTemplate.cpp
    app/inference/ContextManager.cpp
    app/inference/EngineInstaller.cpp
    app/mcp/MCPClient.cpp
    app/mcp/MCPServerManager.cpp
    app/mcp/MCPRegistry.cpp
    app/mcp/ToolCalling.cpp
    app/skills/SkillsManager.cpp
    app/skills/SkillsShClient.cpp
    app/skills/SkillInjector.cpp
    app/billing/SubscriptionManager.cpp
    app/billing/WhopClient.cpp
    app/api/LocalApiServer.cpp
)

set(MODELFIT_HEADERS
    app/Application.h
    app/core/Paths.h
    app/core/Logging.h
    app/core/PathSecurity.h
    app/core/Theme.h
    app/storage/Database.h
    app/storage/Repositories.h
    app/storage/SecretStorage.h
    app/hardware/HardwareDetector.h
    app/hardware/BackendDetector.h
    app/hardware/PerformanceEstimator.h
    app/models/HuggingFaceClient.h
    app/models/ModelRegistry.h
    app/models/ModelDownloader.h
    app/chat/ConversationManager.h
    app/chat/ChatOrchestrator.h
    app/inference/LlamaBackend.h
    app/inference/ChatTemplate.h
    app/inference/ContextManager.h
    app/inference/EngineInstaller.h
    app/mcp/MCPClient.h
    app/mcp/MCPServerManager.h
    app/mcp/MCPRegistry.h
    app/mcp/ToolCalling.h
    app/skills/SkillsManager.h
    app/skills/SkillsShClient.h
    app/skills/SkillInjector.h
    app/billing/SubscriptionManager.h
    app/billing/WhopClient.h
    app/api/LocalApiServer.h
)

qt_add_executable(ModelFit
    ${MODELFIT_SOURCES}
    ${MODELFIT_HEADERS}
)

target_include_directories(ModelFit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

target_link_libraries(ModelFit PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::Sql Qt6::Concurrent)

if(WIN32)
    target_link_libraries(ModelFit PRIVATE crypt32)
endif()

if(MODELFIT_ENABLE_LLAMA_CPP)
    target_compile_definitions(ModelFit PRIVATE HAVE_LLAMA_CPP=1)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/CMakeLists.txt")
        set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/llama.cpp)
        target_link_libraries(ModelFit PRIVATE llama)
        target_include_directories(ModelFit PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/ggml/include
        )
    endif()
endif()

qt_add_qml_module(ModelFit
    URI ModelFit
    VERSION 1.0
    QML_FILES
        app/ui/Main.qml
        app/ui/Sidebar.qml
        app/ui/HeaderBar.qml
        app/ui/HomeView.qml
        app/ui/ChatView.qml
        app/ui/ModelsView.qml
        app/ui/EngineInstallView.qml
        app/ui/CalculatorView.qml
        app/ui/McpHubView.qml
        app/ui/SkillsHubView.qml
        app/ui/SettingsView.qml
        app/ui/SubscriptionView.qml
)

install(TARGETS ModelFit RUNTIME DESTINATION bin)

enable_testing()
add_subdirectory(tests)
