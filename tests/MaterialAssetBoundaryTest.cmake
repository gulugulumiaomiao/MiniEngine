if(NOT DEFINED MINI_SOURCE_DIR)
    message(FATAL_ERROR "MINI_SOURCE_DIR is required")
endif()
file(GLOB material_sources
    "${MINI_SOURCE_DIR}/src/render/material/*.h"
    "${MINI_SOURCE_DIR}/src/render/material/*.cpp")
foreach(source IN LISTS material_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"](asset/|core/serialization/)" OR
       contents MATCHES "(MaterialAsset|AssetManager|AssetId|VirtualPath|Transferable|MaterialManager|MaterialStorage)")
        message(FATAL_ERROR "Runtime material depends on asset/serialization types: ${source}")
    endif()
endforeach()
foreach(source IN ITEMS MaterialAssetData.h MaterialAssetData.cpp)
    file(READ "${MINI_SOURCE_DIR}/src/asset/types/${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"]render/")
        message(FATAL_ERROR "Persistent material schema depends on runtime types: ${source}")
    endif()
endforeach()
if(EXISTS "${MINI_SOURCE_DIR}/src/render/material/MaterialManager.h" OR
   EXISTS "${MINI_SOURCE_DIR}/src/render/gpu/material")
    message(FATAL_ERROR "Removed material manager/GPU storage has returned")
endif()
