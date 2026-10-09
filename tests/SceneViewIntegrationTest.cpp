#include "runtime/engine/Engine.h"
#include "asset/manager/AssetManager.h"
#include "asset/types/ShaderAsset.h"
#include "asset/types/MaterialAsset.h"
#include "render/GraphicsPipelineStorage.h"
#include "runtime/window/Window.h"
#include "render/renderer/Renderer.h"
#include "render/render_target/RenderTarget.h"
#include "rhi/vulkan/VulkanFactory.h"
#include "tools/editor/backend/ImGuiLayer.h"
#include "tools/editor/model/SceneDocument.h"
#include "tools/editor/panels/SceneViewPanel.h"
#include "tools/editor/panels/StatisticsPanel.h"
#include "tools/editor/model/ProjectTemplate.h"
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
    Ref<Shader> keywordShader;

private:
    void onStart() override {
        if (!ENGINE.openProject(project_)) {
            ADD_FAILURE() << "Cannot open integration-test project";
            ENGINE.requestQuit();
            return;
        }
        attach();
        checkKeywordPrograms();
    }
    void checkKeywordPrograms() {
        const auto owner = ASSET_MANAGER.loadAsset<ShaderAsset>(
            VirtualPath{"assets://shaders/blinn_phong.shader.json"});
        ASSERT_TRUE(owner);
        keywordShader = owner->instantiate()->clone();
        MaterialAsset materialAsset;
        const auto material = materialAsset.instantiate(keywordShader);
        const auto other = materialAsset.instantiate(keywordShader);
        ASSERT_TRUE(material);
        ASSERT_TRUE(other);
        const auto& pass = keywordShader->defaultSubShader().requirePass(ShaderPassType::Forward);
        const auto& shadow =
            keywordShader->defaultSubShader().requirePass(ShaderPassType::ShadowCaster);
        EXPECT_EQ(pass.programCount(), 0U);
        const auto off = material->program(pass);
        ASSERT_TRUE(off);
        EXPECT_EQ(other->program(pass), off);
        material->enableKeyword("UNDECLARED");
        EXPECT_TRUE(material->isKeywordEnabled("UNDECLARED"));
        EXPECT_EQ(material->program(pass), off);
        EXPECT_EQ(pass.programCount(), 1U);
        material->disableKeyword("UNDECLARED");
        material->enableKeyword("RECEIVE_SHADOWS");
        const auto on = material->program(pass);
        ASSERT_TRUE(on);
        EXPECT_NE(on, off);
        EXPECT_EQ(pass.programCount(), 2U);
        EXPECT_EQ(pass.findProgram(material->variantKey(pass)), on);
        const auto shadowOn = material->program(shadow);
        ASSERT_TRUE(shadowOn);
        material->disableKeyword("RECEIVE_SHADOWS");
        EXPECT_EQ(material->program(pass), off);
        EXPECT_EQ(material->program(shadow), shadowOn);
        EXPECT_EQ(shadow.programCount(), 1U);
        EXPECT_EQ(keywordShader->clone()->defaultSubShader().passes()[0].programCount(), 0U);
        // Destroy an owner with a populated direct map before cache invalidation.
        // The GPU store must keep its program alive without retaining a Pass pointer.
        Ref<ShaderProgram> orphaned;
        {
            const auto temporary = keywordShader->clone();
            const auto temporaryMaterial = materialAsset.instantiate(temporary);
            orphaned = temporaryMaterial->program(temporary->defaultSubShader().passes()[0]);
            ASSERT_TRUE(orphaned);
        }
        GRAPHICS_PIPELINE_STORAGE.invalidateShader(*keywordShader);
        GRAPHICS_PIPELINE_STORAGE.refreshShaders(777, 0);
        EXPECT_EQ(pass.programCount(), 0U);
        EXPECT_EQ(shadow.programCount(), 0U);
        EXPECT_TRUE(orphaned->vertexShader());
        const auto refreshed = material->program(pass);
        ASSERT_TRUE(refreshed);
        EXPECT_NE(refreshed, off);
        ASSERT_TRUE(keywordShader->rebuild(*owner->buildRuntimeInfo()));
        material->setShader(keywordShader);
        const auto& rebuilt =
            keywordShader->defaultSubShader().requirePass(ShaderPassType::Forward);
        EXPECT_EQ(rebuilt.programCount(), 0U);
        ASSERT_TRUE(material->program(rebuilt));
        EXPECT_EQ(rebuilt.programCount(), 1U);
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
                        ENGINE.renderer().sceneAspectRatio(),
                        0.001F);
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
} // namespace

TEST_F(SceneViewIntegrationTest,
       VulkanSceneResizeHideEmptyAndProjectReattachHaveNoValidationErrors) {
    std::string error;
    const auto project = editor::createNewProject(scratch, "ViewportProject", error);
    ASSERT_TRUE(project.has_value()) << error;
    SceneViewSession application{*project};
    const rhi::vulkan::VulkanFactory factory;
    EXPECT_EQ(ENGINE.run(application, factory), 0);
    EXPECT_EQ(application.frames, 19);
    EXPECT_TRUE(application.drewGeometry);
    EXPECT_TRUE(application.resumed);
    ASSERT_TRUE(application.keywordShader);
    EXPECT_EQ(application.keywordShader->defaultSubShader().passes()[0].programCount(), 0U);
    EXPECT_EQ(validation.str().find("Validation Error"), std::string::npos) << validation.str();
    EXPECT_EQ(validation.str().find("VUID-"), std::string::npos) << validation.str();
}
