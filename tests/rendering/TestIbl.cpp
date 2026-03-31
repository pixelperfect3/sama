#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

#include "engine/math/Types.h"
#include "engine/rendering/IblResources.h"
#include "engine/rendering/Renderer.h"

using Catch::Approx;
using engine::math::Vec2;
using engine::math::Vec3;

// ---------------------------------------------------------------------------
// Headless bgfx fixture (Noop renderer — no window required)
// ---------------------------------------------------------------------------

struct HeadlessBgfx
{
    engine::rendering::Renderer renderer;

    HeadlessBgfx()
    {
        engine::rendering::RendererDesc desc{};
        desc.headless = true;
        desc.width = 1;
        desc.height = 1;
        REQUIRE(renderer.init(desc));
    }

    ~HeadlessBgfx()
    {
        renderer.shutdown();
    }

    HeadlessBgfx(const HeadlessBgfx&) = delete;
    HeadlessBgfx& operator=(const HeadlessBgfx&) = delete;
};

// ---------------------------------------------------------------------------
// CPU-only math helpers (mirrors IblResources.cpp internal helpers).
// Duplicated here so tests are self-contained and not dependent on internal
// linkage of the .cpp file.
// ---------------------------------------------------------------------------

static Vec2 hammersleyCpu(uint32_t i, uint32_t N)
{
    uint32_t bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float ri = float(bits) * 2.3283064365386963e-10f;
    return Vec2(float(i) / float(N), ri);
}

static Vec3 importanceSampleGGXCpu(Vec2 xi, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0f * glm::pi<float>() * xi.x;
    float cosTheta = glm::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = glm::sqrt(1.0f - cosTheta * cosTheta);
    return Vec3(glm::cos(phi) * sinTheta, glm::sin(phi) * sinTheta, cosTheta);
}

static float geometrySmithIBLCpu(float NdotV, float NdotL, float roughness)
{
    float k = (roughness * roughness) / 2.0f;
    auto G1 = [k](float NdotX) { return NdotX / (NdotX * (1.0f - k) + k); };
    return G1(NdotV) * G1(NdotL);
}

// Compute BRDF LUT value for a given (NdotV, roughness) pair — CPU-only,
// no bgfx required.
static Vec2 computeBrdfLut(float NdotV, float roughness, uint32_t numSamples = 1024)
{
    NdotV = glm::max(NdotV, 0.001f);
    roughness = glm::max(roughness, 0.01f);
    Vec3 V{glm::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV};
    float scale = 0.0f;
    float bias = 0.0f;

    for (uint32_t s = 0; s < numSamples; ++s)
    {
        Vec2 xi = hammersleyCpu(s, numSamples);
        Vec3 H = importanceSampleGGXCpu(xi, roughness);
        Vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

        float NdotL = glm::max(L.z, 0.0f);
        float NdotH = glm::max(H.z, 0.0f);
        float VdotH = glm::max(glm::dot(V, H), 0.0f);

        if (NdotL > 0.0f)
        {
            float G = geometrySmithIBLCpu(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV + 1e-6f);
            float Fc = glm::pow(1.0f - VdotH, 5.0f);
            scale += (1.0f - Fc) * G_Vis;
            bias += Fc * G_Vis;
        }
    }

    return Vec2(scale / float(numSamples), bias / float(numSamples));
}

// ---------------------------------------------------------------------------
// Tests: IblResources headless — generateDefault() no crash
// ---------------------------------------------------------------------------

TEST_CASE("IblResources: generateDefault() does not crash in headless mode", "[ibl]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::IblResources ibl;

    // Must not throw or crash in Noop renderer.
    REQUIRE_NOTHROW(ibl.generateDefault());

    // In the Noop renderer bgfx cannot allocate real textures, so handles will
    // be invalid.  isValid() must still return a consistent bool without crash.
    bool valid = ibl.isValid();
    (void)valid;  // either outcome is acceptable in headless mode

    REQUIRE_NOTHROW(ibl.shutdown());
}

TEST_CASE("IblResources: shutdown() is safe on default-constructed object", "[ibl]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::IblResources ibl;
    // shutdown() on a never-generated object must be a no-op.
    REQUIRE_NOTHROW(ibl.shutdown());
}

TEST_CASE("IblResources: shutdown() is idempotent", "[ibl]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::IblResources ibl;
    ibl.generateDefault();
    REQUIRE_NOTHROW(ibl.shutdown());
    REQUIRE_NOTHROW(ibl.shutdown());  // second call must not double-free
}

