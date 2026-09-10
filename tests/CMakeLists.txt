function(mini_add_test target)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE MiniEngine)
    mini_enable_warnings(${target})
    mini_link_mingw_runtime(${target})
    set_target_properties(${target} PROPERTIES FOLDER Tests)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

mini_add_test(HashTest)
mini_add_test(ShaderCompilerTest)
target_compile_definitions(ShaderCompilerTest PRIVATE
    MINI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
    MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    MINI_TEST_GENERATED_SHADER_DIR="${MINI_GENERATED_SHADER_DIR}"
)

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
mini_add_test(TransferTest)
mini_add_test(MeshTest)
mini_add_test(TextureTest)
mini_add_test(RenderCacheTest)
mini_add_test(SceneTest)
mini_add_test(SceneAssetTest)
mini_add_test(RenderGraphTest)
mini_add_test(RenderTargetTest)

mini_add_test(MaterialTest)
target_compile_definitions(MaterialTest PRIVATE
    MINI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
    MINI_TEST_MATERIAL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

mini_add_test(ShaderAssetTest)
target_compile_definitions(ShaderAssetTest PRIVATE
    MINI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
    MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

mini_add_test(ShaderLayoutTest)
target_compile_definitions(ShaderLayoutTest PRIVATE
    MINI_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
)

mini_add_test(ShaderGeneratorTest)
target_compile_definitions(ShaderGeneratorTest PRIVATE
    MINI_TEST_SHADER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

mini_add_test(AssetPipelineTest)

mini_add_test(AssetReleaseTest)
target_compile_definitions(AssetReleaseTest PRIVATE
    MINI_TEST_COOKED_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
)
add_dependencies(AssetReleaseTest MiniAssetCooker)
add_custom_command(TARGET AssetReleaseTest POST_BUILD
    COMMAND "$<TARGET_FILE:MiniAssetCooker>"
            "${PROJECT_SOURCE_DIR}/assets"
            "${PROJECT_SOURCE_DIR}/library"
    COMMENT "Cooking release-test assets into the engine library"
)

add_test(
    NAME RenderRhiBoundaryTest
    COMMAND ${CMAKE_COMMAND}
        -DMINI_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RenderRhiBoundaryTest.cmake
)
