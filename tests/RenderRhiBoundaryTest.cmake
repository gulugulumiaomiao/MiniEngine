if(NOT DEFINED MINI_SOURCE_DIR)
    message(FATAL_ERROR "MINI_SOURCE_DIR is required")
endif()

set(render_dir "${MINI_SOURCE_DIR}/src/render")

# rhi/api is intentionally a three-header surface: Device.h (IDevice + ISwapchain),
# Command.h (command free functions) and ResourceDesc.h (every desc/enum/POD). Guard it so a
# new backend-agnostic header is a deliberate act rather than accidental sprawl.
file(GLOB rhi_api_headers "${MINI_SOURCE_DIR}/src/rhi/api/*.h")
list(LENGTH rhi_api_headers rhi_api_header_count)
if(NOT rhi_api_header_count EQUAL 3)
    message(FATAL_ERROR
        "rhi/api must contain exactly Device.h, Command.h and ResourceDesc.h (found ${rhi_api_header_count}): ${rhi_api_headers}")
endif()

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
# Resource lifecycle now uses the <type>_<op> device API (buffer_create, texture_destroy,
# buffer_upload, ...), so match the verb suffix rather than a leading create/destroy/upload.
if(renderer_contents MATCHES "device_->.*_(create|destroy|upload|allocate_rid|release_rid)")
    message(FATAL_ERROR "Renderer directly creates, destroys, or uploads GPU resources")
endif()
file(READ "${render_dir}/renderer/Renderer.h" renderer_header)
if(renderer_header MATCHES "(createMesh|createProceduralMesh|loadMesh|loadMaterial|loadTexture|destroyMesh|destroyMaterial|destroyTexture|setMaterial)")
    message(FATAL_ERROR "Renderer exposes CPU/GPU asset management APIs")
endif()

# NOTE: render/texture/Texture.h is a layer-2 header now: it holds rhi::RID handles and
# rhi::TextureBinding and talks to IDevice directly, so it legitimately includes rhi/api
# headers and is intentionally excluded from the layer-1 "no RHI" invariant below.
foreach(layer1_header IN ITEMS
    "${render_dir}/mesh/Mesh.h"
    "${render_dir}/material/Material.h"
    "${render_dir}/shader/Shader.h"
)
    file(READ "${layer1_header}" layer1_contents)
    if(layer1_contents MATCHES "#[ \t]*include[ \t]*[<\"]rhi/")
        message(FATAL_ERROR "Layer-1 resource header directly depends on RHI: ${layer1_header}")
    endif()
endforeach()

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
