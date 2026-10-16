#include "TestTextureDesc.h"
#include "TestRenderDevice.h"
#include "render/render_graph/RenderGraph.h"
#include "render/pipeline/RenderContext.h"
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

RgTextureHandle color(RenderGraph& graph) {
    return graph.createTexture(test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16, "graph-test"));
}
RgRasterPass writer(const char* name, RgTextureHandle handle,
                    LoadOp load = LoadOp::Clear, StoreOp store = StoreOp::Store) {
    RgRasterPass pass;
    pass.name = name;
    pass.renderArea = {0, 0, 16, 16};
    pass.colorAttachments.push_back({handle, load, store, math::Vec4{0.25F}});
    pass.execute = [](const RgExecuteContext&) {};
    return pass;
}
void independent(RenderGraph& graph, RgRasterPass pass) {
    pass.execute = [](const RgExecuteContext&) {};
    graph.addGraphicsPass(std::move(pass));
}
RgFrameResources compileGraph(RenderGraph& graph, RgTexturePool& pool, RgRenderPassCache& cache) {
    RenderScene scene;
    RenderContext context(pool, cache, scene);
    graph.compile(context);
    return graph.prepareFrame(context); // Neither plan nor frame retains the local context.
}

TEST(RenderGraphTest, IndependentPassesKeepDynamicRenderingAndAutomaticUsages) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto gbuffer = color(graph);
    const auto output = color(graph);
    independent(graph, writer("GBuffer", gbuffer));
    auto lighting = writer("Lighting", output);
    lighting.sampledTextures = {gbuffer, gbuffer};
    independent(graph, std::move(lighting));
    auto resources = compileGraph(graph, pool, cache);
    ASSERT_EQ(graph.compiledGroups().size(), 2U);
    EXPECT_FALSE(graph.compiledGroups()[0].traditional);
    EXPECT_FALSE(graph.compiledGroups()[1].traditional);
    EXPECT_TRUE(device.renderPasses.empty());
    ASSERT_EQ(graph.compiledGroups()[0].subpasses[0].resources.size(), 1U);
    const auto& accesses = graph.compiledGroups()[1].subpasses[0].resources;
    ASSERT_EQ(accesses.size(), 2U); // Repeated sampled handles describe one read.
    EXPECT_EQ(accesses[0].texture, gbuffer);
    EXPECT_EQ(accesses[0].aspect, rhi::TextureAspect::Color);
    EXPECT_EQ(accesses[0].state, ResourceState::ShaderRead);
    EXPECT_EQ(accesses[0].access, rhi::AccessMode::Read);
    const auto& barriers = resources.groups()[1].preBarriers;
    ASSERT_EQ(barriers.size(), 2U);
    EXPECT_EQ(barriers[0].before, ResourceState::ColorAttachment);
    EXPECT_EQ(barriers[0].after, ResourceState::ShaderRead);
    EXPECT_TRUE(resources.finalBarriers().empty());
    EXPECT_EQ(device.texture_view_info(resources.textureView(gbuffer)).usage,
        rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    EXPECT_EQ(device.texture_view_info(resources.textureView(output)).usage,
        rhi::TextureUsage::ColorAttachment);
}

TEST(RenderGraphTest, TransientExportInfersTransferUsageAndPreservesStore) {
    MockDevice device; RgTexturePool pool; RgRenderPassCache cache;
    for (const bool traditional : {false, true}) {
        RenderGraph graph;
        const auto handle = graph.createTexture(
            test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16), {}, ResourceState::CopySource);
        auto pass = writer("Readback", handle, LoadOp::Clear,
            traditional ? StoreOp::DontCare : StoreOp::Store);
        if (traditional) graph.addRasterGroup("Export", {pass});
        else graph.addGraphicsPass(pass);
        auto frame = compileGraph(graph, pool, cache);
        EXPECT_EQ(device.texture_view_info(frame.textureView(handle)).usage,
            rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSource);
        ASSERT_EQ(frame.finalBarriers().size(), 1U);
        EXPECT_EQ(frame.finalBarriers()[0].after, ResourceState::CopySource);
        if (traditional)
            EXPECT_EQ(graph.compiledGroups()[0].renderPassDesc.attachments[0].storeOp, StoreOp::Store);
    }
}

