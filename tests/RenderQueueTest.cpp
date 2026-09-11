#include "render/queue/RenderQueue.h"
#include "render/scene/RenderScene.h"

#include <cassert>
#include <cstdint>

namespace {

engine::DrawItem makeItem(int renderQueue,
                          std::uint32_t pipelineIndex,
                          std::uint32_t materialIndex,
                          std::uint32_t meshIndex,
                          std::uint32_t firstInstance = 0) {
    engine::DrawItem item;
    item.renderQueue = renderQueue;
    item.pipeline = engine::rhi::GraphicsPipelineHandle{pipelineIndex, 1};
    item.material = engine::MaterialHandle{materialIndex, 1};
    item.mesh = engine::MeshHandle{meshIndex, 1};
    item.arguments.firstInstance = firstInstance;
    return item;
}

} // namespace

int main() {
    using namespace engine;

    // RenderQueueRange
    {
        RenderQueueRange all = RenderQueueRange::all();
        assert(all.contains(0));
        assert(all.contains(2500));
        assert(all.contains(5000));

        RenderQueueRange opaque = RenderQueueRange::opaque();
        assert(opaque.contains(0));
        assert(opaque.contains(kRenderQueueGeometryLast));
        assert(!opaque.contains(kRenderQueueTransparent));

        RenderQueueRange transparent = RenderQueueRange::transparent();
        assert(!transparent.contains(kRenderQueueGeometryLast));
        assert(transparent.contains(kRenderQueueTransparent));
        assert(transparent.contains(kRenderQueueOverlay));
    }

    // DrawFilter queue range
    {
        DrawFilter filter{RenderQueueRange::opaque()};
        DrawItem opaqueItem = makeItem(kRenderQueueGeometry, 1, 1, 1);
        DrawItem transparentItem = makeItem(kRenderQueueTransparent, 1, 1, 1);
        assert(filter.accepts(opaqueItem, 1));
        assert(!filter.accepts(transparentItem, 1));
    }

    // DrawFilter layer mask
    {
        DrawFilter filter{RenderQueueRange::all(), 0b1010U};
        DrawItem item = makeItem(kRenderQueueGeometry, 1, 1, 1);
        assert(filter.accepts(item, 0b0010U));
        assert(!filter.accepts(item, 0b0100U));
    }

    // DrawSorter by render queue
    {
        RenderScene scene;
        scene.setCamera(RenderCamera{});

        std::vector<DrawItem> items;
        items.push_back(makeItem(kRenderQueueTransparent, 1, 1, 1));
        items.push_back(makeItem(kRenderQueueBackground, 1, 1, 1));
        items.push_back(makeItem(kRenderQueueGeometry, 1, 1, 1));

        DrawSorter sorter;
        sorter.sort(items, SortingCriteria::RenderQueue, scene);

        assert(items[0].renderQueue == kRenderQueueBackground);
        assert(items[1].renderQueue == kRenderQueueGeometry);
        assert(items[2].renderQueue == kRenderQueueTransparent);
    }

    // DrawSorter by pipeline then material then mesh
    {
        RenderScene scene;
        scene.setCamera(RenderCamera{});

        std::vector<DrawItem> items;
        items.push_back(makeItem(kRenderQueueGeometry, 3, 1, 1));
        items.push_back(makeItem(kRenderQueueGeometry, 1, 2, 3));
        items.push_back(makeItem(kRenderQueueGeometry, 1, 2, 1));

        DrawSorter sorter;
        sorter.sort(items, SortingCriteria::Pipeline | SortingCriteria::Material | SortingCriteria::Mesh, scene);

        assert(items[0].pipeline.index == 1);
        assert(items[0].material.index == 2);
        assert(items[0].mesh.index == 1);
        assert(items[1].pipeline.index == 1);
        assert(items[1].material.index == 2);
        assert(items[1].mesh.index == 3);
        assert(items[2].pipeline.index == 3);
    }

    // DrawSorter BackToFront
    {
        RenderScene scene;
        RenderCamera camera;
        camera.worldPosition = math::Vec3{0.0F, 0.0F, 0.0F};
        scene.setCamera(camera);

        RenderObject nearObject;
        nearObject.transform = math::Mat44{1.0F};
        nearObject.transform[3] = math::Vec4{0.0F, 0.0F, 5.0F, 1.0F};
        scene.submit(nearObject);

        RenderObject farObject;
        farObject.transform = math::Mat44{1.0F};
        farObject.transform[3] = math::Vec4{0.0F, 0.0F, 10.0F, 1.0F};
        scene.submit(farObject);

        std::vector<DrawItem> items;
        items.push_back(makeItem(kRenderQueueTransparent, 1, 1, 1, 0)); // near
        items.push_back(makeItem(kRenderQueueTransparent, 1, 1, 1, 1)); // far

        DrawSorter sorter;
        sorter.sort(items, SortingCriteria::BackToFront, scene);

        // Farther object should be drawn first.
        assert(items[0].arguments.firstInstance == 1);
        assert(items[1].arguments.firstInstance == 0);
    }

    return 0;
}
