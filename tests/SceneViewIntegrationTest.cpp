#include "runtime/engine/Engine.h"
#include "asset/manager/AssetManager.h"
#include "asset/types/ShaderAsset.h"
#include "asset/types/MaterialAsset.h"
#include "render/GraphicsPipelineStorage.h"
#include "render/mesh/MeshBuilder.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "runtime/window/Window.h"
#include "render/renderer/Renderer.h"
#include "render/pipeline/MiniForwardPipeline.h"
#include "render/pipeline/RenderContext.h"
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
#include <stdexcept>

namespace {
using namespace engine;

struct FrameProbeState {
    int builds{}, updates{}, executions{};
    bool sawEmpty{}, sawItems{};
    bool throwOnce{};
    std::uint32_t frameSlots{};
};

class FrameProbe final : public IRenderPass {
public:
    explicit FrameProbe(FrameProbeState& state) : state_(state) {}
    void buildGraph(RenderContext&, RenderGraph& graph) override {
        ++state_.builds;
        RgRasterPass pass;
        pass.name = "FrameProbe";
        pass.renderAreaResolver = [](const RenderContext& context) {
            return rhi::Rect{0, 0, context.sceneWidth(), context.sceneHeight()};
        };
        pass.execute = [this](const RgExecuteContext& execution) {
            EXPECT_EQ(serial_, execution.frame.frameSerial());
            EXPECT_EQ(execution.resources.groups()[2].rendering[0].depthAttachments[0].loadOp,
                      rhi::LoadOp::Load); // Default Forward must retain the depth prepass.
            EXPECT_EQ(itemCount_ == 0, execution.frame.scene().objects().empty());
            state_.frameSlots |= 1U << execution.frame.frameIndex();
            state_.sawEmpty |= itemCount_ == 0;
            state_.sawItems |= itemCount_ != 0;
            ++state_.executions;
            if (state_.throwOnce) {
                state_.throwOnce = false;
                throw std::runtime_error("cancel scene recording");
            }
        };
        graph.addGraphicsPass(std::move(pass));
    }
    void updateFrame(RenderContext& context, const DrawList& list) override {
        for (const auto& item : list.items) {
            ASSERT_LT(item.sceneObjectIndex, context.scene().objects().size());
            ASSERT_LT(item.arguments.firstInstance, list.objects.size());
            EXPECT_EQ(list.objects[item.arguments.firstInstance].transform,
                      context.scene().objects()[item.sceneObjectIndex].transform);
        }
        serial_ = context.frameSerial();
        itemCount_ = list.items.size();
        ++state_.updates;
    }
private:
    FrameProbeState& state_;
    std::uint64_t serial_{};
    std::size_t itemCount_{};
};

class SceneViewSession final : public Application {
public:
    explicit SceneViewSession(std::filesystem::path project) : project_(std::move(project)) {}
    int frames{};
    bool drewGeometry{};
    bool resumed{};
    bool cancelledRecording{};
    FrameProbeState probe, addedProbe;
    std::uint64_t graphCompiles{};
    Ref<Shader> keywordShader;
    Ref<Material> survivingMaterial;
    Ref<UniformSet> survivingSet;
    Ref<Mesh> survivingMesh;
    Ref<MeshGpuBuffers> survivingMeshBuffers;

private:
    void onStart() override {
        if (!ENGINE.openProject(project_)) {
            ADD_FAILURE() << "Cannot open integration-test project";
            ENGINE.requestQuit();
            return;
        }
        attach();
        auto pipeline = std::make_unique<MiniForwardPipeline>();
        pipeline_ = pipeline.get();
        pipeline_->addPass(std::make_unique<FrameProbe>(probe));
        ENGINE.renderer().setPipeline(std::move(pipeline));
        checkKeywordPrograms();
        MeshBuildRecipe recipe;
        recipe.parts.emplace_back(PlaneGeometry{});
        recipe.keepCpuCopy = true;
        auto info = MeshBuilder::build(recipe);
        ASSERT_TRUE(info);
        survivingMesh = Mesh::create(std::move(*info));
        ASSERT_TRUE(survivingMesh);
        survivingMeshBuffers = survivingMesh->gpuBuffers();
    }
    void checkKeywordPrograms() {
        const auto owner = ASSET_MANAGER.loadAsset<ShaderAsset>(
            VirtualPath{"assets://shaders/blinn_phong.shader.json"});
        ASSERT_TRUE(owner);
        keywordShader = owner->instantiate()->clone();
        MaterialAsset materialAsset;
        const auto material = makeRef<Material>(*materialAsset.buildRuntimeInfo(keywordShader));
        const auto other = makeRef<Material>(*materialAsset.buildRuntimeInfo(keywordShader));
        ASSERT_TRUE(material);
        ASSERT_TRUE(other);
        survivingMaterial = material;
        const auto& pass = keywordShader->defaultSubShader().requirePass(ShaderPassType::Forward);
        const auto& shadow =
            keywordShader->defaultSubShader().requirePass(ShaderPassType::ShadowCaster);
        EXPECT_EQ(pass.programCount(), 0U);
        const auto off = material->program(pass);
        ASSERT_TRUE(off);
        survivingSet = material->uniformSet(*off, 0);
        ASSERT_TRUE(survivingSet);
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
            const auto temporaryMaterial =
                makeRef<Material>(*materialAsset.buildRuntimeInfo(temporary));
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
        auto booleanInfo = *owner->buildRuntimeInfo();
        booleanInfo.properties.push_back({"FeatureToggle", ShaderPropertyType::Boolean, true});
        ASSERT_TRUE(keywordShader->rebuild(std::move(booleanInfo)));
        const auto booleanProgram = material->program(
            keywordShader->defaultSubShader().requirePass(ShaderPassType::Forward));
        ASSERT_TRUE(booleanProgram);
        EXPECT_TRUE(material->getBool("FeatureToggle"));
        const auto* toggle = booleanProgram->uboLayouts(1)[0]->findUniform("FeatureToggle");
        ASSERT_TRUE(toggle);
        EXPECT_EQ(toggle->type, UboUniformType::UInt);
        ASSERT_TRUE(material->uniformSet(*booleanProgram, 0));
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
        if (frames > 1 && frames <= 5) EXPECT_EQ(pipeline_->graphCompileCount(), 1U);
        if (frames == 2) {
            probe.throwOnce = true;
            EXPECT_THROW(ENGINE.renderer().renderFrame(ENGINE.renderScene()), std::runtime_error);
            cancelledRecording = !ENGINE.renderer().rgTexturePool().batchActive();
        }
        if (frames == 5) pipeline_->addPass(std::make_unique<FrameProbe>(addedProbe));
        if (frames >= 6 && frames <= 10) EXPECT_EQ(pipeline_->graphCompileCount(), 2U);
        if (frames == 10) {
            graphCompiles = pipeline_->graphCompileCount();
            pipeline_ = nullptr; // openProject below recreates the renderer and its device.
        }
        if (frames == 4 || frames == 5) {
            // Rebuild an actual rendered asset while another frame may still use its buffers.
            const auto asset = ASSET_MANAGER.loadAsset<MeshAsset>(
                VirtualPath{"assets://meshes/procedural_showcase.mesh.json"});
            ASSERT_TRUE(asset);
            const auto mesh = asset->instantiate();
            ASSERT_TRUE(mesh);
            const auto old = mesh->gpuBuffers();
            auto info = asset->buildRuntimeInfo();
            ASSERT_TRUE(info);
            ASSERT_TRUE(mesh->rebuild(std::move(*info)));
            EXPECT_NE(mesh->gpuBuffers(), old);
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
            const auto program = survivingMaterial->program(
                keywordShader->defaultSubShader().requirePass(ShaderPassType::Forward));
            ASSERT_TRUE(program);
            const auto newSet = survivingMaterial->uniformSet(*program, 0);
            ASSERT_TRUE(newSet);
            EXPECT_NE(newSet, survivingSet);
            survivingSet = newSet;
            ASSERT_TRUE(survivingMesh->isValid());
            EXPECT_NE(survivingMesh->gpuBuffers(), survivingMeshBuffers);
            survivingMeshBuffers = survivingMesh->gpuBuffers();
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
    void onStop() override {
        layer_.detach();
    }
    MiniForwardPipeline* pipeline_{};
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
    EXPECT_TRUE(application.cancelledRecording);
    EXPECT_EQ(application.graphCompiles, 2U); // One initial compile, one pass-list change.
    EXPECT_EQ(application.probe.builds, 2);
    EXPECT_EQ(application.probe.updates, application.probe.executions);
    EXPECT_GE(application.probe.executions, 5);
    EXPECT_TRUE(application.probe.sawEmpty);
    EXPECT_TRUE(application.probe.sawItems);
    EXPECT_EQ(application.probe.frameSlots, 3U);
    EXPECT_EQ(application.addedProbe.builds, 1);
    ASSERT_TRUE(application.keywordShader);
    EXPECT_EQ(application.keywordShader->defaultSubShader().passes()[0].programCount(), 0U);
    EXPECT_EQ(validation.str().find("Validation Error"), std::string::npos) << validation.str();
    EXPECT_EQ(validation.str().find("VUID-"), std::string::npos) << validation.str();
}
