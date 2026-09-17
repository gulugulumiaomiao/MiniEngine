#pragma once

#include "imgui.h"

namespace engine::editor {
void applyDefaultEditorLayout(ImGuiID dockId);
bool hasDockedPanelLayout();
// Returns true when an old layout was migrated while preserving other dock slots.
bool migrateLegacyEditorLayout(ImGuiID dockId);
} // namespace engine::editor
