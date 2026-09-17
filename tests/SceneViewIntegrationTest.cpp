#include "runtime/engine/Engine.h"
#include "runtime/window/Window.h"
#include "render/renderer/Renderer.h"
#include "render/render_target/RenderTarget.h"
#include "rhi/vulkan/VulkanFactory.h"
#include "tools/editor/ImGuiLayer.h"
#include "tools/editor/SceneDocument.h"
#include "tools/editor/SceneViewPanel.h"
#include "tools/editor/StatisticsPanel.h"
#include "tools/editor/ProjectTemplate.h"
#include <gtest/gtest.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace {
using namespace engine;

class SceneViewSession final : public Application {
public:
    explicit SceneViewSession(std::filesystem::path project) : project_(std::move(project)) {}
    int frames{};
    bool drewGeometry{};
    bool resumed{};
private:
    void onStart() override {
        if (!ENGINE.openProject(project_)) {
            ADD_FAILURE() << "Cannot open integration-test project";
            ENGINE.requestQuit();
            return;
        }
        attach();
    }
    void attach() {
        ShowWindow(ENGINE.window().nativeHandle(), SW_HIDE);
        layer_.attach(ENGINE.renderer(), ENGINE.window());
        ImGui::GetIO().IniFilename = nullptr;
        EXPECT_TRUE(document_.open(ENGINE.activeScenePath()));
    }
    void onUpdate(float) override {
        if (++frames > 18) {
            ENGINE.requestQuit();
            return;
        }
        if (frames == 3 || frames == 5) {
            const auto& camera = ENGINE.renderScene().camera();
            ASSERT_TRUE(camera.has_value());
            EXPECT_NEAR(std::abs(camera->projection[1][1] / camera->projection[0][0]),
                        ENGINE.renderer().sceneAspectRatio(), 0.001F);
            EXPECT_FALSE(ENGINE.renderScene().objects().empty());
            drewGeometry = true;
        }
        if (frames == 8)
            document_.createEmpty();
        if (frames == 9)
            EXPECT_FALSE(ENGINE.renderScene().camera().has_value());
        if (frames == 10 || frames == 15) {
            layer_.detach();
            ASSERT_TRUE(ENGINE.openProject(project_));
            attach();
        }
        if (frames == 13) {
            layer_.detach();
            ENGINE.closeProject();
            layer_.attach(ENGINE.renderer(), ENGINE.window());
            ShowWindow(ENGINE.window().nativeHandle(), SW_HIDE);
            ImGui::GetIO().IniFilename = nullptr;
            document_ = {};
        }
        layer_.beginFrame();
        const bool drawScene = frames != 6 && frames != 7 && ENGINE.isProjectOpen();
        if (drawScene) {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(frames < 4 ? ImVec2{320, 180} : ImVec2{640, 360});
            view_.draw();
            EXPECT_GT(ENGINE.renderer().sceneWidth(), 0U);
            EXPECT_GT(ENGINE.renderer().sceneHeight(), 0U);
            if (frames == 16)
                resumed = true;
        } else {
            EXPECT_EQ(ENGINE.renderer().sceneWidth(), 0U);
            EXPECT_EQ(ENGINE.renderer().sceneHeight(), 0U);
        }
        ImGui::SetNextWindowPos({650, 0});
        ImGui::SetNextWindowSize({300, 300});
        stats_.draw();
        layer_.endFrame();
    }
    void onStop() override { layer_.detach(); }
    std::filesystem::path project_;
    editor::ImGuiLayer layer_;
    editor::SceneDocument document_;
    editor::SceneViewPanel view_{document_};
    editor::StatisticsPanel stats_{document_};
};

class SceneViewIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        originalDirectory = std::filesystem::current_path();
        scratch = originalDirectory / ("scene-view-gtest-" + std::to_string(GetCurrentProcessId()));
        ASSERT_TRUE(std::filesystem::create_directory(scratch));
        ownsScratch = true;
        std::string error;
        EngineConfig config;
        config.window = WindowConfig{1000, 700, false};
        ASSERT_TRUE(config.save(scratch / "engine.json", error)) << error;
        std::filesystem::current_path(scratch);
        // Capture the actual validation-layer diagnostics for a gtest assertion.
        oldCerr = std::cerr.rdbuf(validation.rdbuf());
    }
    void TearDown() override {
        if (oldCerr)
            std::cerr.rdbuf(oldCerr);
        std::filesystem::current_path(originalDirectory);
        std::error_code error;
        if (ownsScratch)
            std::filesystem::remove_all(scratch, error);
    }
    std::filesystem::path originalDirectory, scratch;
    std::ostringstream validation;
    std::streambuf* oldCerr{};
    bool ownsScratch{};
};
}

TEST_F(SceneViewIntegrationTest, VulkanSceneResizeHideEmptyAndProjectReattachHaveNoValidationErrors) {
    std::string error;
    const auto project = editor::createNewProject(scratch, "ViewportProject", error);
    ASSERT_TRUE(project.has_value()) << error;
    SceneViewSession application{*project};
    const rhi::vulkan::VulkanFactory factory;
    EXPECT_EQ(ENGINE.run(application, factory), 0);
    EXPECT_EQ(application.frames, 19);
    EXPECT_TRUE(application.drewGeometry);
    EXPECT_TRUE(application.resumed);
    EXPECT_EQ(validation.str().find("Validation Error"), std::string::npos) << validation.str();
    EXPECT_EQ(validation.str().find("VUID-"), std::string::npos) << validation.str();
}