TEST(RenderGraphDeathTest, RejectsInvalidNumericSampleCountsBeforeAllocation) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        auto desc = test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16);
        desc.samples = 3;
        graph.addGraphicsPass(writer("InvalidSamples", graph.createTexture(desc)));
        compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "expected 1, 2, 4 or 8");
}

TEST(RenderGraphTest, DepthFormatDerivesAspectForAttachmentsAndSampledReads) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto depth = graph.createTexture(test::textureDesc(TextureFormat::Depth32Float, 16, 16));
    RgRasterPass clear;
    clear.name = "DepthClear";
    clear.renderArea = {0, 0, 16, 16};
    clear.depthAttachments.push_back({depth});
    independent(graph, std::move(clear));
    auto sample = writer("SampleDepth", color(graph));
    sample.sampledTextures = {depth};
    independent(graph, std::move(sample));
    auto resources = compileGraph(graph, pool, cache);
    const auto& accesses = graph.compiledGroups()[1].subpasses[0].resources;
    ASSERT_EQ(accesses.size(), 2U);
    const auto& sampled = accesses[0];
    EXPECT_EQ(sampled.texture, depth);
    EXPECT_EQ(sampled.aspect, rhi::TextureAspect::Depth);
    EXPECT_EQ(sampled.state, ResourceState::ShaderRead);
    EXPECT_EQ(sampled.access, rhi::AccessMode::Read);
    const auto& first = resources.groups()[0].preBarriers;
    ASSERT_EQ(first.size(), 1U);
    EXPECT_EQ(first[0].aspect, rhi::TextureAspect::Depth);
    EXPECT_EQ(first[0].before, ResourceState::Undefined);
    EXPECT_EQ(first[0].after, ResourceState::DepthAttachment);
    const auto& next = resources.groups()[1].preBarriers;
    ASSERT_EQ(next.size(), 2U);
    EXPECT_EQ(next[0].texture, first[0].texture);
    EXPECT_EQ(next[0].aspect, rhi::TextureAspect::Depth);
    EXPECT_EQ(next[0].before, ResourceState::DepthAttachment);
    EXPECT_EQ(next[0].after, ResourceState::ShaderRead);
}

TEST(RenderGraphTest, ImportedFormatsDeriveAspectAfterRecompilation) {
    MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
    RenderScene scene; RenderContext context(pool, cache, scene);
    const auto depth = device.texture_create({.format = rhi::PixelFormat::Depth32Float,
        .width = 16, .height = 16,
        .usage = rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled});
    const auto colorTexture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16,
        .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled});
    auto current = depth;
    auto initialState = ResourceState::DepthAttachment;
    const auto source = graph.importTexture("Source", [&](RenderContext&) {
        return RgTextureBinding{current, device.texture_default_view(current), initialState};
    });
    auto pass = writer("SampleImported", color(graph));
    pass.sampledTextures = {source};
    graph.addGraphicsPass(std::move(pass));
    for (int iteration = 0; iteration < 2; ++iteration) {
        if (iteration != 0) {
            current = colorTexture;
            initialState = ResourceState::ColorAttachment;
            EXPECT_FALSE(graph.resourcesCompatible(context));
            graph.invalidate();
        }
        graph.compile(context);
        auto frame = graph.prepareFrame(context);
        const auto expected = iteration == 0 ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
        EXPECT_EQ(graph.compiledGroups()[0].subpasses[0].resources[0].aspect, expected);
        ASSERT_FALSE(frame.groups()[0].preBarriers.empty());
        EXPECT_EQ(frame.groups()[0].preBarriers[0].texture, current);
        EXPECT_EQ(frame.groups()[0].preBarriers[0].aspect, expected);
        EXPECT_EQ(frame.groups()[0].preBarriers[0].before, initialState);
        EXPECT_EQ(frame.groups()[0].preBarriers[0].after, ResourceState::ShaderRead);
    }
    EXPECT_EQ(graph.compileCount(), 2U);
}

