if(NOT DEFINED MINI_SOURCE_DIR)
    message(FATAL_ERROR "MINI_SOURCE_DIR is required")
endif()

file(GLOB shader_sources
    "${MINI_SOURCE_DIR}/src/render/shader/*.h"
    "${MINI_SOURCE_DIR}/src/render/shader/*.cpp")
foreach(source IN LISTS shader_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"](asset/|core/serialization/)" OR
       contents MATCHES "(ShaderAsset|AssetManager|AssetId|Transferable)")
        message(FATAL_ERROR "Runtime shader depends on asset/serialization types: ${source}")
    endif()
endforeach()

foreach(source IN ITEMS ShaderAssetData.h ShaderAssetData.cpp)
    file(READ "${MINI_SOURCE_DIR}/src/asset/types/${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"]render/")
        message(FATAL_ERROR "Persistent shader schema depends on runtime types: ${source}")
    endif()
endforeach()