// ---------------------------------------------------------------------------
// Tests: Hammersley sequence — values in [0,1]²
// ---------------------------------------------------------------------------

TEST_CASE("Hammersley: generates values in [0,1] x [0,1]", "[ibl]")
{
    constexpr uint32_t N = 64;
    for (uint32_t i = 0; i < N; ++i)
    {
        Vec2 xi = hammersleyCpu(i, N);
        REQUIRE(xi.x >= 0.0f);
        REQUIRE(xi.x <= 1.0f);
        REQUIRE(xi.y >= 0.0f);
        REQUIRE(xi.y <= 1.0f);
    }
}

TEST_CASE("Hammersley: first sample is (0, 0)", "[ibl]")
{
    Vec2 xi = hammersleyCpu(0, 1024);
    REQUIRE(xi.x == Approx(0.0f).margin(1e-6f));
    REQUIRE(xi.y == Approx(0.0f).margin(1e-6f));
}

// ---------------------------------------------------------------------------
// Tests: importanceSampleGGX
// ---------------------------------------------------------------------------

TEST_CASE("importanceSampleGGX: roughness=0 produces direction at (0,0,1)", "[ibl]")
{
    // With roughness → 0, the GGX lobe collapses to the specular direction.
    // For xi=(0,0) the half-vector should point straight up (0,0,1).
    Vec3 H = importanceSampleGGXCpu(Vec2(0.0f, 0.0f), 0.0f);
    REQUIRE(H.z == Approx(1.0f).margin(1e-4f));
    REQUIRE(glm::length(H) == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("importanceSampleGGX: output is unit length for various inputs", "[ibl]")
{
    constexpr uint32_t N = 32;
    for (uint32_t i = 0; i < N; ++i)
    {
        Vec2 xi = hammersleyCpu(i, N);
        Vec3 H = importanceSampleGGXCpu(xi, 0.5f);
        REQUIRE(glm::length(H) == Approx(1.0f).margin(1e-4f));
    }
}

// ---------------------------------------------------------------------------
// Tests: BRDF LUT integration — CPU-only, no bgfx
// ---------------------------------------------------------------------------

TEST_CASE("BRDF LUT: (NdotV=1, roughness=0) returns approximately (1,0)", "[ibl]")
{
    // Mirror reflection: for a perfectly smooth surface viewed straight on,
    // all energy is specular and there is no Fresnel bias term.
    Vec2 val = computeBrdfLut(1.0f, 0.0f);
    REQUIRE(val.x == Approx(1.0f).margin(0.05f));
    REQUIRE(val.y == Approx(0.0f).margin(0.05f));
}

TEST_CASE("BRDF LUT: (NdotV=1, roughness=1) scale+bias in [0,1] and scale < smooth", "[ibl]")
{
    // Maximally rough surface: both scale and bias must be in [0,1].
    // The scale at roughness=1 is smaller than at roughness=0 (less directional
    // reflection), and both together must not exceed 1.
    Vec2 rough = computeBrdfLut(1.0f, 1.0f);
    Vec2 smooth = computeBrdfLut(1.0f, 0.0f);

    REQUIRE(rough.x >= 0.0f);
    REQUIRE(rough.x <= 1.0f);
    REQUIRE(rough.y >= 0.0f);
    REQUIRE(rough.y <= 1.0f);
    // Rough surface has less directional (F0-scaled) reflection than smooth.
    REQUIRE(rough.x < smooth.x);
}

TEST_CASE("BRDF LUT: scale + bias are in [0,1] for all (NdotV, roughness)", "[ibl]")
{
    // Coarse grid check — integration should never overflow the [0,1] range.
    constexpr uint32_t steps = 8;
    for (uint32_t yi = 0; yi < steps; ++yi)
    {
        float NdotV = (float(yi) + 0.5f) / float(steps);
        for (uint32_t xi = 0; xi < steps; ++xi)
        {
            float roughness = (float(xi) + 0.5f) / float(steps);
            Vec2 val = computeBrdfLut(NdotV, roughness, 256);
            REQUIRE(val.x >= -0.01f);
            REQUIRE(val.x <= 1.01f);
            REQUIRE(val.y >= -0.01f);
            REQUIRE(val.y <= 1.01f);
        }
    }
}
