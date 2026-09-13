# Copy the engine's built-in content next to a built executable, shipping it under
# <bin>/assets, the game runtime's assets:// mount (Engine.cpp). The source is split into
# core/ (assets the engine hardcodes) and samples/ (demo content); both flatten into the
# same assets/ tree here, exactly as the editor flattens them into a project (see
# syncEngineContractIntoProject / seedSampleContentIntoProject). The editor does not mount
# the built-in content. Mirrors that copy: *.meta sidecars never leave the source tree, so
# built output carries no Meta identities. The destination is purged first so stale files
# (e.g. an older copy that still had metas) do not leak into a build.
#
# Usage:
#   cmake -DMINI_BUILTIN_SOURCE=<source builtin dir>
#         -DMINI_BUILTIN_DEST_PARENT=<binary dir>
#         -P CopyBuiltin.cmake

if(NOT DEFINED MINI_BUILTIN_SOURCE OR NOT DEFINED MINI_BUILTIN_DEST_PARENT)
    message(FATAL_ERROR "CopyBuiltin.cmake requires MINI_BUILTIN_SOURCE and "
                        "MINI_BUILTIN_DEST_PARENT")
endif()

set(name assets)
set(destination "${MINI_BUILTIN_DEST_PARENT}/${name}")
# Keep in sync with kBuiltinCoreDirectory / kBuiltinSamplesDirectory (ProjectTemplate.h).
set(layers core samples)

# Flattening two layers into one tree only stays well-defined while their relative paths
# are disjoint. A name in both would make the result depend on copy order, so fail loudly
# here rather than let a build silently pick a winner.
set(seenPaths "")
foreach(layer IN LISTS layers)
    set(layerRoot "${MINI_BUILTIN_SOURCE}/${layer}")
    if(NOT IS_DIRECTORY "${layerRoot}")
        message(FATAL_ERROR "Missing built-in layer directory: ${layerRoot}")
    endif()
    file(GLOB_RECURSE layerFiles RELATIVE "${layerRoot}" "${layerRoot}/*")
    foreach(layerFile IN LISTS layerFiles)
        if(layerFile MATCHES "\\.meta$")
            continue()
        endif()
        list(FIND seenPaths "${layerFile}" duplicateIndex)
        if(NOT duplicateIndex EQUAL -1)
            message(FATAL_ERROR
                "Built-in layers collide on ${layerFile}: core/ and samples/ flatten "
                "into the same assets/ tree, so the path must appear in only one layer")
        endif()
        list(APPEND seenPaths "${layerFile}")
    endforeach()
endforeach()

file(REMOVE_RECURSE "${destination}")
file(MAKE_DIRECTORY "${destination}")
foreach(layer IN LISTS layers)
    file(COPY "${MINI_BUILTIN_SOURCE}/${layer}/"
        DESTINATION "${destination}"
        PATTERN "*.meta" EXCLUDE
    )
endforeach()
list(LENGTH seenPaths copiedCount)
message(STATUS
    "Copying ${MINI_BUILTIN_SOURCE} (${layers}) -> ${destination} "
    "(${copiedCount} files, no .meta)")