add_executable(MiniShaderCompiler shader_compiler/main.cpp)
target_link_libraries(MiniShaderCompiler PRIVATE MiniEngine)
target_compile_definitions(MiniShaderCompiler PRIVATE
    MINI_GLSLC_EXECUTABLE="${GLSLC_EXECUTABLE}"
    $<$<NOT:$<CONFIG:Debug>>:MINI_SHADER_RELEASE=1>
)
mini_enable_warnings(MiniShaderCompiler)
mini_link_mingw_runtime(MiniShaderCompiler)

add_executable(MiniAssetCooker asset_cooker/main.cpp)
target_link_libraries(MiniAssetCooker PRIVATE MiniEngine)
mini_enable_warnings(MiniAssetCooker)
mini_link_mingw_runtime(MiniAssetCooker)

add_custom_target(MiniShaderPackagedShaders
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${MINI_GENERATED_SHADER_DIR}/compiled"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${PROJECT_SOURCE_DIR}/assets/shaders/builtin_color.shader.json"
            Forward
            "${MINI_GENERATED_SHADER_DIR}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${PROJECT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
            Forward
            "${MINI_GENERATED_SHADER_DIR}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${PROJECT_SOURCE_DIR}/assets/shaders/blinn_phong.shader.json"
            Forward
            "${MINI_GENERATED_SHADER_DIR}"
    DEPENDS
        MiniShaderCompiler
        "${PROJECT_SOURCE_DIR}/assets/shaders/builtin_color.shader.json"
        "${PROJECT_SOURCE_DIR}/assets/shaders/builtin_color.Forward.vert"
        "${PROJECT_SOURCE_DIR}/assets/shaders/builtin_color.Forward.frag"
        "${PROJECT_SOURCE_DIR}/assets/shaders/vertex_color.shader.json"
        "${PROJECT_SOURCE_DIR}/assets/shaders/vertex_color.Forward.vert"
        "${PROJECT_SOURCE_DIR}/assets/shaders/vertex_color.Forward.frag"
        "${PROJECT_SOURCE_DIR}/assets/shaders/blinn_phong.shader.json"
        "${PROJECT_SOURCE_DIR}/assets/shaders/blinn_phong.Forward.vert"
        "${PROJECT_SOURCE_DIR}/assets/shaders/blinn_phong.Forward.frag"
        "${PROJECT_SOURCE_DIR}/assets/shaders/include/scene.glsl"
    COMMENT "Explicitly compiling packaged shaders through ShaderCompilePipeline"
    VERBATIM
)

set_target_properties(MiniShaderCompiler MiniAssetCooker MiniShaderPackagedShaders PROPERTIES
    FOLDER Tools
)
