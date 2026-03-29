#include <catch2/catch_test_macros.hpp>

#include "engine/io/Json.h"

// Placeholder -- tests added in Phase 2.
TEST_CASE("Json placeholder compiles", "[json]")
{
    engine::io::JsonDocument doc;
    REQUIRE_FALSE(doc.hasError());
}
