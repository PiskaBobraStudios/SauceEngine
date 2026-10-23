cmake_minimum_required(VERSION 3.20)
project(SauceEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "SauceEngine requires Windows.")
endif()

if(NOT MINGW)
    message(FATAL_ERROR "SauceEngine requires MinGW-w64 because the bundled GLFW library uses the MinGW format.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SauceEngine requires a 64-bit MinGW-w64 toolchain.")
endif()

enable_language(RC)
set(SAUCE_ENGINE_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SauceEngine.rc"
    @ONLY
)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cpp"
)

add_executable(SauceEngine ${ENGINE_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/SauceEngine.rc")

set_target_properties(SauceEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(SauceEngine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/glfw/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/entities"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/gameplay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/network"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/physics"
)

target_compile_definitions(SauceEngine PRIVATE JPH_PROFILE_ENABLED NDEBUG)

target_link_libraries(SauceEngine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Jolt/lib/libJolt.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/glfw/lib/libglfw3.a"
    opengl32
    gdi32
    user32
    shell32
    winmm
    ws2_32
    advapi32
    ole32
    uuid
    comdlg32
    imm32
)

target_link_options(SauceEngine PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
)
