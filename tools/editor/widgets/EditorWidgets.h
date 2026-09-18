#pragma once

#include "asset/base/Asset.h"
#include "core/filesystem/VirtualPath.h"
#include "core/math/Math.h"
#include "imgui.h"

#include <cstdint>
#include <vector>

namespace engine::editor {

// Shared ImGui widgets and helpers for the editor panels. Everything here is a
// free function over the engine's public data so panels stay independent of
// each other and copy-pasted control patterns live in exactly one place.

// Common drag speed for editor float controls.
constexpr float kDragSpeed = 0.05F;

bool dragVec3(const char* label, math::Vec3& value);
bool dragEuler(const char* label, math::Quat& rotation);
bool inputUint(const char* label, std::uint32_t& value);

// InputText resize callback for std::vector<char> rename buffers (pass the
// vector through ImGuiInputTextCallbackData::UserData).
int resizeRenameBuffer(ImGuiInputTextCallbackData* data);

// AssetDatabase records of one type, sorted by relative path for a stable
// combo order. Only imported assets (with GUID records) are listed, i.e.
// exactly the paths a Manager::load can actually succeed on.
[[nodiscard]] std::vector<VirtualPath> collectAssets(AssetType type);

// Combo over asset paths. Returns true when an entry was picked this frame;
// chosenPath then holds the picked asset, or an invalid path when the optional
// noneLabel entry (used for clearable slots) was picked. current may be
// invalid to display noneLabel or "(none)".
bool assetCombo(const char* label,
                const std::vector<VirtualPath>& paths,
                const VirtualPath& current,
                VirtualPath& chosenPath,
                const char* noneLabel = nullptr);

// Three-state checkbox for multi-selection: a mixed anyTrue/anyFalse group
// renders the ImGui mixed-value square and the first click unifies the group
// to active; a uniform group behaves like a plain checkbox. Returns true when
// clicked; active then holds the value to apply to every item.
bool mixedCheckbox(const char* label, bool anyTrue, bool anyFalse, bool& active);

} // namespace engine::editor
