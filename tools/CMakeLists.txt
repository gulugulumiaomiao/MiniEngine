add_executable(MiniShaderCompiler shader_compiler/main.cpp)
target_link_libraries(MiniShaderCompiler PRIVATE MiniEngine)
target_compile_definitions(MiniShaderCompiler PRIVATE
    MINI_GLSLC_EXECUTABLE="${GLSLC_EXECUTABLE}"
    $<$<CONFIG:Publish>:MINI_SHADER_RELEASE=1>
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
            "${PROJECT_SOURCE_DIR}/builtin/shaders/builtin_color.shader.json"
            Forward
            "${MINI_GENERATED_SHADER_DIR}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${PROJECT_SOURCE_DIR}/builtin/shaders/vertex_color.shader.json"
            Forward
            "${MINI_GENERATED_SHADER_DIR}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${PROJECT_SOURCE_DIR}/builtin/shaders/blinn_phong.shader.json"
            Forward
            "${MINI_GENERATED_SHADER_DIR}"
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${PROJECT_SOURCE_DIR}/builtin/shaders/blinn_phong.shader.json"
            Forward
            "${MINI_GENERATED_SHADER_DIR}"
            RECEIVE_SHADOWS
    COMMAND "$<TARGET_FILE:MiniShaderCompiler>" compile
            "${PROJECT_SOURCE_DIR}/builtin/shaders/blinn_phong.shader.json"
            ShadowCaster
            "${MINI_GENERATED_SHADER_DIR}"
    DEPENDS
        MiniShaderCompiler
        "${PROJECT_SOURCE_DIR}/builtin/shaders/builtin_color.shader.json"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/builtin_color.Forward.vert"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/builtin_color.Forward.frag"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/vertex_color.shader.json"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/vertex_color.Forward.vert"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/vertex_color.Forward.frag"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/blinn_phong.shader.json"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/blinn_phong.Forward.vert"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/blinn_phong.Forward.frag"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/blinn_phong.ShadowCaster.vert"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/blinn_phong.ShadowCaster.frag"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/include/scene.glsl"
        "${PROJECT_SOURCE_DIR}/builtin/shaders/include/objects.glsl"
    COMMENT "Explicitly compiling packaged shaders through ShaderCompilePipeline"
    VERBATIM
)

set_target_properties(MiniShaderCompiler MiniAssetCooker MiniShaderPackagedShaders PROPERTIES
    FOLDER Tools
)

# ---------------------------------------------------------------------------
# MiniEditor: minimal Unity-style scene editor built on Dear ImGui. Only the
# Win32 platform backend is vendored; drawing goes through ImGuiRenderer, which
# records into the engine RHI. The engine renders into the swapchain; the ImGui
# panels are recorded as an IFrameOverlay after the render pipeline in the same
# command buffer.
# ---------------------------------------------------------------------------
add_library(MiniImGui STATIC
    ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_win32.cpp
)
target_include_directories(MiniImGui SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party/imgui
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends
)
# imgui_impl_win32 needs gdi32 (DPI) and dwmapi (title bar dark mode); MinGW does
# not honor the translation-unit #pragma comment(lib, ...) hints.
target_link_libraries(MiniImGui PUBLIC gdi32 dwmapi imm32)
set_target_properties(MiniImGui PROPERTIES FOLDER ThirdParty)

# The editor UI pipeline is built directly from the RHI, so its shaders bypass the
# ShaderLab asset pipeline: glslc emits them as comma separated words that
# ImGuiRenderer.cpp includes inside constexpr uint32 arrays. The fragment stage is
# compiled twice because an sRGB attachment needs ImGui's colors decoded first.
function(mini_compile_editor_shader source output)
    add_custom_command(
        OUTPUT "${MINI_EDITOR_SHADER_DIR}/${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${MINI_EDITOR_SHADER_DIR}"
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O -mfmt=num ${ARGN}
                -o "${MINI_EDITOR_SHADER_DIR}/${output}"
                "${CMAKE_CURRENT_SOURCE_DIR}/editor/shaders/${source}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/editor/shaders/${source}"
        COMMENT "Compiling editor UI shader ${output}"
        VERBATIM
    )
