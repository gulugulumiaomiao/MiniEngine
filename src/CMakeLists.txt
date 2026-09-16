# Engine implementation is intentionally one archive. The source tree provides
# subsystem boundaries; splitting every subsystem into a static library only
# creates artificial link cycles between assets, render resources and scenes.
file(GLOB_RECURSE MINI_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(REMOVE_ITEM MINI_ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/main.cpp")

# Vendored third-party sources compiled straight into the engine archive
# (spirv-cross: C++; miniz: C, hence the C language in the root project()).
set(MINI_THIRD_PARTY_SOURCES
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross.cpp
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_parser.cpp
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross_parsed_ir.cpp
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cfg.cpp
    ${PROJECT_SOURCE_DIR}/third_party/miniz/miniz.c
)

# The engine is built twice per configuration. Editor-only engine features are guarded
# by MINI_EDITOR, and because the archive is shared by the game, the tools and the
# tests, defining that macro only on the editor executable would leave the guarded
# function bodies uncompiled and make Engine's layout differ between translation
# units. A dedicated variant keeps every translation unit consistent.
function(mini_add_engine_library target)
    add_library(${target} STATIC
        ${MINI_ENGINE_SOURCES}
        ${MINI_THIRD_PARTY_SOURCES}
    )
    source_group(TREE "${PROJECT_SOURCE_DIR}" FILES
        ${MINI_ENGINE_SOURCES}
        ${MINI_THIRD_PARTY_SOURCES}
    )
    target_include_directories(${target} PUBLIC
        ${PROJECT_SOURCE_DIR}/src
    )
    target_include_directories(${target} SYSTEM PUBLIC
        ${PROJECT_SOURCE_DIR}/third_party/glm/include
        ${PROJECT_SOURCE_DIR}/third_party/nlohmann/include
    )
    target_include_directories(${target} SYSTEM PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/spirv-cross
        ${PROJECT_SOURCE_DIR}/third_party/stb
        ${PROJECT_SOURCE_DIR}/third_party/vma/include
        ${PROJECT_SOURCE_DIR}/third_party/miniz
    )
    target_compile_definitions(${target} PUBLIC
        VK_USE_PLATFORM_WIN32_KHR
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        # MINI_DEBUG: validation layers, debug labels, runtime shader compilation and
        # hot reload (Debug configuration).
        # MINI_RELEASE: optimized build that still uses runtime shader compilation and
        # hot reload, but disables validation layers (Release configuration).
        # MINI_PUBLISH: shipping configuration with packaged assets and read-only
        # packaged SPIR-V; no hot reload (Publish configuration).
        $<$<CONFIG:Debug>:MINI_DEBUG=1>
        $<$<CONFIG:Release>:MINI_RELEASE=1>
        $<$<CONFIG:Publish>:MINI_PUBLISH=1>
    )
    target_compile_definitions(${target} PRIVATE
        MINI_GLSLC_EXECUTABLE="${GLSLC_EXECUTABLE}"
    )
    target_link_libraries(${target} PUBLIC Vulkan::Vulkan)
    target_link_libraries(${target} PRIVATE user32)
    mini_enable_warnings(${target})
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
    endif()
endfunction()

# Game runtime, packaging tools and the engine test suite.
mini_add_engine_library(MiniEngine)

# 编辑器变体额外启用编辑器配置、项目注册与模板同步；ProjectConfig 和项目接口两者共用。
# MINI_EDITOR 为 PUBLIC，确保编辑器及测试看到一致的 Engine 类型布局。
mini_add_engine_library(MiniEngineEditor)
target_compile_definitions(MiniEngineEditor PUBLIC MINI_EDITOR=1)
# The engine's built-in content directory stays on the source tree; the editor never
# mounts it as a virtual scheme. It is split into core/ (engine contract, refreshed into
# every project on open) and samples/ (demo content, seeded once at creation), both
# flattened into the project's assets/ so a project ships independently of the engine's
# location. See syncEngineContractIntoProject / seedSampleContentIntoProject.
target_compile_definitions(MiniEngineEditor PRIVATE
    MINI_SOURCE_BUILTIN_DIR="${PROJECT_SOURCE_DIR}/builtin"
)
set_target_properties(MiniEngineEditor PROPERTIES FOLDER Tools)

add_executable(MiniVulkanEngine runtime/main.cpp)
target_link_libraries(MiniVulkanEngine PRIVATE MiniEngine)
mini_enable_warnings(MiniVulkanEngine)
mini_link_mingw_runtime(MiniVulkanEngine)


