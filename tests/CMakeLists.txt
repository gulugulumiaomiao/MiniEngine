function(mini_add_test target)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE MiniEngine)
    target_compile_definitions(${target} PRIVATE MINI_TEST_BUILTIN_DIR="${PROJECT_SOURCE_DIR}/builtin")
    mini_enable_warnings(${target})
    mini_link_mingw_runtime(${target})
    set_target_properties(${target} PROPERTIES FOLDER Tests)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

# Tests that exercise editor-only engine features link the MiniEngineEditor variant and
# therefore see MINI_EDITOR. Extra arguments are additional sources, which lets a test
# pull in editor implementation files from tools/editor.
function(mini_add_editor_test target)
    add_executable(${target} ${target}.cpp ${ARGN})
    target_link_libraries(${target} PRIVATE MiniEngineEditor)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR})
    target_compile_definitions(${target} PRIVATE MINI_TEST_BUILTIN_DIR="${PROJECT_SOURCE_DIR}/builtin")
    mini_enable_warnings(${target})
    mini_link_mingw_runtime(${target})
    set_target_properties(${target} PROPERTIES FOLDER Tests)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

mini_add_test(BuildConfigTest)
mini_add_editor_test(BuildConfigEditorTest)
mini_add_test(HashTest)
mini_add_test(ShaderCompilerTest)
target_compile_definitions(ShaderCompilerTest PRIVATE
    MINI_TEST_ASSET_DIR="${CMAKE_BINARY_DIR}/assets"
    MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    MINI_TEST_GENERATED_SHADER_DIR="${MINI_GENERATED_SHADER_DIR}"
)
add_dependencies(ShaderCompilerTest MiniCopyBuiltin)

