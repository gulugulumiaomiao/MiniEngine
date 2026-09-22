#include "core/base/Ref.h"
#include "core/base/RefCounted.h"

#include <gtest/gtest.h>

#include <utility>

namespace {

// Probe 通过共享计数器记录存活实例数，测试据此断言"引用归零即析构对象"。
struct Probe : engine::RefCounted {
    explicit Probe(int& live) : live_(live) { ++live_; }
    ~Probe() override { --live_; }

    // 从 this 重建 Ref 的真实场景：内嵌计数保证再取一次引用是安全的。
    [[nodiscard]] engine::Ref<Probe> selfRef() { return engine::Ref<Probe>(this); }

    int& live_;
    int payload{0};
};

struct DerivedProbe : Probe {
    using Probe::Probe;
};

} // namespace

TEST(RefTest, DefaultConstructedIsNull) {
    engine::Ref<Probe> ref;
    EXPECT_FALSE(ref);
    EXPECT_FALSE(ref.valid());
    EXPECT_EQ(ref.get(), nullptr);
    EXPECT_EQ(ref.useCount(), 0U);
    EXPECT_TRUE(ref == nullptr);
}

TEST(RefTest, MakeRefStartsWithOneRef) {
    int live = 0;
    engine::Ref<Probe> ref = engine::makeRef<Probe>(live);
    EXPECT_TRUE(ref);
    EXPECT_EQ(live, 1);
    EXPECT_EQ(ref.useCount(), 1U);

    // 覆盖 operator-> 与 operator*。
    ref->payload = 42;
    EXPECT_EQ((*ref).payload, 42);
}

TEST(RefTest, CopyIncrementsCount) {
    int live = 0;
    engine::Ref<Probe> first = engine::makeRef<Probe>(live);
    {
        engine::Ref<Probe> second = first;
        EXPECT_EQ(first.get(), second.get());
        EXPECT_EQ(first.useCount(), 2U);
    }
    EXPECT_EQ(first.useCount(), 1U);
    EXPECT_EQ(live, 1);
}

TEST(RefTest, MoveTransfersOwnership) {
    int live = 0;
    engine::Ref<Probe> source = engine::makeRef<Probe>(live);
    Probe* raw = source.get();
    engine::Ref<Probe> target = std::move(source);
    EXPECT_FALSE(source);
    EXPECT_EQ(target.get(), raw);
    EXPECT_EQ(target.useCount(), 1U);
    EXPECT_EQ(live, 1);
}

TEST(RefTest, CopyAssignReplacesOldReference) {
    int liveA = 0;
    int liveB = 0;
    engine::Ref<Probe> a = engine::makeRef<Probe>(liveA);
    engine::Ref<Probe> b = engine::makeRef<Probe>(liveB);
    a = b;
    EXPECT_EQ(a.get(), b.get());
    EXPECT_EQ(b.useCount(), 2U);
    EXPECT_EQ(liveA, 0); // 原 a 指向的对象已析构
    EXPECT_EQ(liveB, 1);

    // 自赋值必须保持引用有效；用指针别名规避 -Wself-assign 诊断。
    engine::Ref<Probe>* alias = &a;
    a = *alias;
    EXPECT_TRUE(a);
    EXPECT_EQ(a.useCount(), 2U);
    EXPECT_EQ(liveB, 1);
}

TEST(RefTest, MoveAssignReleasesOldTarget) {
    int liveA = 0;
    int liveB = 0;
    engine::Ref<Probe> a = engine::makeRef<Probe>(liveA);
    engine::Ref<Probe> b = engine::makeRef<Probe>(liveB);
    Probe* rawB = b.get();
    a = std::move(b);
    EXPECT_EQ(liveA, 0); // 原 a 指向的对象已析构
    EXPECT_FALSE(b);
    EXPECT_EQ(a.get(), rawB);
    EXPECT_EQ(a.useCount(), 1U);
    EXPECT_EQ(liveB, 1);
}

TEST(RefTest, DestroysObjectAtZero) {
    int live = 0;
    {
        engine::Ref<Probe> ref = engine::makeRef<Probe>(live);
        EXPECT_EQ(live, 1);
    }
    EXPECT_EQ(live, 0);
}

TEST(RefTest, ResetDropsAndReplaces) {
    int liveA = 0;
    int liveB = 0;
    engine::Ref<Probe> ref = engine::makeRef<Probe>(liveA);
    ref.reset();
    EXPECT_FALSE(ref);
    EXPECT_EQ(liveA, 0);

    Probe* rawB = new Probe(liveB);
    ref.reset(rawB);
    EXPECT_EQ(ref.get(), rawB);
    EXPECT_EQ(ref.useCount(), 1U);
    EXPECT_EQ(liveB, 1);
}

TEST(RefTest, AdoptFromRawPointerRebuildsRef) {
    int live = 0;
    engine::Ref<Probe> owner = engine::makeRef<Probe>(live);
    // selfRef() 内部执行 Ref<Probe>(this)，从裸指针安全地再取一次引用。
    engine::Ref<Probe> rebuilt = owner->selfRef();
    EXPECT_EQ(rebuilt.get(), owner.get());
    EXPECT_EQ(owner.useCount(), 2U);
    EXPECT_EQ(live, 1);
}

TEST(RefTest, UpcastSharesOwnership) {
    int live = 0;
    engine::Ref<DerivedProbe> derived = engine::makeRef<DerivedProbe>(live);
    engine::Ref<Probe> base = derived; // 隐式 upcast（拷贝）
    EXPECT_EQ(base.get(), derived.get());
    EXPECT_EQ(derived.useCount(), 2U);
    EXPECT_EQ(live, 1);

    // move upcast 不改变计数。
    engine::Ref<Probe> moved = engine::makeRef<DerivedProbe>(live);
    EXPECT_EQ(live, 2);
    EXPECT_EQ(moved.useCount(), 1U);
}

TEST(RefTest, RefCastConvertsBetweenRelatedTypes) {
    int live = 0;
    engine::Ref<DerivedProbe> derived = engine::makeRef<DerivedProbe>(live);
    engine::Ref<Probe> base = derived;                                    // upcast
    engine::Ref<DerivedProbe> back = engine::refCast<DerivedProbe>(base); // downcast
    EXPECT_EQ(back.get(), derived.get());
    EXPECT_EQ(derived.useCount(), 3U); // derived + base + back
    EXPECT_EQ(live, 1);
}

TEST(RefTest, EqualityComparesPointers) {
    int liveA = 0;
    int liveB = 0;
    engine::Ref<Probe> a = engine::makeRef<Probe>(liveA);
    engine::Ref<Probe> aCopy = a;
    engine::Ref<Probe> b = engine::makeRef<Probe>(liveB);

    EXPECT_TRUE(a == aCopy);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
    EXPECT_FALSE(a == nullptr);
    EXPECT_TRUE(a != nullptr);

    engine::Ref<Probe> empty;
    EXPECT_TRUE(empty == nullptr);
    EXPECT_TRUE(nullptr == empty);
}
