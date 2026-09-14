#include "scene/scene/Scene.h"
#include "scene/scene/SceneAsset.h"
#include "scene/scene/SceneExport.h"

#include <cstdio>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

namespace {
using namespace engine;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                           \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

bool near(const math::Mat44& a, const math::Mat44& b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(a[c][r]) || !std::isfinite(b[c][r]) ||
                std::abs(a[c][r] - b[c][r]) >
                    1.0e-5F + 1.0e-5F * std::max(std::abs(a[c][r]), std::abs(b[c][r])))
                return false;
    return true;
}

bool ordering() {
    Scene scene;
    const auto root = scene.rootHandle();
    const auto a = scene.createNode("A"), b = scene.createNode("B"), c = scene.createNode("C");
    std::string error;
    CHECK(scene.canMoveNode(a, root, 2, error));
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, b, c}));
    CHECK(scene.moveNode(a, root, 2, error) == NodeMoveResult::Changed);
    CHECK((scene.root().children() == std::vector<NodeHandle>{b, c, a}));
    CHECK(scene.moveNode(a, root, 0, error) == NodeMoveResult::Changed);
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, b, c}));
    CHECK(scene.moveNode(a, root, 0, error) == NodeMoveResult::Unchanged);
    CHECK(scene.moveNode(c, root, 1, error) == NodeMoveResult::Changed);
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, c, b}));
    CHECK(scene.moveNode(c, a, 0, error) == NodeMoveResult::Changed);
    CHECK(scene.moveNode(b, a, 0, error) == NodeMoveResult::Changed);
    CHECK((scene.findNode(a)->children() == std::vector<NodeHandle>{b, c}));
    CHECK(scene.moveNode(b, a, 1, error) == NodeMoveResult::Changed);
    CHECK((scene.findNode(a)->children() == std::vector<NodeHandle>{c, b}));
    const auto d = scene.createNode("D");
    CHECK(scene.moveNode(a, d, 0, error) == NodeMoveResult::Changed);
    CHECK(scene.findNode(b)->parent() == a && scene.findNode(c)->parent() == a);
    CHECK(scene.moveNode(a, root, 0, error) == NodeMoveResult::Changed);
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, d}));
    return true;
}

bool rejectedMoves() {
    Scene scene;
    const auto root = scene.rootHandle();
    const auto a = scene.createNode("A"), b = scene.createNode("B");
    CHECK(scene.findNode(b)->setParent(a));
    const auto oldRoot = scene.root().children();
    const auto oldChildren = scene.findNode(a)->children();
    const auto oldLocal = scene.findNode(a)->transform().localMatrix();
    std::string error;
    for (const auto [node, parent, index] :
         std::vector<std::tuple<NodeHandle, NodeHandle, std::size_t>>{
             {root, a, 0}, {a, a, 0}, {a, b, 0}, {{999, 1}, a, 0}, {a, {}, 0}, {a, root, 9}}) {
        CHECK(!scene.canMoveNode(node, parent, index, error) && !error.empty());
        CHECK(scene.moveNode(node, parent, index, error) == NodeMoveResult::Rejected);
        CHECK(scene.root().children() == oldRoot);
        CHECK(scene.findNode(a)->children() == oldChildren);
        CHECK(near(scene.findNode(a)->transform().localMatrix(), oldLocal));
    }
    const auto gone = scene.createNode("Gone");
    CHECK(scene.destroyNode(gone));
    const auto reused = scene.createNode("Reused");
    CHECK(gone.index == reused.index && gone.generation != reused.generation);
    CHECK(scene.moveNode(gone, root, 0, error) == NodeMoveResult::Rejected);
    return true;
}

