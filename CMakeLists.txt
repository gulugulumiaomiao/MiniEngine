cmake_minimum_required(VERSION 3.24)
project(MiniVulkanEngine VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

if(MINGW)
    # Clang otherwise appends a dynamic libstdc++ after our explicit archive
    # group, which makes F5 depend on the compiler's bin directory being in PATH.
    add_link_options(-static-libstdc++ -static-libgcc)
endif()

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
    src/core/hash.cpp
    src/core/logging/Log.cpp
    src/core/serialization/BinaryTransfer.cpp
    src/core/filesystem/VirtualPath.cpp
    src/core/filesystem/DirectoryMount.cpp
    src/core/filesystem/FileDependencyGraph.cpp
    src/core/filesystem/FileSystem.cpp
    src/core/serialization/JsonTransfer.cpp
    src/core/serialization/Transfer.cpp
)
target_include_directories(MiniCore PUBLIC src)
target_include_directories(MiniCore SYSTEM PUBLIC third_party/glm/include)
target_include_directories(MiniCore SYSTEM PRIVATE third_party/nlohmann/include)
if(MSVC)
    target_compile_options(MiniCore PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(MiniAsset STATIC
    src/asset/base/Asset.cpp
    src/asset/base/AssetId.cpp
    src/asset/base/AssetMeta.cpp
    src/asset/derived_data/AssetArtifact.cpp
    src/asset/database/AssetDatabase.cpp
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
    src/asset/importer/AssetImportPipeline.cpp
    src/asset/importer/FileWatcher.cpp
    src/asset/importer/ShaderAssetImporter.cpp
    src/asset/importer/MaterialAssetImporter.cpp
    src/asset/importer/MeshAssetImporter.cpp
    src/asset/importer/TextureAssetImporter.cpp
    src/asset/importer/SceneAssetImporter.cpp
    src/asset/importer/BuiltinAssetImporters.cpp
    src/render/mesh/Mesh.cpp
    src/render/mesh/MeshManager.cpp
    src/render/mesh/MeshPrimitive.cpp
    src/render/mesh/MeshBuilder.cpp
    src/render/texture/Texture.cpp
    src/render/texture/TextureManager.cpp
)
target_include_directories(MiniAssetImporters PUBLIC src)
target_include_directories(MiniAssetImporters SYSTEM PRIVATE
    third_party/glm/include
    third_party/nlohmann/include
    third_party/stb
)
target_link_libraries(MiniAssetImporters PUBLIC MiniAsset)
if(MSVC)
    target_compile_options(MiniAssetImporters PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniAssetImporters PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(MiniScene STATIC
    src/scene/components/CameraComponent.cpp
    src/scene/components/Component.cpp
    src/scene/components/LightComponent.cpp
    src/scene/components/MaterialComponent.cpp
    src/scene/components/MeshComponent.cpp
    src/scene/node/Node.cpp
    src/scene/scene/Scene.cpp
    src/scene/scene/SceneAsset.cpp
    src/scene/components/TransformComponent.cpp
)
target_include_directories(MiniScene PUBLIC src)
target_include_directories(MiniScene SYSTEM PUBLIC third_party/glm/include)
target_include_directories(MiniScene SYSTEM PRIVATE third_party/nlohmann/include)
target_link_libraries(MiniScene PUBLIC MiniAsset)
target_link_libraries(MiniAssetImporters PUBLIC MiniScene)
if(MSVC)
    target_compile_options(MiniScene PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniScene PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(MiniShaderCompiler
    tools/shader_compiler/main.cpp
    src/render/shader/ShaderGenerator.cpp
    src/asset/manager/AssetManager.cpp
    src/render/material/Material.cpp
    src/render/material/MaterialManager.cpp
    src/render/shader/Shader.cpp
    src/render/shader/ShaderManager.cpp
    src/render/shader/ShaderCompiler.cpp
    src/render/shader/ShaderPreprocessor.cpp
    src/render/shader/ShaderCompilePipeline.cpp
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
target_compile_definitions(MiniShaderCompiler PRIVATE
    MINI_GLSLC_EXECUTABLE="${GLSLC_EXECUTABLE}"
    $<$<CONFIG:Debug>:MINI_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:MINI_SHADER_RELEASE=1>
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

add_executable(MiniAssetCooker
    tools/asset_cooker/main.cpp
    src/asset/manager/AssetManager.cpp
    src/render/material/Material.cpp
    src/render/material/MaterialManager.cpp
    src/render/shader/Shader.cpp
    src/render/shader/ShaderManager.cpp
)
target_include_directories(MiniAssetCooker PRIVATE src)
target_include_directories(MiniAssetCooker SYSTEM PRIVATE
    third_party/glm/include
    third_party/nlohmann/include
)
target_link_libraries(MiniAssetCooker PRIVATE
    MiniAssetImporters
    MiniSpirvCrossCore
)
if(MSVC)
    target_compile_options(MiniAssetCooker PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(MiniAssetCooker PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(MINGW)
    target_link_libraries(MiniAssetCooker PRIVATE
        "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
        "-Wl,--end-group,-Bdynamic"
    )
endif()

set(GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
set(SHADER_PIPELINE_STAMP "${GENERATED_SHADER_DIR}/compiled/pipeline.stamp")
add_custom_command(
    OUTPUT "${SHADER_PIPELINE_STAMP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_SHADER_DIR}/compiled"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
            Forward
            "${GENERATED_SHADER_DIR}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/blinn_phong.shader.json"
            Forward
            "${GENERATED_SHADER_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${SHADER_PIPELINE_STAMP}"
    DEPENDS
        MiniShaderCompiler
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.Forward.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vertex_color.Forward.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/blinn_phong.shader.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/blinn_phong.Forward.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/blinn_phong.Forward.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/include/scene.glsl"
    COMMENT "Compiling packaged shaders through ShaderCompilePipeline"
    VERBATIM
)
add_custom_target(MiniShaderPackagedShaders DEPENDS "${SHADER_PIPELINE_STAMP}")

add_executable(MiniVulkanEngine
    src/runtime/main.cpp
    src/runtime/application/GameApplication.cpp
    src/runtime/engine/Engine.cpp
    src/runtime/window/Window.cpp
    src/asset/manager/AssetManager.cpp
    src/render/material/Material.cpp
    src/render/material/MaterialManager.cpp
    src/render/render_graph/RenderGraph.cpp
    src/render/renderer/Renderer.cpp
    src/render/shader/Shader.cpp
    src/render/shader/ShaderManager.cpp
    src/render/shader/ShaderCompiler.cpp
    src/render/shader/ShaderPreprocessor.cpp
    src/render/shader/ShaderCompilePipeline.cpp
    src/render/shader/ShaderGenerator.cpp
    src/rhi/vulkan/VulkanBuffer.cpp
    src/rhi/vulkan/VulkanDescriptorAllocator.cpp
    src/rhi/vulkan/VulkanGraphicsPipeline.cpp
    src/rhi/vulkan/VulkanDevice.cpp
    src/rhi/vulkan/VulkanFactory.cpp
    src/rhi/vulkan/VulkanSwapchain.cpp
    src/rhi/vulkan/VulkanImage.cpp
    src/render/cache/MaterialGpuCache.cpp
    src/render/cache/MeshGpuCache.cpp
    src/render/cache/PipelineCache.cpp
    src/render/cache/RhiShaderCache.cpp
    src/render/cache/TextureGpuCache.cpp
    src/rhi/vulkan/VulkanSampler.cpp
    src/rhi/vulkan/VulkanShaderModule.cpp
    src/rhi/vulkan/VmaImplementation.cpp
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
    COMMAND "$<TARGET_FILE:MiniAssetCooker>"
            "${CMAKE_CURRENT_BINARY_DIR}/assets"
            "${CMAKE_CURRENT_BINARY_DIR}/library"
    COMMENT "Copying JSON shader and material assets"
)
add_dependencies(MiniVulkanEngine MiniAssetCooker MiniShaderPackagedShaders)
target_link_libraries(MiniVulkanEngine PRIVATE
    MiniAssetImporters
    MiniScene
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
    add_executable(HashTest tests/HashTest.cpp)
    target_link_libraries(HashTest PRIVATE MiniCore)
    if(MSVC)
        target_compile_options(HashTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(HashTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(HashTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME HashTest COMMAND HashTest)

    add_executable(ShaderCompilerTest
        tests/ShaderCompilerTest.cpp
        src/asset/manager/AssetManager.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
        src/render/shader/ShaderCompiler.cpp
        src/render/shader/ShaderPreprocessor.cpp
        src/render/shader/ShaderCompilePipeline.cpp
        src/render/shader/ShaderGenerator.cpp
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
    add_dependencies(ShaderCompilerTest MiniShaderPackagedShaders)
    if(MSVC)
        target_compile_options(ShaderCompilerTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ShaderCompilerTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(ShaderCompilerTest PRIVATE MiniAssetImporters MiniSpirvCrossCore)
    if(MINGW)
        target_link_libraries(ShaderCompilerTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
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
        src/asset/manager/AssetManager.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
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
    target_compile_definitions(AssetImporterTest PRIVATE
        MINI_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
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

    add_executable(HandlePoolTest tests/HandlePoolTest.cpp)
    target_link_libraries(HandlePoolTest PRIVATE MiniCore)
    if(MSVC)
        target_compile_options(HandlePoolTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(HandlePoolTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(HandlePoolTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME HandlePoolTest COMMAND HandlePoolTest)

    add_executable(TransferTest tests/TransferTest.cpp)
    target_link_libraries(TransferTest PRIVATE MiniCore)
    if(MSVC)
        target_compile_options(TransferTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(TransferTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(TransferTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME TransferTest COMMAND TransferTest)

    add_executable(MeshTest
        tests/MeshTest.cpp
        src/asset/manager/AssetManager.cpp
        src/render/cache/MeshGpuCache.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
    )
    target_link_libraries(MeshTest PRIVATE MiniAssetImporters MiniSpirvCrossCore Vulkan::Vulkan)
    target_include_directories(MeshTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    if(MSVC)
        target_compile_options(MeshTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(MeshTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(MeshTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME MeshTest COMMAND MeshTest)

    add_executable(TextureTest
        tests/TextureTest.cpp
        src/asset/manager/AssetManager.cpp
        src/render/cache/TextureGpuCache.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
    )
    target_link_libraries(TextureTest PRIVATE
        MiniAssetImporters
        MiniSpirvCrossCore
        Vulkan::Vulkan
    )
    target_include_directories(TextureTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    if(MSVC)
        target_compile_options(TextureTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(TextureTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(TextureTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME TextureTest COMMAND TextureTest)

    add_executable(SceneTest tests/SceneTest.cpp)
    target_link_libraries(SceneTest PRIVATE MiniScene)
    target_include_directories(SceneTest SYSTEM PRIVATE third_party/glm/include)
    if(MSVC)
        target_compile_options(SceneTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(SceneTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(SceneTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME SceneTest COMMAND SceneTest)

    add_executable(SceneAssetTest tests/SceneAssetTest.cpp)
    target_link_libraries(SceneAssetTest PRIVATE MiniScene MiniAsset)
    target_include_directories(SceneAssetTest SYSTEM PRIVATE third_party/glm/include)
    if(MSVC)
        target_compile_options(SceneAssetTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(SceneAssetTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_libraries(SceneAssetTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME SceneAssetTest COMMAND SceneAssetTest)

    add_executable(RenderGraphTest
        tests/RenderGraphTest.cpp
        src/render/render_graph/RenderGraph.cpp
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
        src/asset/manager/AssetManager.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
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
    target_link_libraries(MaterialTest PRIVATE MiniAssetImporters MiniSpirvCrossCore)
    if(MINGW)
        target_link_libraries(MaterialTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME MaterialTest COMMAND MaterialTest)

    add_executable(ShaderAssetTest
        tests/ShaderAssetTest.cpp
        src/asset/manager/AssetManager.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
    )
    target_include_directories(ShaderAssetTest PRIVATE src)
    target_include_directories(ShaderAssetTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    target_compile_definitions(ShaderAssetTest PRIVATE
        MINI_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/assets"
    )
    if(MSVC)
        target_compile_options(ShaderAssetTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ShaderAssetTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(ShaderAssetTest PRIVATE MiniAssetImporters MiniSpirvCrossCore)
    if(MINGW)
        target_link_libraries(ShaderAssetTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME ShaderAssetTest COMMAND ShaderAssetTest)

    add_executable(ShaderLayoutTest
        tests/ShaderLayoutTest.cpp
        src/asset/manager/AssetManager.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
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
    target_link_libraries(ShaderLayoutTest PRIVATE MiniAssetImporters MiniSpirvCrossCore)
    if(MINGW)
        target_link_libraries(ShaderLayoutTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME ShaderLayoutTest COMMAND ShaderLayoutTest)

    add_executable(ShaderGeneratorTest
        tests/ShaderGeneratorTest.cpp
        src/render/shader/ShaderGenerator.cpp
        src/asset/manager/AssetManager.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
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
    target_link_libraries(ShaderGeneratorTest PRIVATE MiniAssetImporters MiniSpirvCrossCore)
    if(MINGW)
        target_link_libraries(ShaderGeneratorTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME ShaderGeneratorTest COMMAND ShaderGeneratorTest)

    add_executable(AssetPipelineTest
        tests/AssetPipelineTest.cpp
        src/asset/manager/AssetManager.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
    )
    target_include_directories(AssetPipelineTest PRIVATE src)
    target_include_directories(AssetPipelineTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    if(MSVC)
        target_compile_options(AssetPipelineTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(AssetPipelineTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(AssetPipelineTest PRIVATE
        MiniAssetImporters
        MiniSpirvCrossCore
    )
    if(MINGW)
        target_link_libraries(AssetPipelineTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_test(NAME AssetPipelineTest COMMAND AssetPipelineTest)

    add_executable(AssetReleaseTest
        tests/AssetReleaseTest.cpp
        src/asset/manager/AssetManager.cpp
        src/render/material/Material.cpp
        src/render/material/MaterialManager.cpp
        src/render/shader/Shader.cpp
        src/render/shader/ShaderManager.cpp
    )
    target_include_directories(AssetReleaseTest PRIVATE src)
    target_include_directories(AssetReleaseTest SYSTEM PRIVATE
        third_party/glm/include
        third_party/nlohmann/include
    )
    target_compile_definitions(AssetReleaseTest PRIVATE
        MINI_RELEASE=1
        MINI_TEST_COOKED_ASSET_DIR="${CMAKE_CURRENT_BINARY_DIR}/assets"
    )
    if(MSVC)
        target_compile_options(AssetReleaseTest PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(AssetReleaseTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(AssetReleaseTest PRIVATE
        MiniAssetImporters
        MiniSpirvCrossCore
    )
    if(MINGW)
        target_link_libraries(AssetReleaseTest PRIVATE
            "-Wl,-Bstatic,--start-group" stdc++ gcc gcc_eh winpthread
            "-Wl,--end-group,-Bdynamic"
        )
    endif()
    add_dependencies(AssetReleaseTest MiniVulkanEngine)
    add_test(NAME AssetReleaseTest COMMAND AssetReleaseTest)

    add_test(
        NAME RenderRhiBoundaryTest
        COMMAND ${CMAKE_COMMAND}
            -DMINI_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/RenderRhiBoundaryTest.cmake
    )
endif()
