if(NOT DEFINED MINI_SOURCE_DIR)
    message(FATAL_ERROR "MINI_SOURCE_DIR is required")
endif()
file(GLOB mesh_sources
    "${MINI_SOURCE_DIR}/src/render/mesh/*.h"
    "${MINI_SOURCE_DIR}/src/render/mesh/*.cpp")
foreach(source IN LISTS mesh_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"](asset/|core/serialization/)" OR
       contents MATCHES "(MeshAsset|AssetManager|AssetId|VirtualPath|Transferable|MeshDesc|buildAsset)")
        message(FATAL_ERROR "Runtime mesh depends on asset/serialization types: ${source}")
    endif()
endforeach()
foreach(source IN ITEMS MeshAssetData.h MeshAssetData.cpp)
    file(READ "${MINI_SOURCE_DIR}/src/asset/types/${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"](render/|rhi/)")
        message(FATAL_ERROR "Persistent mesh schema depends on runtime/GPU types: ${source}")
    endif()
endforeach()