TEST(RenderGraphTest, OneGroupProducesTwoSubpassesAndInputDependency) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderGraph graph;
    const auto gbuffer = color(graph);
    const auto output = color(graph);
    auto lighting = writer("Lighting", output);
    lighting.inputAttachments = {gbuffer};
    graph.addRasterGroup("Deferred", {writer("GBuffer", gbuffer, LoadOp::Clear, StoreOp::DontCare), lighting});
    auto resources = compileGraph(graph, pool, cache);
    ASSERT_EQ(graph.compiledGroups().size(), 1U);
    EXPECT_EQ(graph.passCount(), 2U);
    const auto& group = graph.compiledGroups()[0];
    ASSERT_TRUE(group.traditional);
    EXPECT_TRUE(resources.groups()[0].beginInfo.renderPass);
    EXPECT_TRUE(resources.groups()[0].beginInfo.framebuffer);
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
    const auto gbuffer = color(graph);
    const auto temporary = color(graph);
    const auto output = color(graph);
    auto lighting = writer("Lighting", output);
    lighting.inputAttachments = {gbuffer};
    graph.addRasterGroup("Deferred", {writer("GBuffer", gbuffer), writer("Middle", temporary), lighting});
    auto resources = compileGraph(graph, pool, cache);
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
    const auto gbuffer = color(graph);
    const auto lit = color(graph);
    const auto output = color(graph);
    auto lighting = writer("Lighting", lit);
    lighting.inputAttachments = {gbuffer};
    graph.addRasterGroup("Deferred", {writer("GBuffer", gbuffer, LoadOp::Clear, StoreOp::DontCare), lighting});
    auto post = writer("Post", output);
    post.sampledTextures = {gbuffer};
    independent(graph, post);
    auto resources = compileGraph(graph, pool, cache);
    EXPECT_EQ(graph.compiledGroups()[0].renderPassDesc.attachments[0].storeOp, StoreOp::Store);
    const auto& barriers = resources.groups()[1].preBarriers;
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
    auto resources = compileGraph(graph, pool, cache);
    const auto& barriers = resources.groups()[1].preBarriers;
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
    const auto source = graph.importTexture("Source",
        [texture, view = device.texture_default_view(texture)](RenderContext&) {
            return RgTextureBinding{texture, view, ResourceState::ShaderRead};
        });
    auto reader = writer("Read", color(graph));
    reader.sampledTextures = {source};
    independent(graph, reader);
    independent(graph, writer("Write", source));
    auto resources = compileGraph(graph, pool, cache);
    const auto& barrier = resources.groups()[1].preBarriers[0];
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
    const auto view = device.texture_default_view(texture);
    const auto resolver = [texture, view](RenderContext&) { return RgTextureBinding{texture, view}; };
    const auto first = graph.importTexture("Target", resolver);
    EXPECT_EQ(first, graph.importTexture("Target", resolver));
    EXPECT_EQ(graph.textureCount(), 1U);
    independent(graph, writer("First", first));
    auto resources = compileGraph(graph, pool, cache);
    EXPECT_TRUE(resources.finalBarriers().empty()); // Undefined is never a destination layout.
    resources.reset();
    graph.reset();
    EXPECT_FALSE(graph.compiled());
    EXPECT_EQ(graph.passCount(), 0U);
    EXPECT_EQ(graph.textureCount(), 0U);
    graph.addRasterGroup("New", {writer("New", color(graph))});
    resources = compileGraph(graph, pool, cache);
    EXPECT_EQ(graph.passCount(), 1U);
    EXPECT_TRUE(graph.compiledGroups()[0].traditional);
    resources.reset();
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
        auto resources = compileGraph(graph, pool, cache);
        const auto& begin = resources.groups()[0].beginInfo;
        if (frame == 0) {
            renderPass = begin.renderPass;
            framebuffer = begin.framebuffer;
        } else {
            EXPECT_EQ(begin.renderPass, renderPass);
            EXPECT_EQ(begin.framebuffer, framebuffer);
        }
    }
    EXPECT_EQ(pool.inUseCount(), 0U);
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
        auto resources = compileGraph(graph, pool, cache);
        EXPECT_EQ(resources.groups()[0].beginInfo.renderPass, renderPass);
        EXPECT_NE(resources.groups()[0].beginInfo.framebuffer, framebuffer);
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

