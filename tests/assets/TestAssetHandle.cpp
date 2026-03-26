#include <catch2/catch_test_macros.hpp>

#include "engine/assets/AssetHandle.h"

using namespace engine::assets;

// Dummy types to exercise the phantom-type distinction.
struct FakeTexture
{
};
struct FakeScene
{
};

TEST_CASE("AssetHandle — default-constructed handle is invalid", "[assets][handle]")
{
    AssetHandle<FakeTexture> h;
    CHECK(h.isValid() == false);
    CHECK(h.index == 0);
    CHECK(h.generation == 0);
}

TEST_CASE("AssetHandle — non-zero index is valid", "[assets][handle]")
{
    AssetHandle<FakeTexture> h{1, 1};
    CHECK(h.isValid() == true);
}

TEST_CASE("AssetHandle — equality operators", "[assets][handle]")
{
    AssetHandle<FakeTexture> a{1, 1};
    AssetHandle<FakeTexture> b{1, 1};
    AssetHandle<FakeTexture> c{2, 1};

    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("AssetHandle — handles of different phantom types are distinct types", "[assets][handle]")
{
    // These two variables must not be assignable to each other.
    // This is a compile-time property; we verify the values are independent at runtime.
    AssetHandle<FakeTexture> tex{3, 7};
    AssetHandle<FakeScene> scene{3, 7};

    CHECK(tex.index == scene.index);
    CHECK(tex.generation == scene.generation);
    // They are different C++ types — no implicit conversion exists.
    static_assert(!std::is_same_v<AssetHandle<FakeTexture>, AssetHandle<FakeScene>>);
}

TEST_CASE("AssetHandle — default-constructed handles compare equal", "[assets][handle]")
{
    AssetHandle<FakeTexture> a;
    AssetHandle<FakeTexture> b;
    CHECK(a == b);
}

TEST_CASE("AssetHandle — handles with same index but different generation are not equal",
          "[assets][handle]")
{
    AssetHandle<FakeTexture> a{1, 1};
    AssetHandle<FakeTexture> b{1, 2};
    CHECK(a != b);
}