mini_add_test(FileSystemTest)
mini_add_test(AssetFoundationTest)
mini_add_test(AssetImporterTest)
target_compile_definitions(AssetImporterTest PRIVATE
    MINI_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

mini_add_test(LogTest)
add_test(NAME LogFatalTest COMMAND LogTest fatal)
set_tests_properties(LogFatalTest PROPERTIES WILL_FAIL TRUE)

mini_add_test(HandlePoolTest)
mini_add_test(FrustumTest)
mini_add_test(TransferTest)
mini_add_test(MeshTest)
mini_add_test(TextureTest)
mini_add_test(RenderCacheTest)
mini_add_test(SceneTest)
mini_add_test(SceneAssetTest)
mini_add_test(SceneExportTest)
target_compile_definitions(SceneExportTest PRIVATE
    MINI_TEST_ASSET_DIR="${CMAKE_BINARY_DIR}/assets"
)
add_dependencies(SceneExportTest MiniCopyBuiltin)

mini_add_test(ProjectConfigTest)
# 同一套配置测试在无 MINI_EDITOR 的普通引擎和编辑器变体中分别执行。
add_executable(ProjectConfigEditorTest ProjectConfigTest.cpp)
target_link_libraries(ProjectConfigEditorTest PRIVATE MiniEngineEditor)
mini_enable_warnings(ProjectConfigEditorTest)
mini_link_mingw_runtime(ProjectConfigEditorTest)
set_target_properties(ProjectConfigEditorTest PROPERTIES FOLDER Tests)
add_test(NAME ProjectConfigEditorTest COMMAND ProjectConfigEditorTest)
mini_add_editor_test(ProjectSceneTest)
mini_add_editor_test(ProjectTemplateTest)
# The template test also runs its checks against a project created inside the build tree,
# which shares a volume with MINI_TEST_BUILTIN_DIR in a normal checkout. That matters:
# copy_file only refuses to replace an existing file when source and destination sit on
# one volume, so a project under temp_directory_path() alone would not reproduce it.
target_compile_definitions(ProjectTemplateTest PRIVATE
    MINI_TEST_SCRATCH_DIR="${CMAKE_BINARY_DIR}/template-test"
)
mini_add_editor_test(ProjectRegistryTest)
mini_add_editor_test(ProjectLifecycleTest)
mini_add_test(RenderGraphTest)
mini_add_test(RenderTargetTest)

# The editor UI backend lives with the editor, so its test compiles that translation
# unit directly; the UI shaders are baked into it as SPIR-V words, so no shader build
# step or generated-shader include directory is needed.
add_executable(ImGuiRendererTest
    ImGuiRendererTest.cpp
    ${PROJECT_SOURCE_DIR}/tools/editor/ImGuiRenderer.cpp
)
target_include_directories(ImGuiRendererTest PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(ImGuiRendererTest PRIVATE MiniEngine MiniImGui)
mini_enable_warnings(ImGuiRendererTest)
if(NOT MSVC)
    target_compile_options(ImGuiRendererTest PRIVATE -Wno-missing-field-initializers)
endif()
mini_link_mingw_runtime(ImGuiRendererTest)
set_target_properties(ImGuiRendererTest PROPERTIES FOLDER Tests)
add_test(NAME ImGuiRendererTest COMMAND ImGuiRendererTest)

mini_add_test(VulkanPipelineCacheTest)
target_link_libraries(VulkanPipelineCacheTest PRIVATE user32)
target_include_directories(VulkanPipelineCacheTest SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/vma/include
)
if(NOT MSVC)
    target_compile_options(VulkanPipelineCacheTest PRIVATE -Wno-missing-field-initializers)
endif()
mini_add_test(RgTexturePoolTest)
mini_add_test(RenderPipelineTest)
mini_add_test(RenderQueueTest)
mini_add_test(DrawBatcherTest)
mini_add_test(RenderPassTest)

mini_add_test(MaterialTest)
target_compile_definitions(MaterialTest PRIVATE
    MINI_TEST_ASSET_DIR="${CMAKE_BINARY_DIR}/assets"
    MINI_TEST_MATERIAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
add_dependencies(MaterialTest MiniCopyBuiltin)

mini_add_test(ShaderAssetTest)
target_compile_definitions(ShaderAssetTest PRIVATE
    MINI_TEST_ASSET_DIR="${CMAKE_BINARY_DIR}/assets"
    MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
add_dependencies(ShaderAssetTest MiniCopyBuiltin)

mini_add_test(ShaderLayoutTest)
target_compile_definitions(ShaderLayoutTest PRIVATE
    MINI_TEST_ASSET_DIR="${CMAKE_BINARY_DIR}/assets"
)
add_dependencies(ShaderLayoutTest MiniCopyBuiltin)

mini_add_test(ShaderGeneratorTest)
target_compile_definitions(ShaderGeneratorTest PRIVATE
    MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

mini_add_test(AssetPipelineTest)

mini_add_test(AssetReleaseTest)
# The packaged-release test cooks its assets from the build-local copy of builtin
# (MiniCopyBuiltin, meta-free) into the build-local library: cooking against the source
# tree would let the importer self-heal *.meta sidecars back into builtin/, and the
# library output belongs under the build directory anyway.
target_compile_definitions(AssetReleaseTest PRIVATE
    MINI_TEST_COOKED_ASSET_DIR="${CMAKE_BINARY_DIR}/assets"
)
add_dependencies(AssetReleaseTest MiniAssetCooker MiniCopyBuiltin)
add_custom_command(TARGET AssetReleaseTest POST_BUILD
    # Purge the build-local library first: it is shared with the other asset tests and
    # may hold records keyed by asset IDs that no longer exist (stale metas purged from
    # the source tree), which makes the cook reject the freshly imported paths.
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/library"
    COMMAND "$<TARGET_FILE:MiniAssetCooker>"
            "${CMAKE_BINARY_DIR}/assets"
            "${CMAKE_BINARY_DIR}/library"
    COMMENT "Cooking packaged-release test assets into the build library"
)

add_test(
    NAME RenderRhiBoundaryTest
    COMMAND ${CMAKE_COMMAND}
        -DMINI_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RenderRhiBoundaryTest.cmake
)



mini_add_test(SwapchainLayoutTest)
target_include_directories(SwapchainLayoutTest SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/vma/include
)
target_link_libraries(SwapchainLayoutTest PRIVATE user32)
