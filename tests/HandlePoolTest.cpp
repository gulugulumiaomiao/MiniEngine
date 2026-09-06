#include "core/base/HandlePool.h"

#include <cstdint>
#include <limits>

namespace {

struct TestHandle {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{};
};

} // namespace

int main() {
    engine::HandlePool<int, TestHandle> pool;
    const TestHandle first = pool.insert(10);
    const TestHandle second = pool.insert(20);
    if (first.index != 0 || second.index != 1 || pool.size() != 2 ||
        pool.freeCount() != 0) {
        return 1;
    }

    if (!pool.release(first) || pool.find(first) || pool.size() != 1 ||
        pool.freeCount() != 1) {
        return 2;
    }
    const TestHandle reused = pool.insert(30);
    if (reused.index != first.index ||
        reused.generation == first.generation || *pool.find(reused) != 30 ||
        !pool.find(second)) {
        return 3;
    }

    int sum = 0;
    pool.forEach([&sum](int value) { sum += value; });
    if (sum != 50) return 4;

    pool.clear();
    if (pool.find(second) || pool.find(reused) || pool.size() != 0 ||
        pool.capacity() != 2 || pool.freeCount() != 2) {
        return 5;
    }

    const TestHandle afterClear = pool.insert(40);
    if (afterClear.index >= 2 || afterClear.generation == second.generation ||
        !pool.find(afterClear) || pool.release(first)) {
        return 6;
    }
}