TEST(RenderGraphTest, DynamicExtentAndClearValuesChangeWithoutRecompilation) {
    MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
    RenderScene scene; RenderContext context(pool, cache, scene);
    std::uint32_t size = 16;
    math::Vec4 clear{0.25F};
    const auto extent = [&](const RenderContext&) { return rhi::Rect{0, 0, size, size}; };
    const auto target = graph.createTexture(test::textureDesc(TextureFormat::Rgba8Unorm, 0, 0), extent);
    auto pass = writer("Resize", target);
    pass.renderAreaResolver = extent;
    pass.colorAttachments[0].clearColorResolver = [&](const RenderContext&) { return clear; };
    graph.addRasterGroup("Resize", {pass});
    graph.compile(context);
    rhi::RID renderPass, framebuffer, view;
    for (int iteration = 0; iteration < 3; ++iteration) {
        auto frame = graph.prepareFrame(context);
        const auto& begin = frame.groups()[0].beginInfo;
        EXPECT_EQ(begin.renderArea.width, size);
        EXPECT_EQ(begin.clearValues[0].color, clear);
        EXPECT_EQ(frame.groups()[0].rendering[0].colorAttachments[0].view, frame.textureView(target));
        EXPECT_EQ(device.texture_view_info(frame.textureView(target)).width, size);
        if (iteration == 0) {
            renderPass = begin.renderPass; framebuffer = begin.framebuffer; view = frame.textureView(target);
        } else {
            EXPECT_EQ(begin.renderPass, renderPass);
            if (iteration == 1) EXPECT_NE(begin.framebuffer, framebuffer);
            else {
                EXPECT_EQ(begin.framebuffer, framebuffer);
                EXPECT_EQ(frame.textureView(target), view); // Reuse old size's allocation.
            }
        }
        EXPECT_EQ(pool.inUseCount(), 1U);
        auto moved = std::move(frame); // Ownership transfers exactly once.
        moved.reset();
        EXPECT_EQ(pool.inUseCount(), 0U);
        EXPECT_TRUE(graph.compiled());
        size = iteration == 0 ? 32U : 16U;
        clear = math::Vec4{0.75F};
    }
    EXPECT_EQ(graph.compileCount(), 1U);
    EXPECT_EQ(pool.totalEntryCount(), 2U);
    EXPECT_EQ(cache.renderPassCount(), 1U);
    EXPECT_EQ(cache.framebufferCount(), 2U);
}

TEST(RenderGraphTest, ExternalBindingsTrackCurrentImageStateAndFormatChanges) {
    MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
    RenderScene scene; RenderContext context(pool, cache, scene);
    auto create = [&](rhi::SampleCount samples) {
        return device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment,
            .samples = samples});
    };
    const rhi::RID textures[]{create(rhi::SampleCount::One), create(rhi::SampleCount::One),
                              create(rhi::SampleCount::Four)};
    rhi::RID current = textures[0];
    auto state = ResourceState::Undefined;
    const auto resolver = [&](RenderContext&) {
        return RgTextureBinding{current, device.texture_default_view(current), state,
                                rhi::AccessMode::Auto, &state};
    };
    const auto target = graph.importTexture("CurrentTarget", resolver);
    EXPECT_EQ(target, graph.importTexture("CurrentTarget", resolver));
    graph.addRasterGroup("Current", {writer("Color", target)});
    graph.compile(context);
    rhi::RID cachedPass;
    for (int iteration = 0; iteration < 2; ++iteration) {
        current = textures[iteration];
        state = iteration == 0 ? ResourceState::Undefined : ResourceState::ColorAttachment;
        EXPECT_TRUE(graph.resourcesCompatible(context));
        auto frame = graph.prepareFrame(context);
        EXPECT_EQ(frame.texture(target), current);
        EXPECT_EQ(frame.groups()[0].preBarriers[0].texture, current);
        EXPECT_EQ(frame.groups()[0].preBarriers[0].before, state);
        EXPECT_EQ(frame.groups()[0].preBarriers[0].sourceAccess,
                  iteration == 0 ? rhi::AccessMode::Read : rhi::AccessMode::ReadWrite);
        if (cachedPass) EXPECT_EQ(frame.groups()[0].beginInfo.renderPass, cachedPass);
        cachedPass = frame.groups()[0].beginInfo.renderPass;
    }
    EXPECT_EQ(graph.compileCount(), 1U);
    current = textures[2];
    EXPECT_FALSE(graph.resourcesCompatible(context));
    graph.invalidate();
    graph.compile(context);
    EXPECT_EQ(graph.compileCount(), 2U);
    auto frame = graph.prepareFrame(context);
    EXPECT_NE(frame.groups()[0].beginInfo.renderPass, cachedPass);
    frame.reset();
    graph.invalidate();
    graph.addGraphicsPass(writer("Additional", target));
    graph.compile(context);
    EXPECT_EQ(graph.compileCount(), 3U);
    EXPECT_EQ(graph.passCount(), 2U);
}

