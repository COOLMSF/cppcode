cmake_minimum_required(VERSION 3.25)
project(cc-haha-cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# ── Dependencies via FetchContent ──────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
  GIT_SHALLOW    TRUE
)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json CLI11 ftxui)

# ── System dependency: libcurl ─────────────────────────────────────────────────
find_package(CURL REQUIRED)

# ── Source files ───────────────────────────────────────────────────────────────
set(SOURCES
  src/main.cpp
  src/api/Types.cpp
  src/api/ClaudeClient.cpp
  src/agent/QueryEngine.cpp
  src/tools/Tool.cpp
  src/tools/BashTool.cpp
  src/tools/FileReadTool.cpp
  src/tools/FileWriteTool.cpp
  src/tools/FileEditTool.cpp
  src/tools/GlobTool.cpp
  src/tools/GrepTool.cpp
  src/utils/Config.cpp
  src/utils/Logger.cpp
  src/utils/StringUtils.cpp
  src/tui/App.cpp
)

add_executable(claude-haha ${SOURCES})

target_include_directories(claude-haha PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(claude-haha PRIVATE
  nlohmann_json::nlohmann_json
  CLI11::CLI11
  ftxui::screen
  ftxui::dom
  ftxui::component
  CURL::libcurl
)

# macOS: link against libc++ and frameworks
if(APPLE)
  target_link_libraries(claude-haha PRIVATE
    "-framework CoreFoundation"
  )
endif()

# ── Install ────────────────────────────────────────────────────────────────────
install(TARGETS claude-haha DESTINATION bin)
