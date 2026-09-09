if(NOT DEFINED MINI_SOURCE_DIR)
    message(FATAL_ERROR "MINI_SOURCE_DIR is required")
endif()

set(render_dir "${MINI_SOURCE_DIR}/src/render")
if(EXISTS "${render_dir}/backend")
    file(GLOB_RECURSE legacy_backend_files
        "${render_dir}/backend/*.h"
        "${render_dir}/backend/*.cpp"
    )
    if(legacy_backend_files)
        message(FATAL_ERROR "Legacy render/backend sources still exist")
    endif()
endif()

foreach(legacy_dir IN ITEMS "${render_dir}/cache" "${render_dir}/resource")
    if(EXISTS "${legacy_dir}")
        file(GLOB_RECURSE legacy_files
            "${legacy_dir}/*.h"
            "${legacy_dir}/*.cpp"
        )
        if(legacy_files)
            message(FATAL_ERROR "Legacy render directory still contains sources: ${legacy_dir}")
        endif()
    endif()
endforeach()

foreach(legacy_renderer_file IN ITEMS RenderResources.h RenderScene.h Lighting.h)
    if(EXISTS "${render_dir}/renderer/${legacy_renderer_file}")
        message(FATAL_ERROR "Renderer directory still owns ${legacy_renderer_file}")
    endif()
endforeach()

file(READ "${render_dir}/renderer/Renderer.cpp" renderer_contents)
if(renderer_contents MATCHES "device_->(create|destroy|upload)")
    message(FATAL_ERROR "Renderer directly creates, destroys, or uploads GPU resources")
endif()
file(READ "${render_dir}/renderer/Renderer.h" renderer_header)
if(renderer_header MATCHES "(createMesh|createProceduralMesh|loadMesh|loadMaterial|loadTexture|destroyMesh|destroyMaterial|destroyTexture|setMaterial)")
    message(FATAL_ERROR "Renderer exposes CPU/GPU asset management APIs")
endif()

file(GLOB_RECURSE render_sources
    "${render_dir}/*.h"
    "${render_dir}/*.cpp"
)
foreach(source IN LISTS render_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"](vulkan/|vk_mem_alloc|rhi/vulkan)")
        message(FATAL_ERROR "Render layer depends on Vulkan: ${source}")
    endif()
endforeach()