TEST(RenderGraphTest, TexturePoolsRetireOldDeviceEntriesWithoutTouchingNewRids) {
    auto orphaned = std::make_unique<RgTexturePool>();
    auto reused = std::make_unique<RgTexturePool>();
    const TextureDesc desc = test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16);
    {
        MockDevice first;
        auto* a = orphaned->acquire(desc, rhi::TextureUsage::ColorAttachment);
        auto* b = reused->acquire(desc, rhi::TextureUsage::ColorAttachment);
        RgTexturePool::release(a);
        RgTexturePool::release(b);
    }
    MockDevice second;
    const auto original = second.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
        .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment});
    orphaned.reset();
    EXPECT_EQ(second.destroyedTextures, 0U);
    EXPECT_TRUE(second.texture_default_view(original));
    auto* fresh = reused->acquire(desc, rhi::TextureUsage::ColorAttachment);
    EXPECT_EQ(second.textures.size(), 2U);
    EXPECT_NE(fresh->texture->textureHandle(), original);
    EXPECT_EQ(second.texture_view_info(fresh->texture->defaultView()).texture,
              fresh->texture->textureHandle());
    RgTexturePool::release(fresh);
    reused.reset();
    EXPECT_EQ(second.destroyedTextures, 1U);
    EXPECT_TRUE(second.texture_default_view(original));
}

TEST(RenderGraphTest, PoolTextureReferencesKeepResourcesAliveAfterPoolDestruction) {
    MockDevice device;
    Ref<Texture> retained;
    {
        RgTexturePool pool;
        auto* entry = pool.acquire(test::textureDesc(TextureFormat::Bgra8Unorm, 16, 16), rhi::TextureUsage::ColorAttachment);
        retained = entry->texture;
        RgTexturePool::release(entry);
    }
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained->pixelFormat(), rhi::PixelFormat::Bgra8Unorm);
    EXPECT_EQ(device.destroyedTextures, 0U);
    retained = {};
    EXPECT_EQ(device.destroyedTextures, 1U);
}

