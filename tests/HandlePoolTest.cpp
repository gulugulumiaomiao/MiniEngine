#include "core/base/Handle.h"
#include "core/base/HandlePool.h"
#include "core/base/KeyedHandleRegistry.h"

#include <string>

namespace {

struct TestHandleTag;
using TestHandle = engine::Handle<TestHandleTag>;

struct TestResource {
    std::string key;
    int value{};
};

class TestRegistry final
    : public engine::KeyedHandleRegistry<TestResource, TestHandle, std::string> {
private:
    [[nodiscard]] std::string keyOf(const TestResource& resource) const override {
        return resource.key;
    }

    [[nodiscard]] bool validate(const TestResource& resource) const override {
        return !resource.key.empty();
    }
};

} // namespace

int main() {
    engine::HandlePool<int, TestHandle> pool;
    const TestHandle first = pool.insert(10);
    const TestHandle second = pool.insert(20);
    if (first.index != 0 || second.index != 1 || pool.size() != 2 || pool.freeCount() != 0) {
        return 1;
    }

    if (!pool.release(first) || pool.find(first) || pool.size() != 1 || pool.freeCount() != 1) {
        return 2;
    }
    const TestHandle reused = pool.insert(30);
    if (reused.index != first.index || reused.generation == first.generation ||
        *pool.find(reused) != 30 || !pool.find(second)) {
        return 3;
    }

    int sum = 0;
    pool.forEach([&sum](int value) { sum += value; });
    if (sum != 50)
        return 4;

    pool.clear();
    if (pool.find(second) || pool.find(reused) || pool.size() != 0 || pool.capacity() != 2 ||
        pool.freeCount() != 2) {
        return 5;
    }

    const TestHandle afterClear = pool.insert(40);
    if (afterClear.index >= 2 || afterClear.generation == second.generation ||
        !pool.find(afterClear) || pool.release(first)) {
        return 6;
    }

    TestRegistry registry;
    const TestHandle alpha = registry.insert({"alpha", 10});
    const TestHandle duplicate = registry.insert({"alpha", 20});
    if (!alpha || duplicate != alpha || registry.size() != 1 ||
        registry.findHandle("alpha") != alpha || registry.find("alpha")->value != 10) {
        return 7;
    }

    if (!registry.destroy("alpha") || registry.find(alpha) || registry.find("alpha") ||
        registry.findHandle("alpha")) {
        return 8;
    }

    const TestHandle beta = registry.insert({"beta", 30});
    if (beta.index != alpha.index || beta.generation == alpha.generation ||
        registry.find(beta)->value != 30 || registry.destroy(alpha)) {
        return 9;
    }

    if (registry.insert({"", 40}) || registry.size() != 1)
        return 10;

    registry.clear();
    if (registry.size() != 0 || registry.find(beta) || registry.find("beta"))
        return 11;
}