bool transforms() {
    // 两边父节点的旋转相同时，非均匀与负缩放也能精确表达。
    for (const math::Vec3 scale :
         {math::Vec3{1.0F}, math::Vec3{2.0F}, math::Vec3{2, 3, 4}, math::Vec3{-2, 3, 4}}) {
        Scene scene;
        const auto a = scene.createNode("A"), b = scene.createNode("B");
        const auto child = scene.createNode("Child"), grandchild = scene.createNode("Grandchild");
        CHECK(scene.findNode(child)->setParent(a));
        CHECK(scene.findNode(grandchild)->setParent(child));
        const auto rotation = math::fromEuler({0.3F, 0.4F, -0.2F});
        scene.findNode(a)->transform().setLocalRotation(rotation);
        scene.findNode(a)->transform().setLocalPosition({3, 4, 5});
        scene.findNode(a)->transform().setLocalScale(scale);
        scene.findNode(b)->transform().setLocalRotation(rotation);
        scene.findNode(b)->transform().setLocalPosition({-1, 2, -3});
        scene.findNode(b)->transform().setLocalScale({0.5F, 0.75F, 2.0F});
        scene.findNode(child)->transform().setLocalPosition({1, -2, 3});
        scene.findNode(grandchild)->transform().setLocalPosition({2, 1, 0});
        scene.findNode(grandchild)
            ->transform()
            .setLocalRotation(math::fromEuler({0.1F, -0.2F, 0.5F}));
        const auto world = scene.findNode(child)->transform().worldMatrix();
        const auto descendant = scene.findNode(grandchild)->transform().worldMatrix();
        const auto local = scene.findNode(child)->transform().localMatrix();
        std::string error;
        CHECK(scene.canMoveNode(child, b, 0, error));
        CHECK(scene.findNode(child)->parent() == a);
        CHECK(near(scene.findNode(child)->transform().localMatrix(), local));
        CHECK(scene.moveNode(child, b, 0, error) == NodeMoveResult::Changed);
        CHECK(near(scene.findNode(child)->transform().worldMatrix(), world));
        CHECK(near(scene.findNode(grandchild)->transform().worldMatrix(), descendant));
    }
    // 均匀缩放下，两个任意旋转的父节点之间以及移回根层都保持世界变换。
    Scene scene;
    const auto a = scene.createNode("A"), b = scene.createNode("B"), c = scene.createNode("C");
    CHECK(scene.findNode(c)->setParent(a));
    scene.findNode(a)->transform().setLocalRotation(math::fromEuler({0.6F, 0.9F, 0.2F}));
    scene.findNode(b)->transform().setLocalRotation(math::fromEuler({-0.3F, 0.2F, -0.4F}));
    scene.findNode(b)->transform().setLocalScale(math::Vec3{2});
    const auto world = scene.findNode(c)->transform().worldMatrix();
    std::string error;
    CHECK(scene.moveNode(c, b, 0, error) == NodeMoveResult::Changed);
    CHECK(near(scene.findNode(c)->transform().worldMatrix(), world));
    CHECK(scene.moveNode(c, scene.rootHandle(), 0, error) == NodeMoveResult::Changed);
    CHECK(near(scene.findNode(c)->transform().worldMatrix(), world));
    return true;
}

bool invalidTransforms() {
    Scene scene;
    const auto parent = scene.createNode("Parent"), node = scene.createNode("Node");
    const auto root = scene.rootHandle();
    const auto oldOrder = scene.root().children();
    auto& transform = scene.findNode(parent)->transform();
    const auto local = scene.findNode(node)->transform().localMatrix();
    std::string error;
    for (const auto scale : {math::Vec3{0, 1, 1},
                             math::Vec3{1.0e-9F, 1, 1},
                             math::Vec3{std::numeric_limits<float>::infinity(), 1, 1},
                             math::Vec3{std::numeric_limits<float>::quiet_NaN(), 1, 1}}) {
        transform.setLocalScale(scale);
        CHECK(!scene.canMoveNode(node, parent, 0, error));
        CHECK(scene.moveNode(node, parent, 0, error) == NodeMoveResult::Rejected);
        CHECK(scene.findNode(node)->parent() == root && scene.root().children() == oldOrder);
        CHECK(near(scene.findNode(node)->transform().localMatrix(), local));
    }
    transform.setLocalScale({2, 1, 1});
    transform.setLocalRotation(math::angleAxis(0.7F, {0, 0, 1}));
    CHECK(scene.moveNode(node, parent, 0, error) == NodeMoveResult::Rejected);
    CHECK(scene.root().children() == oldOrder);
    transform.setLocalScale(math::Vec3{1});
    transform.setLocalRotation(math::Quat{1, 0, 0, 0});
    scene.findNode(node)->transform().setLocalScale({0, 1, 1});
    CHECK(scene.moveNode(node, parent, 0, error) == NodeMoveResult::Rejected);
    CHECK(scene.moveNode(node, root, 0, error) == NodeMoveResult::Changed);
    CHECK(scene.moveNode(node, root, 0, error) == NodeMoveResult::Unchanged);
    return true;
}