TEST(RenderGraphTest, GlobalPoolTrimsOnlyCompletedUnleasedTexturesAndInvalidatesFramebuffers) {
    MockDevice device;
    RgTexturePool pool;
    RgRenderPassCache cache;
    auto* entry = pool.acquire(test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16), rhi::TextureUsage::ColorAttachment);
    rhi::RenderPassDesc desc;
    desc.attachments.push_back({rhi::PixelFormat::Rgba8Unorm, rhi::SampleCount::One,
        LoadOp::Clear, StoreOp::Store, ResourceState::ColorAttachment, ResourceState::ColorAttachment});
    rhi::SubpassDesc subpass;
    subpass.colors.push_back({0, ResourceState::ColorAttachment});
    desc.subpasses.push_back(subpass);
    const auto pass = cache.getOrCreateRenderPass(desc);
    const auto framebuffer = cache.getOrCreateFramebuffer({pass, {entry->texture->defaultView()}, 16, 16, 1});
    pool.beginBatch();
    pool.recordUsage(entry, ResourceState::ColorAttachment, rhi::AccessMode::Write);
    pool.release(entry);
    pool.trim(0, cache); // CPU release does not retire recorded work.
    EXPECT_EQ(cache.framebufferCount(), 1U);
    pool.onSubmitted(1);
    pool.trim(0, cache); // GPU work still owns the texture.
    EXPECT_EQ(pool.totalEntryCount(), 1U);
    pool.collect(1);
    pool.trim(0, cache);
    EXPECT_EQ(pool.totalEntryCount(), 0U);
    EXPECT_EQ(cache.framebufferCount(), 0U);
    EXPECT_EQ(device.destroyedTextures, 1U);
    EXPECT_EQ(device.destroyedFramebuffers, (std::vector<rhi::RID>{framebuffer}));
    EXPECT_EQ(cache.renderPassCount(), 1U);
}

TEST(RenderGraphTest, GlobalPoolIdleAgeUsesBatchEpochAndPreservesRetainedReferences) {
    MockDevice device; RgTexturePool pool; RgRenderPassCache cache;
    auto* entry = pool.acquire(test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16), rhi::TextureUsage::ColorAttachment);
    Ref<Texture> retained = entry->texture;
    pool.release(entry);
    pool.beginBatch(); pool.cancelBatch();
    pool.trim(2, cache);
    EXPECT_EQ(pool.totalEntryCount(), 1U);
    pool.beginBatch(); pool.cancelBatch();
    pool.trim(2, cache);
    EXPECT_EQ(pool.totalEntryCount(), 1U); // An external owning reference remains.
    retained = {};
    pool.trim(2, cache);
    EXPECT_EQ(pool.totalEntryCount(), 0U);
}

TEST(RenderGraphTest, GlobalPoolMatchesSamplerAndUsageButIgnoresDebugName) {
    MockDevice device;
    RgTexturePool pool;
    const auto usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    auto desc = test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16, "Original");
    auto* original = pool.acquire(desc, usage);
    pool.release(original);
    desc.debugName = "Renamed";
    auto* renamed = pool.acquire(desc, usage);
    EXPECT_EQ(renamed, original);
    pool.release(renamed);

    auto changed = desc;
    changed.sampler.addressModeU = TextureAddressMode::ClampToEdge;
    auto* clamped = pool.acquire(changed, usage);
    EXPECT_NE(clamped, original);
    EXPECT_EQ(device.samplers.back().addressU, rhi::SamplerAddressMode::ClampToEdge);
    pool.release(clamped);
    changed = desc;
    changed.sampler.filterMode = TextureFilterMode::Point;
    auto* point = pool.acquire(changed, usage);
    EXPECT_NE(point, original);
    EXPECT_NE(point, clamped);
    EXPECT_EQ(device.samplers.back().minFilter, rhi::SamplerFilter::Nearest);
    pool.release(point);
    changed = desc;
    changed.sampler.maxAnisotropy = 4.0F;
    auto* anisotropic = pool.acquire(changed, usage);
    EXPECT_NE(anisotropic, original);
    EXPECT_EQ(device.samplers.back().maxAnisotropy, 4.0F);
    pool.release(anisotropic);
    changed = desc;
    auto* transfer = pool.acquire(changed, usage | rhi::TextureUsage::TransferSource);
    EXPECT_NE(transfer, original);
    EXPECT_TRUE(rhi::hasFlag(transfer->texture->usage(), rhi::TextureUsage::TransferSource));
    EXPECT_EQ(device.textures.back().tiling, rhi::TextureTiling::Optimal);
    pool.release(transfer);
    changed.debugName = "TransferReuse";
    auto* again = pool.acquire(changed, usage | rhi::TextureUsage::TransferSource);
    EXPECT_EQ(again, transfer);
    pool.release(again);
    EXPECT_EQ(pool.totalEntryCount(), 5U);
}

