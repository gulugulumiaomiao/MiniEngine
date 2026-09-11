# Engine implementation is intentionally one archive. The source tree provides
# subsystem boundaries; splitting every subsystem into a static library only
# creates artificial link cycles between assets, render resources and scenes.
file(GLOB_RECURSE MINI_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(REMOVE_ITEM MINI_ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/main.cpp")

set(MINI_SPIRV_CROSS_SOURCES
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross.cpp
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_parser.cpp
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cross_parsed_ir.cpp
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross/spirv_cfg.cpp
)

add_library(MiniEngine STATIC
    ${MINI_ENGINE_SOURCES}
    ${MINI_SPIRV_CROSS_SOURCES}
)
source_group(TREE "${PROJECT_SOURCE_DIR}" FILES
    ${MINI_ENGINE_SOURCES}
    ${MINI_SPIRV_CROSS_SOURCES}
)
target_include_directories(MiniEngine PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)
target_include_directories(MiniEngine SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party/glm/include
    ${PROJECT_SOURCE_DIR}/third_party/nlohmann/include
)
target_include_directories(MiniEngine SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/spirv-cross
    ${PROJECT_SOURCE_DIR}/third_party/stb
    ${PROJECT_SOURCE_DIR}/third_party/vma/include
)
target_compile_definitions(MiniEngine PUBLIC
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
target_compile_definitions(MiniEngine PRIVATE
    MINI_GLSLC_EXECUTABLE="${GLSLC_EXECUTABLE}"
)
target_link_libraries(MiniEngine PUBLIC Vulkan::Vulkan)
target_link_libraries(MiniEngine PRIVATE user32)
mini_enable_warnings(MiniEngine)
if(NOT MSVC)
    target_compile_options(MiniEngine PRIVATE -Wno-missing-field-initializers)
endif()

add_executable(MiniVulkanEngine runtime/main.cpp)
target_link_libraries(MiniVulkanEngine PRIVATE MiniEngine)
mini_enable_warnings(MiniVulkanEngine)
mini_link_mingw_runtime(MiniVulkanEngine)

add_custom_command(TARGET MiniVulkanEngine POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/config/engine.json"
            "$<TARGET_FILE_DIR:MiniVulkanEngine>/engine.json"
    COMMENT "Staging engine configuration"
)
