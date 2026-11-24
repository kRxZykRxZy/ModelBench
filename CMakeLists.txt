cmake_minimum_required(VERSION 3.21)

# Use ccache if available to speed up rebuilds.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

project(ModelBench VERSION 0.1.0 LANGUAGES CXX)

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

option(MODELFIT_ENABLE_LLAMA_CPP "Build with embedded llama.cpp" OFF)

set(MODELFIT_SOURCES
    app/main.cpp
    app/Application.cpp
    app/core/Paths.cpp
    app/core/Logging.cpp
    app/core/PathSecurity.cpp
    app/core/Theme.cpp
    app/core/AccountManager.cpp
    app/core/Updater.cpp
    app/storage/Database.cpp
    app/storage/Repositories.cpp
    app/storage/SecretStorage.cpp
    app/storage/VectorStore.cpp
    app/storage/CodeIndexer.cpp
    app/hardware/HardwareDetector.cpp
    app/hardware/BackendDetector.cpp
    app/models/HuggingFaceClient.cpp
    app/models/ModelRegistry.cpp
    app/models/ModelDownloader.cpp
    app/models/ModelDownloads.cpp
    app/chat/ConversationManager.cpp
    app/chat/ChatOrchestrator.cpp
    app/agent/AgentMemory.cpp
    app/agent/AgentPlanner.cpp
    app/agent/AgentOrchestrator.cpp
    app/tools/ToolApproval.cpp
    app/tools/ToolRegistry.cpp
    app/tools/FileSystemTools.cpp
    app/tools/TerminalTool.cpp
    app/tools/GitTools.cpp
    app/tools/FileEditHistory.cpp
    app/tools/CodeRunner.cpp
    app/tools/FileWatcher.cpp
    app/tools/SystemIntegration.cpp
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
    app/core/AccountManager.h
    app/core/Updater.h
    app/storage/Database.h
    app/storage/Repositories.h
    app/storage/SecretStorage.h
    app/storage/VectorStore.h
    app/storage/CodeIndexer.h
    app/hardware/HardwareDetector.h
    app/hardware/BackendDetector.h
    app/models/HuggingFaceClient.h
    app/models/ModelRegistry.h
    app/models/ModelDownloader.h
    app/chat/ConversationManager.h
    app/chat/ChatOrchestrator.h
    app/agent/AgentMemory.h
    app/agent/AgentPlanner.h
    app/agent/AgentOrchestrator.h
    app/tools/ToolApproval.h
    app/tools/ToolRegistry.h
    app/tools/FileSystemTools.h
    app/tools/TerminalTool.h
    app/tools/GitTools.h
    app/tools/FileEditHistory.h
    app/tools/CodeRunner.h
    app/tools/FileWatcher.h
    app/tools/SystemIntegration.h
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

qt_add_executable(ModelBench
    ${MODELFIT_SOURCES}
    ${MODELFIT_HEADERS}
)

target_include_directories(ModelBench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

target_link_libraries(ModelBench PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::Sql Qt6::Concurrent)

if(WIN32)
    target_link_libraries(ModelBench PRIVATE crypt32)
endif()

# Optional OpenSSL for update signature verification.
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_compile_definitions(ModelBench PRIVATE HAVE_OPENSSL=1)
    target_link_libraries(ModelBench PRIVATE OpenSSL::Crypto)
    target_include_directories(ModelBench PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

if(MODELFIT_ENABLE_LLAMA_CPP)
    target_compile_definitions(ModelBench PRIVATE HAVE_LLAMA_CPP=1)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/CMakeLists.txt")
        set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/llama.cpp)
        target_link_libraries(ModelBench PRIVATE llama)
        target_include_directories(ModelBench PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/ggml/include
        )
    endif()
endif()

set_source_files_properties(app/ui/Navigation.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(ModelBench
    URI ModelBench
    VERSION 1.0
    QML_FILES
        app/ui/Navigation.qml
        app/ui/Main.qml
        app/ui/Sidebar.qml
        app/ui/HeaderBar.qml
        app/ui/HomeView.qml
        app/ui/ChatView.qml
        app/ui/ModelsView.qml
        app/ui/EngineInstallView.qml
        app/ui/McpHubView.qml
        app/ui/SkillsHubView.qml
        app/ui/SettingsView.qml
        app/ui/LoginView.qml
        app/ui/SubscriptionView.qml
        app/ui/AgentTimeline.qml
        app/ui/GitView.qml
        app/ui/CreditsView.qml
        app/ui/OnboardingWizard.qml
        app/ui/components/TerminalView.qml
        app/ui/components/CodeRunnerView.qml
)

install(TARGETS ModelBench RUNTIME DESTINATION bin)

# Avoid QML module directory name collision with executable output (macOS/Linux).
set_target_properties(ModelBench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ---- Packaging (CPack) ----
set(CPACK_PACKAGE_NAME "ModelBench")
set(CPACK_PACKAGE_VENDOR "ModelBench")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local-first LLM workbench")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/kRxZykRxZy/ModelBench")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
endif()
if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "ModelBench")
    set(CPACK_NSIS_PACKAGE_NAME "ModelBench")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME "ModelBench")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

enable_testing()
add_subdirectory(tests)