TEST(RenderGraphTest, FinalInputAndShaderReadLayoutsPreserveStoredContents) {
    MockDevice device; RgTexturePool pool; RgRenderPassCache cache;
    for (const auto final : {ResourceState::InputAttachment, ResourceState::ShaderRead}) {
        RenderGraph graph;
        const auto texture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16,
            .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::InputAttachment});
        const auto handle = graph.importTexture("Output",
            [texture, view = device.texture_default_view(texture)](RenderContext&) {
                return RgTextureBinding{texture, view};
            }, final);
        graph.addRasterGroup("Export", {writer("Color", handle, LoadOp::Clear, StoreOp::DontCare)});
        auto frame = compileGraph(graph, pool, cache);
        EXPECT_EQ(graph.compiledGroups()[0].renderPassDesc.attachments[0].storeOp, StoreOp::Store);
        ASSERT_EQ(frame.finalBarriers().size(), 1U);
        EXPECT_EQ(frame.finalBarriers()[0].after, final);
    }
}

TEST(RenderGraphDeathTest, TexturePoolRejectsNestedBatchesAndDuplicateRelease) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool;
        pool.beginBatch(); pool.beginBatch();
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "batch is already active");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        graph.addGraphicsPass(writer("Color", color(graph)));
        auto frame = compileGraph(graph, pool, cache);
        auto* entry = pool.acquire(test::textureDesc(TextureFormat::Rgba8Unorm, 16, 16), rhi::TextureUsage::ColorAttachment);
        RgTexturePool::release(entry); RgTexturePool::release(entry);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "lease already released");
}

TEST(RenderGraphDeathTest, RejectsInvalidFinalStatesAndDiscardedFinalContents) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto texture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment});
        const auto handle = graph.importTexture("Output",
            [texture, view = device.texture_default_view(texture)](RenderContext&) {
                return RgTextureBinding{texture, view};
            }, ResourceState::CopySource);
        graph.addGraphicsPass(writer("Color", handle));
        auto frame = compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "view/usage/extent");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto texture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16,
            .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled});
        const auto handle = graph.importTexture("Output",
            [texture, view = device.texture_default_view(texture)](RenderContext&) {
                return RgTextureBinding{texture, view};
            }, ResourceState::ShaderRead);
        graph.addGraphicsPass(writer("Discard", handle, LoadOp::Clear, StoreOp::DontCare));
        auto frame = compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Final state reads undefined");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto texture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16, .usage = rhi::TextureUsage::Sampled});
        (void)graph.importTexture("Output",
            [texture, view = device.texture_default_view(texture)](RenderContext&) {
                return RgTextureBinding{texture, view};
            }, ResourceState::ShaderRead);
        auto frame = compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "reads undefined texture contents");
}

TEST(RenderGraphDeathTest, RejectsDynamicRenderingAttachmentsWithDifferentSamples) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        auto pass = writer("MixedSamples", color(graph));
        const auto depth = graph.createTexture(test::textureDesc(TextureFormat::Depth32Float, 16, 16, "", 4));
        pass.depthAttachments.push_back({depth});
        graph.addGraphicsPass(std::move(pass));
        auto frame = compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "matching sample counts");
}

TEST(RenderGraphDeathTest, RejectsResetAndOverlappingPreparationWhileFrameIsLive) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        graph.addGraphicsPass(writer("Color", color(graph)));
        auto frame = compileGraph(graph, pool, cache);
        graph.reset();
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Release prepared frame resources");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        RenderScene scene; RenderContext context(pool, cache, scene);
        graph.addGraphicsPass(writer("Color", color(graph)));
        graph.compile(context);
        auto first = graph.prepareFrame(context);
        auto second = graph.prepareFrame(context);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Release prepared frame resources");
}

TEST(RenderGraphDeathTest, RejectsSamplingAttachmentInsideGroup) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto source = color(graph);
        auto lighting = writer("Lighting", color(graph));
        lighting.sampledTextures = {source};
        graph.addRasterGroup("Invalid", {writer("GBuffer", source), lighting});
        compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Sampling a group attachment");
}

