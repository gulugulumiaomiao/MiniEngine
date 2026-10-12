#include "TestRenderDevice.h"
#include "render/render_graph/RenderGraph.h"
#include "core/filesystem/FileSystem.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompilePipeline.h"
#include "render/shader/ShaderProgram.h"
#include "render/uniform/PipelineLayout.h"
#include "render/uniform/UniformSetLayout.h"
#include "render/GraphicsPipelineStorage.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshBuilder.h"
#include <filesystem>
#include <gtest/gtest.h>

namespace {
using namespace engine;
using rhi::ResourceState;
using rhi::LoadOp;
using rhi::StoreOp;

RgTextureHandle color(RenderGraph& graph, rhi::TextureUsage extra = rhi::TextureUsage::None) {
    return graph.createTexture({.format = rhi::PixelFormat::Rgba8Unorm, .width = 16, .height = 16,
        .usage = rhi::TextureUsage::ColorAttachment | extra, .debugName = "graph-test"});
}
RgRasterPass writer(const char* name, RgTextureHandle handle,
                    LoadOp load = LoadOp::Clear, StoreOp store = StoreOp::Store) {
    RgRasterPass pass;
    pass.name = name;
    pass.rendering.renderArea = {0, 0, 16, 16};
    pass.rendering.colorAttachments.push_back({handle, load, store, math::Vec4{0.25F}});
    pass.execute = [](const RgExecuteContext&) {};
    return pass;
}
void independent(RenderGraph& graph, RgRasterPass pass) {
    graph.addGraphicsPass(pass.name, std::move(pass.rendering), std::move(pass.resources), [](rhi::RID) {});
}

TEST(RenderGraphTest, IndependentPassesKeepDynamicRenderingAndAutomaticUsages) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto gbuffer = color(graph, rhi::TextureUsage::Sampled);
    const auto output = color(graph);
    independent(graph, writer("GBuffer", gbuffer));
    auto lighting = writer("Lighting", output);
    lighting.resources.push_back({gbuffer, rhi::TextureAspect::Color, ResourceState::ShaderRead});
    independent(graph, std::move(lighting));
    graph.compile(pool, cache);
    ASSERT_EQ(graph.compiledGroups().size(), 2U);
    EXPECT_FALSE(graph.compiledGroups()[0].traditional);
    EXPECT_FALSE(graph.compiledGroups()[1].traditional);
    EXPECT_TRUE(device.renderPasses.empty());
    ASSERT_EQ(graph.compiledGroups()[0].subpasses[0].resources.size(), 1U);
    const auto& barriers = graph.compiledGroups()[1].preBarriers;
    ASSERT_EQ(barriers.size(), 2U);
    EXPECT_EQ(barriers[0].before, ResourceState::ColorAttachment);
    EXPECT_EQ(barriers[0].after, ResourceState::ShaderRead);
    EXPECT_TRUE(graph.finalBarriers().empty());
}

TEST(RenderGraphTest, OneGroupProducesTwoSubpassesAndInputDependency) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto gbuffer = color(graph, rhi::TextureUsage::InputAttachment);
    const auto output = color(graph);
    auto lighting = writer("Lighting", output);
    lighting.rendering.inputAttachments = {gbuffer};
    graph.addRasterGroup("Deferred", {writer("GBuffer", gbuffer, LoadOp::Clear, StoreOp::DontCare), lighting});
    graph.compile(pool, cache);
    ASSERT_EQ(graph.compiledGroups().size(), 1U);
    EXPECT_EQ(graph.passCount(), 2U);
    const auto& group = graph.compiledGroups()[0];
    ASSERT_TRUE(group.traditional);
    EXPECT_TRUE(group.beginInfo.renderPass);
    EXPECT_TRUE(group.beginInfo.framebuffer);
    ASSERT_EQ(group.renderPassDesc.attachments.size(), 2U);
    EXPECT_EQ(group.renderPassDesc.attachments[0].storeOp, StoreOp::DontCare);
    EXPECT_EQ(group.renderPassDesc.attachments[0].finalState, ResourceState::InputAttachment);
    ASSERT_EQ(group.renderPassDesc.subpasses.size(), 2U);
    EXPECT_EQ(group.renderPassDesc.subpasses[0].colors[0].attachment, 0U);
    EXPECT_EQ(group.renderPassDesc.subpasses[1].colors[0].attachment, 1U);
    EXPECT_EQ(group.renderPassDesc.subpasses[1].inputs[0].attachment, 0U);
    ASSERT_EQ(group.renderPassDesc.dependencies.size(), 1U);
    const auto& dependency = group.renderPassDesc.dependencies[0];
    EXPECT_EQ(dependency.source, 0U);
    EXPECT_EQ(dependency.destination, 1U);
    EXPECT_EQ(dependency.before, ResourceState::ColorAttachment);
    EXPECT_EQ(dependency.after, ResourceState::InputAttachment);
    EXPECT_TRUE(dependency.byRegion);
    EXPECT_EQ(group.preBarriers.size(), 2U);
}