endfunction()
mini_compile_editor_shader(imgui.vert imgui.vert.inc)
mini_compile_editor_shader(imgui.frag imgui.frag.inc)
mini_compile_editor_shader(imgui.frag imgui.frag.srgb.inc -DSRGB_TARGET)
add_custom_target(MiniEditorShaders DEPENDS
    "${MINI_EDITOR_SHADER_DIR}/imgui.vert.inc"
    "${MINI_EDITOR_SHADER_DIR}/imgui.frag.inc"
    "${MINI_EDITOR_SHADER_DIR}/imgui.frag.srgb.inc"
)
set_target_properties(MiniEditorShaders PROPERTIES FOLDER Tools)

# 编辑器配置、项目注册与模板仅编入编辑器变体，供编辑器接口和测试复用。
target_sources(MiniEngineEditor PRIVATE
    editor/EditorConfig.cpp
    editor/ProjectRegistry.cpp
    editor/ProjectTemplate.cpp
)
target_include_directories(MiniEngineEditor PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(MiniEngineEditor PRIVATE shell32)
source_group(TREE "${PROJECT_SOURCE_DIR}" FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/EditorConfig.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/ProjectRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/ProjectTemplate.cpp"
)

add_executable(MiniEditor
    editor/main.cpp
    editor/EditorApplication.cpp
    editor/ImGuiLayer.cpp
    editor/ImGuiRenderer.cpp
    editor/SceneDocument.cpp
    editor/ProjectPanel.cpp
    editor/ProjectPickerPanel.cpp
    editor/HierarchyPanel.cpp
    editor/InspectorPanel.cpp
    editor/SceneViewPanel.cpp
)
add_dependencies(MiniEditor MiniEditorShaders)
target_include_directories(MiniEditor PRIVATE ${PROJECT_SOURCE_DIR} "${MINI_EDITOR_SHADER_DIR}")
# MINI_EDITOR 启用编辑器配置、最近项目和模板同步；项目配置与生命周期由两种引擎共用。
# 此目标也显式声明宏，供 tools/editor 内的代码使用。
target_link_libraries(MiniEditor PRIVATE MiniEngineEditor MiniImGui)
target_compile_definitions(MiniEditor PRIVATE MINI_EDITOR=1)
# ole32: IFileDialog folder picker. shell32: SHFileOperationW recycle-bin deletion.
target_link_libraries(MiniEditor PRIVATE ole32 shell32)
mini_enable_warnings(MiniEditor)
# RHI descriptors are filled with designated initializers that leave defaulted fields
# out; the engine disables this warning for the same reason (see src/CMakeLists.txt).
target_compile_options(MiniEditor PRIVATE -Wno-missing-field-initializers)
mini_link_mingw_runtime(MiniEditor)

set_target_properties(MiniEditor PROPERTIES FOLDER Tools)

# The game runtime mounts assets:// at <working directory>/assets, so the built-in
# content must sit next to the executable for the game to run. The editor, by contrast,
# never mounts it: the source tree's builtin/ is copied into every project's assets/ at
# creation and at open (see ProjectTemplate.cpp / Engine::openProject). CopyBuiltin.cmake
# purges the target and copies everything except *.meta sidecars: builds never ship Meta
# identities, matching what the project copy puts into assets/. The target is
# always-stale, so every build refreshes the copy next to the executables.
set(MINI_BUILTIN_SOURCE "${PROJECT_SOURCE_DIR}/builtin")
add_custom_target(MiniCopyBuiltin
    COMMAND "${CMAKE_COMMAND}"
            "-DMINI_BUILTIN_SOURCE=${MINI_BUILTIN_SOURCE}"
            "-DMINI_BUILTIN_DEST_PARENT=${CMAKE_BINARY_DIR}"
            "-P" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyBuiltin.cmake"
    COMMENT "Copying builtin content (without .meta) next to the executables"
    VERBATIM
)
set_target_properties(MiniCopyBuiltin PROPERTIES FOLDER Tools)
# MiniEditor lives in this directory; MiniVulkanEngine is created in src/, but target
# names are global, so the dependency can be declared from here after add_subdirectory.
add_dependencies(MiniEditor MiniCopyBuiltin)
add_dependencies(MiniVulkanEngine MiniCopyBuiltin)
