#pragma once

#if defined(MINI_EDITOR)

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace engine::editor {

// Ownership layers inside MINI_SOURCE_BUILTIN_DIR. Kept here so the copy functions and
// the tests that verify them agree on the layout.
inline constexpr std::string_view kBuiltinCoreDirectory = "core";
inline constexpr std::string_view kBuiltinSamplesDirectory = "samples";

// The engine's built-in content (MINI_SOURCE_BUILTIN_DIR) is split by ownership. Both
// subtrees flatten into the same projectRoot/assets tree, so an asset keeps the same
// assets:// path whichever layer it came from. Companion *.meta sidecars are never
// copied: they carry source identities and the project's import pipeline generates its
// own Meta for each asset on first import. The engine does NOT mount the built-in
// content as a virtual scheme; every project ships a working copy in its own assets/.

// Copies builtin/core -- the assets the engine hardcodes and cannot run without (the
// Error Material, the fallback Shader and the GLSL includes they pull in). Overwrites
// unconditionally, so a project that lost or broke them is repaired on the next open.
// These are engine-owned: local edits to them are not preserved by design.
[[nodiscard]] bool syncEngineContractIntoProject(const std::filesystem::path& projectRoot,
                                                 std::string& error);

// Copies builtin/samples -- the demo materials, shaders, meshes, scenes and textures a
// new project starts from. Existing files are left untouched, so this only ever fills
// gaps. Call it once at creation: the content belongs to the project afterwards and the
// user is free to edit, rename or delete any of it.
[[nodiscard]] bool seedSampleContentIntoProject(const std::filesystem::path& projectRoot,
                                                std::string& error);

// Creates a new project directory at parentDirectory/name with the standard layout
// (assets/, library/, generated-shaders/), a project.json and both built-in layers
// copied into assets/, so the project reads and ships independently of the engine (no
// builtin:// mount). Does NOT create a scene under assets://scenes; the sample content
// carries the default and demo scenes. Returns the project root on success.
[[nodiscard]] std::optional<std::filesystem::path>
createNewProject(const std::filesystem::path& parentDirectory, const std::string& name,
                 std::string& error);

} // namespace engine::editor

#endif // MINI_EDITOR