TEST(RenderGraphTest, PreservesAttachmentAcrossUnusedSubpass) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto gbuffer = color(graph, rhi::TextureUsage::InputAttachment);
    const auto temporary = color(graph);
    const auto output = color(graph);
    auto lighting = writer("Lighting", output);
    lighting.rendering.inputAttachments = {gbuffer};
    graph.addRasterGroup("Deferred", {writer("GBuffer", gbuffer), writer("Middle", temporary), lighting});
    graph.compile(pool, cache);
    const auto& desc = graph.compiledGroups()[0].renderPassDesc;
    EXPECT_EQ(desc.subpasses[1].preserves, std::vector<std::uint32_t>{0});
    ASSERT_EQ(desc.dependencies.size(), 1U);
    EXPECT_EQ(desc.dependencies[0].destination, 2U);
}

TEST(RenderGraphTest, MixedGroupsUpgradeStoreAndSynchronizeImplicitStore) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto gbuffer = color(graph, rhi::TextureUsage::InputAttachment | rhi::TextureUsage::Sampled);
    const auto lit = color(graph, rhi::TextureUsage::Sampled);
    const auto output = color(graph);
    auto lighting = writer("Lighting", lit);
    lighting.rendering.inputAttachments = {gbuffer};
    graph.addRasterGroup("Deferred", {writer("GBuffer", gbuffer, LoadOp::Clear, StoreOp::DontCare), lighting});
    auto post = writer("Post", output);
    post.resources.push_back({gbuffer, rhi::TextureAspect::Color, ResourceState::ShaderRead});
    independent(graph, post);
    graph.compile(pool, cache);
    EXPECT_EQ(graph.compiledGroups()[0].renderPassDesc.attachments[0].storeOp, StoreOp::Store);
    const auto& barriers = graph.compiledGroups()[1].preBarriers;
    ASSERT_FALSE(barriers.empty());
    EXPECT_EQ(barriers[0].before, ResourceState::InputAttachment);
    EXPECT_EQ(barriers[0].after, ResourceState::ShaderRead);
    EXPECT_EQ(barriers[0].sourceAccess, rhi::AccessMode::ReadWrite);
}

TEST(RenderGraphTest, SameStateWriteHazardStillGeneratesBarrier) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto output = color(graph);
    independent(graph, writer("First", output));
    independent(graph, writer("Second", output, LoadOp::Load));
    graph.compile(pool, cache);
    const auto& barriers = graph.compiledGroups()[1].preBarriers;
    ASSERT_EQ(barriers.size(), 1U);
    EXPECT_EQ(barriers[0].before, ResourceState::ColorAttachment);
    EXPECT_EQ(barriers[0].after, ResourceState::ColorAttachment);
}

TEST(RenderGraphTest, ReadBeforeWriteHazardIsTracked) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto texture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
        .debugName = "graph-test"});
    const auto source = graph.importTexture({texture, device.texture_default_view(texture), ResourceState::ShaderRead});
    auto reader = writer("Read", color(graph));
    reader.resources.push_back({source, rhi::TextureAspect::Color, ResourceState::ShaderRead});
    independent(graph, reader);
    independent(graph, writer("Write", source));
    graph.compile(pool, cache);
    const auto& barrier = graph.compiledGroups()[1].preBarriers[0];
    EXPECT_EQ(barrier.before, ResourceState::ShaderRead);
    EXPECT_EQ(barrier.after, ResourceState::ColorAttachment);
    EXPECT_EQ(barrier.sourceAccess, rhi::AccessMode::Read);
}

