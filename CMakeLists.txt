cmake_minimum_required(VERSION 3.24)
project(MiniVulkanEngine VERSION 0.1.0 LANGUAGES CXX)

# Publish is the shipping configuration: Release-level code generation plus the
# engine's publish behavior (MINI_PUBLISH). CMake only predefines flags for its
# known configurations, so alias the custom one to the Release profile.
set(CMAKE_CXX_FLAGS_PUBLISH "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING
    "Compiler flags used for Publish builds." FORCE)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Vulkan REQUIRED)
find_program(GLSLC_EXECUTABLE
    NAMES glslc glslc.exe
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

set(MINI_GENERATED_SHADER_DIR "${PROJECT_SOURCE_DIR}/generated-shaders")

if(MINGW)
    # Keep executables independent of the compiler installation's runtime DLLs.
    # The explicit archive group below supplies libstdc++; prevent clang++ from
    # appending a second copy after that group.
    add_link_options(-nostdlib++ -static-libgcc)
endif()

function(mini_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(mini_link_mingw_runtime target)
    if(MINGW)
        target_link_libraries(${target} PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
endfunction()

add_subdirectory(src)
add_subdirectory(tools)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT MiniVulkanEngine)
