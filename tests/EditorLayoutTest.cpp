#include "tools/editor/EditorLayout.h"
#include "imgui_internal.h"
#include <gtest/gtest.h>
#include <string>

namespace {
class EditorLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::GetIO().DisplaySize = {1600, 900};
        unsigned char* pixels;
        int width, height;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
    }
    void TearDown() override { ImGui::EndFrame(); ImGui::DestroyContext(); }
    ImGuiID dockId{1234};
    static ImGuiID dock(const char* name) {
        const auto* settings = ImGui::FindWindowSettingsByID(ImHashStr(name));
        return settings ? settings->DockId : 0;
    }
};
}

TEST_F(EditorLayoutTest, DefaultDocksSceneInCenterAndStatisticsWithProject) {
    EXPECT_FALSE(engine::editor::hasDockedPanelLayout());
    engine::editor::applyDefaultEditorLayout(dockId);
    const auto* center = ImGui::DockBuilderGetCentralNode(dockId);
    ASSERT_NE(center, nullptr);
    EXPECT_EQ(dock("Scene View"), center->ID);
    EXPECT_EQ(dock("Statistics"), dock("Project"));
    EXPECT_NE(dock("Statistics"), center->ID);
    EXPECT_TRUE(engine::editor::hasDockedPanelLayout());
}

TEST_F(EditorLayoutTest, LegacyLayoutMigratesStatisticsWithoutMovingOtherPanels) {
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, {1600, 900});
    ImGuiID bottom, center;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down, 0.24F, &bottom, &center);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Scene View", bottom);
    ImGui::DockBuilderDockWindow("Hierarchy", center);
    ImGui::DockBuilderFinish(dockId);
    const auto originalHierarchy = dock("Hierarchy");
    ASSERT_TRUE(engine::editor::migrateLegacyEditorLayout(dockId));
    EXPECT_EQ(dock("Statistics"), bottom);
    EXPECT_EQ(dock("Project"), bottom);
    EXPECT_EQ(dock("Hierarchy"), originalHierarchy);
    EXPECT_EQ(dock("Scene View"), center);
    EXPECT_FALSE(engine::editor::migrateLegacyEditorLayout(dockId));
}

TEST_F(EditorLayoutTest, NewCustomLayoutIsPreservedAndResetRestoresDefault) {
    engine::editor::applyDefaultEditorLayout(dockId);
    const auto inspector = dock("Inspector");
    ImGui::DockBuilderDockWindow("Scene View", inspector);
    ImGui::DockBuilderFinish(dockId);
    EXPECT_FALSE(engine::editor::migrateLegacyEditorLayout(dockId));
    EXPECT_EQ(dock("Scene View"), inspector);
    engine::editor::applyDefaultEditorLayout(dockId);
    EXPECT_EQ(dock("Scene View"), ImGui::DockBuilderGetCentralNode(dockId)->ID);
}

TEST_F(EditorLayoutTest, LegacyIniRestoredInNewContextCanBeMigrated) {
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, {1600, 900});
    ImGuiID bottom, center;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down, 0.24F, &bottom, &center);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Scene View", bottom);
    ImGui::DockBuilderFinish(dockId);
    const std::string ini = ImGui::SaveIniSettingsToMemory();
    ImGui::EndFrame();
    ImGui::DestroyContext();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().DisplaySize = {1600, 900};
    unsigned char* pixels;
    int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
    ImGui::NewFrame();
    ASSERT_TRUE(engine::editor::migrateLegacyEditorLayout(dockId)) << ini;
    EXPECT_EQ(dock("Statistics"), bottom);
    EXPECT_EQ(dock("Project"), bottom);
    EXPECT_EQ(dock("Scene View"), center);
}