TEST(RenderGraphTest, RepeatedImportsShareStateAndResetClearsRecording) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto texture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment, .debugName = "graph-test"});
    RenderGraph::ImportedTexture imported{texture, device.texture_default_view(texture)};
    const auto first = graph.importTexture(imported);
    EXPECT_EQ(first, graph.importTexture(imported));
    EXPECT_EQ(graph.textureCount(), 1U);
    independent(graph, writer("First", first));
    graph.compile(pool, cache);
    EXPECT_TRUE(graph.finalBarriers().empty()); // Undefined is never a destination layout.
    graph.reset();
    EXPECT_FALSE(graph.compiled());
    EXPECT_EQ(graph.passCount(), 0U);
    EXPECT_EQ(graph.textureCount(), 0U);
    graph.addRasterGroup("New", {writer("New", color(graph))});
    graph.compile(pool, cache);
    EXPECT_EQ(graph.passCount(), 1U);
    EXPECT_TRUE(graph.compiledGroups()[0].traditional);
    graph.reset();
    EXPECT_EQ(pool.inUseCount(), 0U);
}

TEST(RenderGraphTest, CacheOutlivesFrameLocalGraphsAndDestroysFramebuffersFirst) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    rhi::RID renderPass, framebuffer;
    for (int frame = 0; frame < 2; ++frame) {
        RenderGraph graph;
        graph.addRasterGroup("Frame", {writer("Color", color(graph))});
        graph.compile(pool, cache);
        const auto& begin = graph.compiledGroups()[0].beginInfo;
        if (frame == 0) {
            renderPass = begin.renderPass;
            framebuffer = begin.framebuffer;
        } else {
            EXPECT_EQ(begin.renderPass, renderPass);
            EXPECT_EQ(begin.framebuffer, framebuffer);
        }
    }
    EXPECT_EQ(cache.renderPassCount(), 1U);
    EXPECT_EQ(cache.framebufferCount(), 1U);
    EXPECT_EQ(device.renderPasses.size(), 1U);
    EXPECT_EQ(device.framebuffers.size(), 1U);
    EXPECT_TRUE(device.destroyedRenderPasses.empty());
    EXPECT_TRUE(device.destroyedFramebuffers.empty());
    cache.clearFramebuffers();
    EXPECT_EQ(cache.framebufferCount(), 0U);
    EXPECT_EQ(cache.renderPassCount(), 1U);
    EXPECT_EQ(device.destroyedFramebuffers, std::vector<rhi::RID>{framebuffer});
    EXPECT_TRUE(device.destroyedRenderPasses.empty());
    {
        RenderGraph graph;
        graph.addRasterGroup("Next", {writer("Color", color(graph))});
        graph.compile(pool, cache);
        EXPECT_EQ(graph.compiledGroups()[0].beginInfo.renderPass, renderPass);
        EXPECT_NE(graph.compiledGroups()[0].beginInfo.framebuffer, framebuffer);
    }
    cache.clear();
    EXPECT_EQ(device.renderObjectDestructionOrder,
              (std::vector<std::string>{"Framebuffer", "Framebuffer", "RenderPass"}));
}

rhi::RenderPassDesc cachePassDesc() {
    rhi::RenderPassDesc desc;
    desc.attachments.push_back({rhi::PixelFormat::Rgba8Unorm, rhi::SampleCount::One,
        rhi::LoadOp::Clear, rhi::StoreOp::Store, ResourceState::ColorAttachment,
        ResourceState::ColorAttachment});
    rhi::SubpassDesc subpass;
    subpass.colors.push_back({0, ResourceState::ColorAttachment});
    desc.subpasses.push_back(std::move(subpass));
    return desc;
}

TEST(RenderGraphTest, FramebufferCacheKeysViewsGenerationsExtentAndRenderPass) {
    MockDevice device;
    RgRenderPassCache cache;
    auto desc = cachePassDesc();
    const auto renderPass = cache.getOrCreateRenderPass(desc);
    EXPECT_EQ(cache.getOrCreateRenderPass(desc), renderPass);
    const rhi::RID originalView{10, 1};
    rhi::FramebufferDesc fb{renderPass, {originalView}, 16, 16, 1};
    const auto original = cache.getOrCreateFramebuffer(fb);
    EXPECT_EQ(cache.getOrCreateFramebuffer(fb), original);
    fb.views[0] = {10, 2};
    const auto newGeneration = cache.getOrCreateFramebuffer(fb);
    EXPECT_NE(original, newGeneration);
    fb.views[0] = originalView;
    fb.width = 32;
    EXPECT_NE(cache.getOrCreateFramebuffer(fb), original);
    desc.attachments[0].loadOp = rhi::LoadOp::Load;
    fb.renderPass = cache.getOrCreateRenderPass(desc);
    fb.width = 16;
    EXPECT_NE(cache.getOrCreateFramebuffer(fb), original);
    EXPECT_EQ(cache.framebufferCount(), 4U);
    EXPECT_EQ(cache.renderPassCount(), 2U);
    cache.invalidateFramebuffers(originalView);
    EXPECT_EQ(cache.framebufferCount(), 1U);
    EXPECT_EQ(cache.renderPassCount(), 2U);
    EXPECT_EQ(device.destroyedFramebuffers.size(), 3U);
    EXPECT_EQ(device.destroyedRenderPasses.size(), 0U);
    fb = {renderPass, {{10, 2}}, 16, 16, 1};
    EXPECT_EQ(cache.getOrCreateFramebuffer(fb), newGeneration);
}