TEST(RenderGraphTest, CompileIsIdempotentAndDoesNotAllocatePhysicalResources) {
    MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
    RenderScene scene; RenderContext context(pool, cache, scene);
    graph.addRasterGroup("Frame", {writer("Color", color(graph))});
    graph.compile(context);
    graph.compile(context);
    EXPECT_EQ(graph.compileCount(), 1U);
    EXPECT_EQ(pool.inUseCount(), 0U);
    EXPECT_TRUE(device.textures.empty());
    EXPECT_EQ(cache.renderPassCount(), 0U);
    auto frame = graph.prepareFrame(context);
    EXPECT_EQ(pool.inUseCount(), 1U);
    graph.compile(context);
    EXPECT_EQ(graph.compileCount(), 1U);
    frame.reset();
    EXPECT_TRUE(graph.compiled());
    EXPECT_EQ(pool.inUseCount(), 0U);
}

TEST(RenderGraphDeathTest, RejectsRepeatedClearAndMismatchedAreas) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto output = color(graph);
        graph.addRasterGroup("Invalid", {writer("First", output), writer("Second", output)});
        compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Repeated attachment must Load");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        auto second = writer("Second", color(graph)); second.renderArea.width = 8;
        graph.addRasterGroup("Invalid", {writer("First", color(graph)), second});
        compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "render areas must match");
}

TEST(RenderGraphDeathTest, TrackedImportMustBeInitializedByExecutionTime) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        RenderScene scene; RenderContext context(pool, cache, scene);
        const auto texture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment});
        auto state = ResourceState::Undefined;
        const auto handle = graph.importTexture("Uninitialized", [&](RenderContext&) {
            return RgTextureBinding{texture, device.texture_default_view(texture), state,
                                    rhi::AccessMode::Auto, &state};
        });
        graph.addGraphicsPass(writer("Load", handle, LoadOp::Load));
        graph.compile(context);
        auto frame = graph.prepareFrame(context); // An earlier graph could still initialize it.
        const auto command = rhi::RID(0, 1);
        pool.beginBatch(command);
        graph.execute(context, frame, command);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "reads undefined texture contents");
}

TEST(RenderGraphDeathTest, RejectsUndefinedInputAndMissingUsage) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        auto pass = writer("UndefinedSample", color(graph));
        pass.sampledTextures = {color(graph)};
        independent(graph, std::move(pass)); compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "reads undefined texture contents");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        auto lighting = writer("Lighting", color(graph));
        lighting.inputAttachments = {color(graph)};
        graph.addRasterGroup("Invalid", {lighting}); compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "reads undefined texture contents");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto texture = device.texture_create({.format = rhi::PixelFormat::Rgba8Unorm,
            .width = 16, .height = 16, .usage = rhi::TextureUsage::ColorAttachment});
        const auto source = graph.importTexture("MissingInputUsage",
            [texture, view = device.texture_default_view(texture)](RenderContext&) {
                return RgTextureBinding{texture, view};
            });
        auto lighting = writer("Lighting", color(graph)); lighting.inputAttachments = {source};
        graph.addRasterGroup("Invalid", {writer("First", source), lighting}); compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "attachment view/usage/extent");
}

TEST(RenderGraphDeathTest, RejectsFeedbackAndModificationAfterCompile) {
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto source = color(graph);
        auto pass = writer("SampledFeedback", source); pass.sampledTextures = {source};
        independent(graph, std::move(pass)); compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Attachment feedback or conflicting usage");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        const auto source = color(graph);
        auto pass = writer("Invalid", source); pass.inputAttachments = {source};
        graph.addRasterGroup("Invalid", {pass}); compileGraph(graph, pool, cache);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Duplicate attachment binding");
    EXPECT_EXIT({
        MockDevice device; RgTexturePool pool; RgRenderPassCache cache; RenderGraph graph;
        independent(graph, writer("First", color(graph))); compileGraph(graph, pool, cache);
        (void)color(graph);
    }, ::testing::ExitedWithCode(EXIT_FAILURE), "Cannot modify a compiled graph");
}
} // namespace
