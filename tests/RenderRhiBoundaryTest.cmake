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