TEST(RenderGraphTest, OldCacheCannotDestroyReusedRidsInAnotherDevice) {
    RgRenderPassCache cache;
    const auto desc = cachePassDesc();
    {
        MockDevice first;
        const auto pass = cache.getOrCreateRenderPass(desc);
        (void)cache.getOrCreateFramebuffer({pass, {{10, 1}}, 16, 16, 1});
    }
    MockDevice second;
    const auto rawPass = second.render_pass_create(desc);
    const auto rawFramebuffer = second.framebuffer_create({rawPass, {{10, 1}}, 16, 16, 1});
    cache.clear();
    EXPECT_TRUE(second.destroyedRenderPasses.empty());
    EXPECT_TRUE(second.destroyedFramebuffers.empty());
    const auto newPass = cache.getOrCreateRenderPass(desc);
    const auto newFramebuffer = cache.getOrCreateFramebuffer({newPass, {{10, 1}}, 16, 16, 1});
    EXPECT_NE(rawPass, newPass);
    EXPECT_NE(rawFramebuffer, newFramebuffer);
    cache.clear();
    EXPECT_EQ(second.destroyedRenderPasses, std::vector<rhi::RID>{newPass});
    EXPECT_EQ(second.destroyedFramebuffers, std::vector<rhi::RID>{newFramebuffer});
}

TEST(RenderGraphTest, InputAttachmentReflectionBuildsCorrectDescriptorAndSubpassPipelines) {
    MockDevice device;
    std::filesystem::create_directories(MINI_TEST_RG_CACHE_DIR);
    ASSERT_TRUE(FILE_SYSTEM.mountDirectory("rg-fixture", MINI_TEST_RG_ASSET_DIR, true));
    ASSERT_TRUE(FILE_SYSTEM.mountDirectory("rg-cache", MINI_TEST_RG_CACHE_DIR, false));
    ShaderPassCreateInfo pass;
    pass.name = "InputRead";
    pass.program = {VirtualPath{"rg-fixture://render_graph_reflection_vertex.glsl"},
                    VirtualPath{"rg-fixture://render_graph_reflection_fragment.glsl"}};
    pass.fragmentOutputs.push_back({.name = "color", .semantic = "COLOR",
                                    .type = ShaderValueType::Vec4, .location = 0});
    ShaderCreateInfo desc;
    desc.name = "RenderGraphInput";
    desc.subShaders.push_back({"MiniForward", 2000, {pass}});
    Shader shader{std::move(desc)};
    ShaderCompilePipelineConfig config;
    config.intermediateRoot = VirtualPath{"rg-cache://"};
    ShaderCompilePipeline compiler{config};
    const auto handle = compiler.getOrCreate(shader, shader.defaultSubShader().passes()[0]);
    ASSERT_TRUE(handle);
    const auto& compiled = compiler.resolveProgram(handle);
    const auto& fragment = compiler.resolveCompiled(compiled.fragment);
    const auto binding = std::ranges::find(fragment.reflection.descriptors,
        ShaderDescriptorType::InputAttachment, &ShaderDescriptorBinding::type);
    ASSERT_NE(binding, fragment.reflection.descriptors.end());
    EXPECT_EQ(binding->set, 3U);
    EXPECT_EQ(binding->binding, 0U);
    const auto program = ShaderProgram::create(compiler.resolveCompiled(compiled.vertex), fragment, compiled.id);
    ASSERT_TRUE(program);
    ASSERT_EQ(program->setCount(), 4U);
    EXPECT_EQ(program->setLayout(3)->bindings()[0].type, rhi::ResourceType::InputAttachment);
    ASSERT_TRUE(GRAPHICS_PIPELINE_STORAGE.initialize());
    MeshBuildRecipe recipe;
    recipe.name = "GraphTestMesh";
    recipe.parts.emplace_back(PlaneGeometry{{1.0F, 1.0F}, 1, 1});
    recipe.vertexLayout = PrimitiveVertexLayout::Position;
    const auto meshInfo = MeshBuilder::build(recipe);
    ASSERT_TRUE(meshInfo);
    const auto mesh = Mesh::create(*meshInfo);
    ASSERT_TRUE(mesh);
    const rhi::PixelFormat formats[]{rhi::PixelFormat::Rgba8Unorm};
    const auto first = GRAPHICS_PIPELINE_STORAGE.resolve(*program, *mesh, formats,
        rhi::PixelFormat::Undefined, {1, 1}, 0);
    const auto same = GRAPHICS_PIPELINE_STORAGE.resolve(*program, *mesh, formats,
        rhi::PixelFormat::Undefined, {1, 1}, 0);
    const auto second = GRAPHICS_PIPELINE_STORAGE.resolve(*program, *mesh, formats,
        rhi::PixelFormat::Undefined, {1, 1}, 1);
    const auto otherPass = GRAPHICS_PIPELINE_STORAGE.resolve(*program, *mesh, formats,
        rhi::PixelFormat::Undefined, {2, 1}, 0);
    EXPECT_TRUE(first);
    EXPECT_EQ(first, same);
    EXPECT_NE(first, second);
    EXPECT_NE(first, otherPass);
    ASSERT_EQ(device.pipelines.size(), 3U);
    EXPECT_EQ(device.pipelines[1].subpass, 1U);
    EXPECT_EQ(device.pipelines[2].renderPass, (rhi::RID{2, 1}));
    GRAPHICS_PIPELINE_STORAGE.shutdown();
    (void)FILE_SYSTEM.unmount("rg-fixture");
    (void)FILE_SYSTEM.unmount("rg-cache");
}

