cmake_minimum_required(VERSION 3.24)
project(MiniVulkanEngine VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

add_library(MiniSpirvCrossCore STATIC
    third_party/spirv-cross/spirv_cross.cpp
    third_party/spirv-cross/spirv_parser.cpp
    third_party/spirv-cross/spirv_cross_parsed_ir.cpp
    third_party/spirv-cross/spirv_cfg.cpp
)
target_include_directories(MiniSpirvCrossCore SYSTEM PUBLIC
    third_party/spirv-cross
)

find_program(GLSLC_EXECUTABLE
    NAMES glslc glslc.exe
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

add_library(MiniCore STATIC
    src/core/Log.cpp
    src/core/io/VirtualPath.cpp
    src/core/io/DirectoryMount.cpp
    src/core/io/FileSystem.cpp
)
target_include_directories(MiniCore PUBLIC src)
if(MSVC)
    target_compile_options(MiniCore PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(MiniAsset STATIC
    src/asset/Asset.cpp
    src/asset/AssetId.cpp
    src/asset/AssetMeta.cpp
    src/asset/AssetArtifact.cpp
    src/asset/AssetDatabase.cpp
    src/asset/importer/IAssetImporter.cpp
    src/asset/importer/AssetImporterRegistry.cpp
)
target_include_directories(MiniAsset PUBLIC src)
target_include_directories(MiniAsset SYSTEM PRIVATE third_party/nlohmann/include)
target_link_libraries(MiniAsset PUBLIC MiniCore)
if(MSVC)
    target_compile_options(MiniAsset PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniAsset PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(MiniAssetImporters STATIC
    src/asset/importer/ShaderAssetImporter.cpp
    src/asset/importer/MaterialAssetImporter.cpp
    src/asset/importer/BuiltinAssetImporters.cpp
)
target_include_directories(MiniAssetImporters PUBLIC src)
target_include_directories(MiniAssetImporters SYSTEM PRIVATE
    third_party/glm/include
    third_party/nlohmann/include
)
target_link_libraries(MiniAssetImporters PUBLIC MiniAsset)
if(MSVC)
    target_compile_options(MiniAssetImporters PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniAssetImporters PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(MiniShaderCompiler
    tools/shader_compiler/main.cpp
    src/renderer/ShaderGenerator.cpp
    src/renderer/AssetManager.cpp
    src/renderer/Shader.cpp
    src/renderer/ShaderCompiler.cpp
)
target_link_libraries(MiniShaderCompiler PRIVATE
    MiniAssetImporters
    MiniSpirvCrossCore
)
target_include_directories(MiniShaderCompiler PRIVATE
    src
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_include_directories(MiniShaderCompiler SYSTEM PRIVATE
    third_party/glm/include
    third_party/nlohmann/include
)
if(MSVC)
    target_compile_options(MiniShaderCompiler PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniShaderCompiler PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(MINGW)
    target_link_libraries(MiniShaderCompiler PRIVATE
        "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
        "-Wl,--end-group,-Bdynamic"
    )
endif()

set(GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
set(GENERATED_MATERIAL_DECLARATIONS
    "${GENERATED_SHADER_DIR}/vertex_color.material.glsl"
)
set(GENERATED_VERTEX_STAGE
    "${GENERATED_SHADER_DIR}/vertex_color.Forward.vert.glsl"
)
set(GENERATED_FRAGMENT_STAGE
    "${GENERATED_SHADER_DIR}/vertex_color.Forward.frag.glsl"
)
set(GENERATED_VERTEX_SPIRV
    "${GENERATED_SHADER_DIR}/vertex_color.Forward.vert.spv"
)
set(GENERATED_FRAGMENT_SPIRV
    "${GENERATED_SHADER_DIR}/vertex_color.Forward.frag.spv"
)
add_custom_command(
    OUTPUT
        "${GENERATED_MATERIAL_DECLARATIONS}"
        "${GENERATED_VERTEX_STAGE}"
        "${GENERATED_FRAGMENT_STAGE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_SHADER_DIR}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" properties
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
            "${GENERATED_MATERIAL_DECLARATIONS}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" stages
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
            Forward
            "${GENERATED_VERTEX_STAGE}"
            "${GENERATED_FRAGMENT_STAGE}"
    DEPENDS
        MiniShaderCompiler
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.Forward.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.Forward.frag"
    COMMENT "Generating ShaderLab material and pass GLSL"
    VERBATIM
)
add_custom_target(MiniShaderLabGeneratedSources
    DEPENDS
        "${GENERATED_MATERIAL_DECLARATIONS}"
        "${GENERATED_VERTEX_STAGE}"
        "${GENERATED_FRAGMENT_STAGE}"
)

set(SHADERLAB_GLSLC_OPTIONS
    --target-env=vulkan1.3
    "$<$<CONFIG:Debug>:-O0>"
    "$<$<CONFIG:Debug>:-g>"
    "$<$<NOT:$<CONFIG:Debug>>:-O>"
)
set(GENERATED_VERTEX_REFLECTION_SPIRV
    "${GENERATED_SHADER_DIR}/vertex_color.Forward.vert.reflection.spv"
)
set(GENERATED_FRAGMENT_REFLECTION_SPIRV
    "${GENERATED_SHADER_DIR}/vertex_color.Forward.frag.reflection.spv"
)
add_custom_command(
    OUTPUT "${GENERATED_VERTEX_SPIRV}"
    COMMAND "${GLSLC_EXECUTABLE}"
            ${SHADERLAB_GLSLC_OPTIONS}
            -fshader-stage=vert
            "${GENERATED_VERTEX_STAGE}"
            -o "${GENERATED_VERTEX_SPIRV}"
    DEPENDS
        MiniShaderLabGeneratedSources
        "${GENERATED_VERTEX_STAGE}"
    COMMENT "Compiling ShaderLab vertex_color/Forward vertex stage"
    COMMAND_EXPAND_LISTS
    VERBATIM
)
add_custom_command(
    OUTPUT "${GENERATED_FRAGMENT_SPIRV}"
    COMMAND "${GLSLC_EXECUTABLE}"
            ${SHADERLAB_GLSLC_OPTIONS}
            -fshader-stage=frag
            "${GENERATED_FRAGMENT_STAGE}"
            -o "${GENERATED_FRAGMENT_SPIRV}"
    DEPENDS
        MiniShaderLabGeneratedSources
        "${GENERATED_FRAGMENT_STAGE}"
    COMMENT "Compiling ShaderLab vertex_color/Forward fragment stage"
    COMMAND_EXPAND_LISTS
    VERBATIM
)
add_custom_command(
    OUTPUT "${GENERATED_VERTEX_REFLECTION_SPIRV}"
    COMMAND "${GLSLC_EXECUTABLE}"
            --target-env=vulkan1.3
            -O0
            -g
            -fshader-stage=vert
            "${GENERATED_VERTEX_STAGE}"
            -o "${GENERATED_VERTEX_REFLECTION_SPIRV}"
    DEPENDS
        MiniShaderLabGeneratedSources
        "${GENERATED_VERTEX_STAGE}"
    COMMENT "Compiling reflection-friendly ShaderLab vertex stage"
    VERBATIM
)
add_custom_command(
    OUTPUT "${GENERATED_FRAGMENT_REFLECTION_SPIRV}"
    COMMAND "${GLSLC_EXECUTABLE}"
            --target-env=vulkan1.3
            -O0
            -g
            -fshader-stage=frag
            "${GENERATED_FRAGMENT_STAGE}"
            -o "${GENERATED_FRAGMENT_REFLECTION_SPIRV}"
    DEPENDS
        MiniShaderLabGeneratedSources
        "${GENERATED_FRAGMENT_STAGE}"
    COMMENT "Compiling reflection-friendly ShaderLab fragment stage"
    VERBATIM
)
add_custom_target(MiniShaderLabCompiledShaders
    DEPENDS
        "${GENERATED_VERTEX_SPIRV}"
        "${GENERATED_FRAGMENT_SPIRV}"
        "${GENERATED_VERTEX_REFLECTION_SPIRV}"
        "${GENERATED_FRAGMENT_REFLECTION_SPIRV}"
)

set(SHADER_REFLECTION_STAMP
    "${GENERATED_SHADER_DIR}/vertex_color.Forward.reflection.validated"
)
add_custom_command(
    OUTPUT "${SHADER_REFLECTION_STAMP}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" reflect
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
            Forward
            "${GENERATED_VERTEX_REFLECTION_SPIRV}"
            "${GENERATED_FRAGMENT_REFLECTION_SPIRV}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${SHADER_REFLECTION_STAMP}"
    DEPENDS
        MiniShaderCompiler
        "${GENERATED_VERTEX_REFLECTION_SPIRV}"
        "${GENERATED_FRAGMENT_REFLECTION_SPIRV}"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
    COMMENT "Validating ShaderLab declarations against SPIR-V reflection"
    VERBATIM
)
add_custom_target(MiniShaderLabReflection
    DEPENDS "${SHADER_REFLECTION_STAMP}"
)

add_executable(MiniVulkanEngine
    src/main.cpp
    src/core/Application.cpp
    src/platform/Window.cpp
    src/renderer/AssetManager.cpp
    src/renderer/Material.cpp
    src/renderer/Mesh.cpp
    src/renderer/RenderGraph.cpp
    src/renderer/Renderer.cpp
    src/renderer/Shader.cpp
    src/renderer/ShaderCompiler.cpp
    src/renderer/ShaderGenerator.cpp
    src/rhi/vulkan/Buffer.cpp
    src/rhi/vulkan/DescriptorAllocator.cpp
    src/rhi/vulkan/GpuAllocator.cpp
    src/rhi/vulkan/GraphicsPipeline.cpp
    src/rhi/vulkan/Image.cpp
    src/rhi/vulkan/MaterialGpuCache.cpp
    src/rhi/vulkan/PipelineCache.cpp
    src/rhi/vulkan/RhiShaderCache.cpp
    src/rhi/vulkan/Sampler.cpp
    src/rhi/vulkan/ShaderModule.cpp
    src/rhi/vulkan/VmaImplementation.cpp
    src/rhi/vulkan/VulkanBackend.cpp
    src/rhi/vulkan/VulkanCommandEncoder.cpp
)
target_include_directories(MiniVulkanEngine PRIVATE
    src
)
target_include_directories(MiniVulkanEngine SYSTEM PRIVATE third_party/vma/include)
target_include_directories(MiniVulkanEngine SYSTEM PRIVATE third_party/nlohmann/include)
target_include_directories(MiniVulkanEngine SYSTEM PRIVATE third_party/glm/include)
target_compile_definitions(MiniVulkanEngine PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    MINI_GENERATED_SHADER_DIR="${GENERATED_SHADER_DIR}"
    MINI_GLSLC_EXECUTABLE="${GLSLC_EXECUTABLE}"
    MINI_ASSET_DIR="${CMAKE_CURRENT_BINARY_DIR}/assets"
    $<$<CONFIG:Debug>:MINI_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:MINI_RELEASE=1>
)

add_custom_command(TARGET MiniVulkanEngine POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMENT "Copying JSON shader and material assets"
)
target_link_libraries(MiniVulkanEngine PRIVATE
    MiniAssetImporters
    MiniSpirvCrossCore
    Vulkan::Vulkan
    user32
)

# Produce a self-contained executable with the MinGW/WinLibs runtime linked in.
# Vulkan and Win32 remain normal system DLL imports.
if(MINGW)
    # WinLibs uses the POSIX threading model. libgcc_eh and winpthread have a
    # circular archive dependency, so they must be resolved as one link group.
    target_link_libraries(MiniVulkanEngine PRIVATE
        "-Wl,-Bstatic,--start-group"
        stdc++
        gcc
        gcc_eh
        winpthread
        "-Wl,--end-group,-Bdynamic"
    )
endif()

if(MSVC)
    target_compile_options(MiniVulkanEngine PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniVulkanEngine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
    )
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT MiniVulkanEngine)

if(BUILD_TESTING)
    add_executable(ShaderCompilerTest
        tests/ShaderCompilerTest.cpp
        src/renderer/AssetManager.cpp
        src/renderer/Shader.cpp
        src/renderer/ShaderCompiler.cpp
        src/renderer/ShaderGenerator.cpp
    )
    target_include_directories(ShaderCompilerTest PRIVATE src)
    target_include_directories(ShaderCompilerTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    target_compile_definitions(ShaderCompilerTest PRIVATE
        MINI_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
        MINI_TEST_GENERATED_SHADER_DIR="${GENERATED_SHADER_DIR}"
        MINI_GLSLC_EXECUTABLE="${GLSLC_EXECUTABLE}"
    )
    add_dependencies(ShaderCompilerTest MiniShaderLabReflection)
    if(MSVC)
        target_compile_options(ShaderCompilerTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ShaderCompilerTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(ShaderCompilerTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    target_link_libraries(ShaderCompilerTest PRIVATE MiniAsset MiniSpirvCrossCore)
    add_test(NAME ShaderCompilerTest COMMAND ShaderCompilerTest)

    add_executable(FileSystemTest tests/FileSystemTest.cpp)
    target_link_libraries(FileSystemTest PRIVATE MiniCore)
    if(MSVC)
        target_compile_options(FileSystemTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(FileSystemTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(FileSystemTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME FileSystemTest COMMAND FileSystemTest)

    add_executable(AssetFoundationTest tests/AssetFoundationTest.cpp)
    target_include_directories(AssetFoundationTest PRIVATE src)
    target_link_libraries(AssetFoundationTest PRIVATE MiniAsset)
    if(MSVC)
        target_compile_options(AssetFoundationTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(AssetFoundationTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(AssetFoundationTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME AssetFoundationTest COMMAND AssetFoundationTest)

    add_executable(AssetImporterTest
        tests/AssetImporterTest.cpp
        src/renderer/Shader.cpp
    )
    target_include_directories(AssetImporterTest PRIVATE src)
    target_include_directories(AssetImporterTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    target_link_libraries(AssetImporterTest PRIVATE
        MiniAssetImporters
        MiniSpirvCrossCore
    )
    if(MSVC)
        target_compile_options(AssetImporterTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(AssetImporterTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(AssetImporterTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME AssetImporterTest COMMAND AssetImporterTest)

    add_executable(LogTest tests/LogTest.cpp)
    target_link_libraries(LogTest PRIVATE MiniCore)
    if(MSVC)
        target_compile_options(LogTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(LogTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(LogTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME LogTest COMMAND LogTest)
    add_test(NAME LogFatalTest COMMAND LogTest fatal)
    set_tests_properties(LogFatalTest PROPERTIES WILL_FAIL TRUE)

    add_executable(RenderGraphTest
        tests/RenderGraphTest.cpp
        src/renderer/RenderGraph.cpp
    )
    target_include_directories(RenderGraphTest PRIVATE src)
    target_include_directories(RenderGraphTest SYSTEM PRIVATE third_party/glm/include)
    if(MSVC)
        target_compile_options(RenderGraphTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(RenderGraphTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(RenderGraphTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME RenderGraphTest COMMAND RenderGraphTest)
    target_link_libraries(RenderGraphTest PRIVATE MiniCore)

    add_executable(MaterialTest
        tests/MaterialTest.cpp
        src/renderer/AssetManager.cpp
        src/renderer/Material.cpp
        src/renderer/Mesh.cpp
        src/renderer/Shader.cpp
    )
    target_include_directories(MaterialTest PRIVATE src)
    target_include_directories(MaterialTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    target_compile_definitions(MaterialTest PRIVATE
        MINI_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        MINI_TEST_MATERIAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
    )
    if(MSVC)
        target_compile_options(MaterialTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MaterialTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(MaterialTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME MaterialTest COMMAND MaterialTest)
    target_link_libraries(MaterialTest PRIVATE MiniAsset MiniSpirvCrossCore)

    add_executable(ShaderAssetTest
        tests/ShaderAssetTest.cpp
        src/renderer/AssetManager.cpp
        src/renderer/Shader.cpp
    )
    target_include_directories(ShaderAssetTest PRIVATE src)
    target_include_directories(ShaderAssetTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    target_compile_definitions(ShaderAssetTest PRIVATE
        MINI_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
        MINI_TEST_GENERATED_SHADER_DIR="${GENERATED_SHADER_DIR}"
    )
    add_dependencies(ShaderAssetTest MiniShaderLabReflection)
    if(MSVC)
        target_compile_options(ShaderAssetTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ShaderAssetTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(ShaderAssetTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME ShaderAssetTest COMMAND ShaderAssetTest)
    target_link_libraries(ShaderAssetTest PRIVATE MiniAsset MiniSpirvCrossCore)

    add_executable(ShaderLayoutTest
        tests/ShaderLayoutTest.cpp
        src/renderer/AssetManager.cpp
        src/renderer/Shader.cpp
    )
    target_include_directories(ShaderLayoutTest PRIVATE src)
    target_include_directories(ShaderLayoutTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    target_compile_definitions(ShaderLayoutTest PRIVATE
        MINI_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )
    if(MSVC)
        target_compile_options(ShaderLayoutTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ShaderLayoutTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(ShaderLayoutTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME ShaderLayoutTest COMMAND ShaderLayoutTest)
    target_link_libraries(ShaderLayoutTest PRIVATE MiniAsset MiniSpirvCrossCore)

    add_executable(ShaderGeneratorTest
        tests/ShaderGeneratorTest.cpp
        src/renderer/ShaderGenerator.cpp
        src/renderer/AssetManager.cpp
        src/renderer/Shader.cpp
    )
    target_include_directories(ShaderGeneratorTest PRIVATE
        src
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_include_directories(ShaderGeneratorTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    target_compile_definitions(ShaderGeneratorTest PRIVATE
        MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
    )
    if(MSVC)
        target_compile_options(ShaderGeneratorTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ShaderGeneratorTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(ShaderGeneratorTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME ShaderGeneratorTest COMMAND ShaderGeneratorTest)
    target_link_libraries(ShaderGeneratorTest PRIVATE MiniAsset MiniSpirvCrossCore)
endif()