struct Counts {
    int enable{}, disable{};
};
class Probe final : public Component {
public:
    explicit Probe(Counts& counts) : counts_(counts) {}

private:
    void onEnable() override { ++counts_.enable; }
    void onDisable() override { ++counts_.disable; }
    Counts& counts_;
};

bool activation() {
    Counts childCounts, grandchildCounts;
    Scene scene;
    const auto off = scene.createNode("Off"), other = scene.createNode("Other");
    const auto child = scene.createNode("Child"), grandchild = scene.createNode("Grandchild");
    scene.findNode(off)->setActive(false);
    CHECK(scene.findNode(grandchild)->setParent(child));
    scene.findNode(child)->addComponent<Probe>(childCounts);
    scene.findNode(grandchild)->addComponent<Probe>(grandchildCounts);
    std::string error;
    CHECK(scene.canMoveNode(child, off, 0, error));
    CHECK(childCounts.disable == 0 && grandchildCounts.disable == 0);
    CHECK(scene.moveNode(child, off, 0, error) == NodeMoveResult::Changed);
    CHECK(!scene.findNode(child)->activeInHierarchy() &&
          !scene.findNode(grandchild)->activeInHierarchy());
    CHECK(childCounts.disable == 1 && grandchildCounts.disable == 1);
    CHECK(scene.moveNode(other, off, 0, error) == NodeMoveResult::Changed);
    CHECK(scene.moveNode(child, off, 0, error) == NodeMoveResult::Changed);
    CHECK(childCounts.enable == 1 && childCounts.disable == 1);
    CHECK(scene.moveNode(child, scene.rootHandle(), 0, error) == NodeMoveResult::Changed);
    CHECK(childCounts.enable == 2 && grandchildCounts.enable == 2);
    return true;
}

bool sameTree(const Node& a, const Node& b) {
    CHECK(a.name() == b.name() && a.activeSelf() == b.activeSelf());
    CHECK(a.children().size() == b.children().size());
    CHECK(near(a.transform().worldMatrix(), b.transform().worldMatrix()));
    for (std::size_t i = 0; i < a.children().size(); ++i)
        CHECK(sameTree(*a.scene().findNode(a.children()[i]), *b.scene().findNode(b.children()[i])));
    return true;
}

bool roundtrip() {
    Scene scene{"Ordering"};
    const auto a = scene.createNode("A"), b = scene.createNode("B"), c = scene.createNode("C");
    const auto d = scene.createNode("D"), e = scene.createNode("E");
    scene.findNode(a)->transform().setLocalPosition({4, 2, 1});
    std::string error;
    CHECK(scene.moveNode(c, a, 0, error) == NodeMoveResult::Changed);
    CHECK(scene.moveNode(d, a, 0, error) == NodeMoveResult::Changed);
    CHECK(scene.moveNode(e, d, 0, error) == NodeMoveResult::Changed);
    CHECK(scene.moveNode(b, scene.rootHandle(), 0, error) == NodeMoveResult::Changed);
    const VirtualPath path{"assets://scenes/hierarchy.scene.json"};
    const auto asset = exportSceneToAsset(scene, path, error);
    CHECK(asset);
    CHECK(asset->nodes.size() == 5);
    CHECK(asset->nodes[0].name == "B" && asset->nodes[1].name == "A" &&
          asset->nodes[2].name == "D" && asset->nodes[3].name == "E" &&
          asset->nodes[4].name == "C");
    const auto parsed = detail::parseSceneAsset(path, writeSceneAssetJson(*asset));
    CHECK(parsed && parsed->nodes == asset->nodes);
    const auto runtime = parsed->instantiate({});
    CHECK(runtime && sameTree(scene.root(), runtime->root()));
    return true;
}

} // namespace

int main() {
    return ordering() && rejectedMoves() && transforms() && invalidTransforms() && activation() &&
                   roundtrip()
               ? 0
               : 1;
}