TEST(RenderGraphDeathTest, RejectsSamplingAttachmentInsideGroup) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto source = color(graph, rhi::TextureUsage::Sampled);
        auto lighting = writer("Lighting", color(graph));
        lighting.resources.push_back({source, rhi::TextureAspect::Color, ResourceState::ShaderRead});
        graph.addRasterGroup("Invalid", {writer("GBuffer", source), lighting});
        graph.compile(pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Sampling a group attachment");
}

TEST(RenderGraphDeathTest, RasterGroupsRequirePersistentCache) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RenderGraph graph;
        graph.addRasterGroup("NoCache", {writer("Color", color(graph))});
        graph.compile(pool);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "require a persistent RgRenderPassCache");
}

TEST(RenderGraphDeathTest, RejectsRepeatedClearAndMismatchedAreas) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto output = color(graph);
        graph.addRasterGroup("Invalid", {writer("First", output), writer("Second", output)});
        graph.compile(pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Repeated attachment must Load");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        auto second = writer("Second", color(graph)); second.rendering.renderArea.width = 8;
        graph.addRasterGroup("Invalid", {writer("First", color(graph)), second});
        graph.compile(pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "render areas must match");
}

TEST(RenderGraphDeathTest, RejectsUndefinedInputAndMissingUsage) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        auto lighting = writer("Lighting", color(graph));
        lighting.rendering.inputAttachments = {color(graph, rhi::TextureUsage::InputAttachment)};
        graph.addRasterGroup("Invalid", {lighting}); graph.compile(pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "reads undefined texture contents");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto source = color(graph);
        auto lighting = writer("Lighting", color(graph)); lighting.rendering.inputAttachments = {source};
        graph.addRasterGroup("Invalid", {writer("First", source), lighting}); graph.compile(pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "attachment view/usage/extent");
}

TEST(RenderGraphDeathTest, RejectsFeedbackAndModificationAfterCompile) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto source = color(graph, rhi::TextureUsage::InputAttachment);
        auto pass = writer("Invalid", source); pass.rendering.inputAttachments = {source};
        graph.addRasterGroup("Invalid", {pass}); graph.compile(pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Duplicate attachment binding");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        independent(graph, writer("First", color(graph))); graph.compile(pool, cache);
        (void)color(graph);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Cannot modify a compiled graph");
}
} // namespace
