#include <nuf/geometry/Kernel.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <stdexcept>

TEST_CASE("OCCT está enlazado y construye sólidos", "[geometry]")
{
    REQUIRE_FALSE(nuf::geometry::kernelVersion().empty());
    REQUIRE_THAT(nuf::geometry::probeBoxVolume(10.0, 20.0, 30.0),
                 Catch::Matchers::WithinAbs(6000.0, 1e-6));
}

TEST_CASE("Dimensiones no positivas se rechazan", "[geometry]")
{
    REQUIRE_THROWS_AS(nuf::geometry::probeBoxVolume(0.0, 1.0, 1.0), std::invalid_argument);
    REQUIRE_THROWS_AS(nuf::geometry::probeBoxVolume(1.0, -1.0, 1.0), std::invalid_argument);
}
