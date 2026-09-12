#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/domain/geo/GeoTransformService.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace {

namespace geo = infraforge::domain::geo;
using infraforge::testhelpers::sampleGeoreference;

geo::SourceSpatialReference source(std::string horizontal, std::string vertical = {}) {
    return geo::SourceSpatialReference{std::move(horizontal), std::move(vertical)};
}

// Expected transform values below are computed from published formulas,
// not from the library under test:
//
// - UTM zone 33N (EPSG:32633): lon 15 is the zone's central meridian, so
//   easting is exactly the false easting 500000 m. Northing is
//   k0 * M(55 deg), where M is the WGS84 meridional arc evaluated with the
//   standard e^6 series (EPSG Guidance Note 7-2): M(55 deg) =
//   6097230.3137 m -> northing 6094791.4216 m. The same series reproduces
//   the WGS84 quarter meridian (10001965.729 m) to ~0.2 mm, so a 1 mm
//   tolerance is strict and safe.
// - Web Mercator (EPSG:3857, EPSG method 1024): x = R * lon,
//   y = R * ln(tan(pi/4 + lat/2)), R = 6378137 m. lon 12 -> x
//   1335833.8895 m; lat 55 -> y 7361866.1131 m.
// - US survey foot: the international definition is exactly 1200/3937 m =
//   0.30480060960121924 m.

constexpr double kControlNorthing = 6094791.4216;
constexpr double kMetreTolerance = 0.001;   // 1 mm absolute
constexpr double kExactTolerance = 1e-6;    // micrometre absolute
constexpr double kRoundTripTolerance = 1e-8;

geo::GeoreferenceConfig utmConfig() {
    geo::GeoreferenceConfig config = sampleGeoreference();
    config.originEasting = 500000.0;
    config.originNorthing = kControlNorthing;
    return config;
}

} // namespace

TEST_SUITE("geo transform service") {

TEST_CASE("resolve validates and describes a projected metre CRS") {
    geo::GeoTransformService service;
    const auto resolved = service.resolveProjectGeoreference(utmConfig());

    CHECK(resolved.horizontalCrs.identifier == "EPSG:32633");
    CHECK(resolved.horizontalCrs.kind == geo::CrsKind::Projected);
    CHECK(std::abs(resolved.horizontalCrs.axisUnitToMetre - 1.0) < kExactTolerance);
    CHECK(resolved.horizontalCrs.name.find("UTM") != std::string::npos);
    CHECK(std::abs(resolved.linearUnit.toMetre - 1.0) < kExactTolerance);
    CHECK_FALSE(resolved.vertical.present);

    const auto canonical = service.canonicalizeConfig(utmConfig());
    CHECK(canonical.horizontalCrs == "EPSG:32633");
    CHECK(canonical.linearUnit == "metre");
}

TEST_CASE("canonicalize normalizes authority-resolvable definitions") {
    geo::GeoTransformService service;

    auto urn = utmConfig();
    urn.horizontalCrs = "urn:ogc:def:crs:EPSG::32633";
    CHECK(service.canonicalizeConfig(urn).horizontalCrs == "EPSG:32633");
}

TEST_CASE("invalid CRS definitions fail explicitly") {
    geo::GeoTransformService service;

    auto garbage = utmConfig();
    garbage.horizontalCrs = "not-a-crs";
    CHECK_THROWS_AS((void)service.resolveProjectGeoreference(garbage), const geo::GeoError&);

    auto empty = utmConfig();
    empty.horizontalCrs = "";
    CHECK_THROWS_AS((void)service.resolveProjectGeoreference(empty), const geo::GeoError&);

    CHECK_THROWS_AS((void)service.describeCrs("EPSG:9999999"), const geo::GeoError&);
}

TEST_CASE("a geographic project CRS is explicitly unsupported") {
    geo::GeoTransformService service;

    auto config = utmConfig();
    config.horizontalCrs = "EPSG:4326";
    try {
        (void)service.resolveProjectGeoreference(config);
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedCrs);
    }
}

TEST_CASE("unknown linear units fail explicitly") {
    geo::GeoTransformService service;

    auto config = utmConfig();
    config.linearUnit = "parsec-ish";
    try {
        (void)service.resolveProjectGeoreference(config);
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedUnit);
    }
}

TEST_CASE("vertical reference metadata resolves and non-vertical input is rejected") {
    geo::GeoTransformService service;

    auto config = utmConfig();
    config.verticalCrs = "EPSG:3855"; // EGM2008 height
    const auto resolved = service.resolveProjectGeoreference(config);
    REQUIRE(resolved.vertical.present);
    CHECK(resolved.vertical.identifier == "EPSG:3855");
    CHECK(resolved.vertical.transformSupported);
    CHECK(resolved.vertical.axisUnitToMetre > 0.0);

    auto wrongKind = utmConfig();
    wrongKind.verticalCrs = "EPSG:4326";
    try {
        (void)service.resolveProjectGeoreference(wrongKind);
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedCrs);
    }
}

TEST_CASE("source-to-project control point EPSG:4326 -> EPSG:32633") {
    geo::GeoTransformService service;
    const auto project = service.resolveProjectGeoreference(utmConfig());

    const auto position = service.sourceToProjectGlobal(
        project, source("EPSG:4326"), geo::GeoCoordinate{15.0, 55.0, 12.5});

    CHECK(std::abs(position.easting - 500000.0) < kExactTolerance);
    CHECK(std::abs(position.northing - kControlNorthing) < kMetreTolerance);
    // No vertical transform applies; the source height is in metres (no
    // source vertical CRS) and the project linear unit is the metre.
    CHECK(position.height == doctest::Approx(12.5));
}

TEST_CASE("heights convert from the source vertical axis unit into the project linear unit") {
    geo::GeoTransformService service;

    auto config = utmConfig();
    // NAVD88 height (ftUS): vertical CRS whose axis unit (0.3048006096 m)
    // differs from the project's metre linear unit.
    config.verticalCrs = "EPSG:6360";
    const auto project = service.resolveProjectGeoreference(config);
    REQUIRE(project.vertical.present);
    CHECK(project.vertical.axisUnitToMetre == doctest::Approx(0.30480060960121924).epsilon(1e-9));

    // Same vertical CRS on both sides: no datum transform is needed, but
    // the ftUS source height must convert into metres.
    const auto global = service.sourceToProjectGlobal(
        project, source("EPSG:4326", "EPSG:6360"), geo::GeoCoordinate{15.0, 55.0, 10.0});
    CHECK(std::abs(global.easting - 500000.0) < kExactTolerance);
    CHECK(std::abs(global.northing - kControlNorthing) < kMetreTolerance);
    CHECK(global.height == doctest::Approx(3.0480060960121924));

    // Inverse: project-global metres convert back into ftUS.
    const auto back = service.projectGlobalToSource(
        project, source("EPSG:4326", "EPSG:6360"), global);
    CHECK(back.x == doctest::Approx(15.0));
    CHECK(back.y == doctest::Approx(55.0));
    CHECK(back.z == doctest::Approx(10.0));
}

TEST_CASE("a source without a vertical CRS still converts into a non-metre linear unit") {
    geo::GeoTransformService service;

    auto config = utmConfig();
    config.linearUnit = "US survey foot";
    const auto project = service.resolveProjectGeoreference(config);
    REQUIRE(project.linearUnit.toMetre == doctest::Approx(0.30480060960121924));

    // No source vertical CRS: the source height is interpreted as metres
    // and converted into the project's US-survey-foot linear unit.
    const auto global = service.sourceToProjectGlobal(
        project, source("EPSG:4326"), geo::GeoCoordinate{15.0, 55.0, 0.30480060960121924});
    CHECK(global.height == doctest::Approx(1.0));
}

TEST_CASE("source-to-project control point EPSG:4326 -> EPSG:3857") {
    geo::GeoTransformService service;

    auto config = utmConfig();
    config.horizontalCrs = "EPSG:3857";
    const auto project = service.resolveProjectGeoreference(config);

    const auto position = service.sourceToProjectGlobal(
        project, source("EPSG:4326"), geo::GeoCoordinate{12.0, 55.0, 0.0});

    CHECK(std::abs(position.easting - 1335833.8895) < kMetreTolerance);
    CHECK(std::abs(position.northing - 7361866.1131) < kMetreTolerance);
}

TEST_CASE("project-global round-trips back to source coordinates") {
    geo::GeoTransformService service;
    const auto project = service.resolveProjectGeoreference(utmConfig());

    const geo::GeoCoordinate input{15.1234567, 55.7654321, 8.25};
    const auto global = service.sourceToProjectGlobal(project, source("EPSG:4326"), input);
    const auto back = service.projectGlobalToSource(project, source("EPSG:4326"), global);

    CHECK(std::abs(back.x - input.x) < kRoundTripTolerance);
    CHECK(std::abs(back.y - input.y) < kRoundTripTolerance);
    CHECK(back.z == doctest::Approx(input.z));
}

TEST_CASE("project-global space uses the canonical linear unit") {
    geo::GeoTransformService service;

    auto config = utmConfig();
    config.linearUnit = "kilometre";
    const auto project = service.resolveProjectGeoreference(config);

    CHECK(std::abs(project.linearUnit.toMetre - 1000.0) < kExactTolerance);
    const auto position = service.sourceToProjectGlobal(
        project, source("EPSG:4326"), geo::GeoCoordinate{15.0, 55.0, 0.0});
    CHECK(std::abs(position.easting - 500.0) < kExactTolerance);
    CHECK(std::abs(position.northing - kControlNorthing / 1000.0) < kMetreTolerance);
}

TEST_CASE("US survey foot axes expose the documented conversion factor") {
    geo::GeoTransformService service;

    const auto described = service.describeCrs("EPSG:2263");
    CHECK(described.kind == geo::CrsKind::Projected);
    CHECK(std::abs(described.axisUnitToMetre - 0.30480060960121924) < 1e-12);
}

TEST_CASE("project origin is applied only at the render-local boundary") {
    geo::GeoTransformService service;
    const auto project = service.resolveProjectGeoreference(utmConfig());

    const auto global = service.sourceToProjectGlobal(
        project, source("EPSG:4326"), geo::GeoCoordinate{15.0, 55.0, 0.0});

    // Project-global space is the CRS frame itself; the control point lands
    // exactly on the configured origin.
    const auto frame = service.renderLocalFrame(project);
    const auto local = frame.toRenderLocal(global);
    CHECK(std::abs(local.x) < kMetreTolerance);
    CHECK(std::abs(local.y) < kMetreTolerance);
    CHECK(local.z == doctest::Approx(0.0));

    const geo::ProjectGlobalPosition offset{500123.4, 6095100.2, 40.0};
    const auto localOffset = frame.toRenderLocal(offset);
    CHECK(localOffset.x == doctest::Approx(123.4));
    CHECK(localOffset.y == doctest::Approx(308.78));
    CHECK(localOffset.z == doctest::Approx(40.0));

    const auto back = frame.toProjectGlobal(localOffset);
    CHECK(back.easting == doctest::Approx(offset.easting));
    CHECK(back.northing == doctest::Approx(offset.northing));
    CHECK(back.height == doctest::Approx(offset.height));
}

TEST_CASE("non-finite coordinates are rejected explicitly") {
    geo::GeoTransformService service;
    const auto project = service.resolveProjectGeoreference(utmConfig());

    const double nan = std::numeric_limits<double>::quiet_NaN();
    try {
        (void)service.sourceToProjectGlobal(project, source("EPSG:4326"), {nan, 55.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::NotFinite);
    }
    try {
        (void)service.sourceToProjectGlobal(
            project, source("EPSG:4326"), {std::numeric_limits<double>::infinity(), 55.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::NotFinite);
    }
}

TEST_CASE("a transform whose best operation needs unavailable grids fails explicitly") {
    geo::GeoTransformService service;
    const auto project = service.resolveProjectGeoreference(utmConfig());

    // NAD27 -> WGS84 family: the best known operations require NADCON/NTv2
    // grids that are not part of the engine's PROJ deployment. The source
    // CRS itself resolves fine, so the failure must be the explicit
    // unsupported-transform path (ONLY_BEST=YES, ALLOW_BALLPARK=NO) rather
    // than a silently degraded or ballpark result.
    CHECK(service.describeCrs("EPSG:4267").kind == geo::CrsKind::Geographic);
    try {
        (void)service.sourceToProjectGlobal(
            project, source("EPSG:4267"), {15.0, 55.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedTransform);
    }
}

TEST_CASE("wrong-role source references are rejected explicitly") {
    geo::GeoTransformService service;
    const auto project = service.resolveProjectGeoreference(utmConfig());

    // A vertical CRS as the source horizontal reference resolves cleanly
    // but is the wrong role.
    try {
        (void)service.sourceToProjectGlobal(
            project, source("EPSG:3855"), {15.0, 55.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedCrs);
    }

    // A geographic CRS supplied as the source vertical reference.
    try {
        (void)service.sourceToProjectGlobal(
            project, source("EPSG:4326", "EPSG:4326"), {15.0, 55.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedCrs);
    }

    // A projected CRS supplied as the source vertical reference.
    try {
        (void)service.sourceToProjectGlobal(
            project, source("EPSG:4326", "EPSG:32633"), {15.0, 55.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedCrs);
    }

    // A geocentric source horizontal CRS: x/y/z all participate in the CRS
    // transformation, which this API does not support — reject rather than
    // feeding z=0 through a horizontal-only operation.
    try {
        (void)service.sourceToProjectGlobal(
            project, source("EPSG:4978"), {0.0, 0.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedCrs);
    }

    // A compound CRS as the source horizontal reference: the source
    // vertical belongs in source_vertical_crs, not in a compound string.
    try {
        (void)service.sourceToProjectGlobal(
            project, source("EPSG:4326+3855"), {15.0, 55.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedCrs);
    }

    // The same role checks apply to the inverse direction.
    try {
        (void)service.projectGlobalToSource(
            project, source("EPSG:4326", "EPSG:32633"), {500000.0, kControlNorthing, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedCrs);
    }

    // A real vertical CRS still works.
    auto config = utmConfig();
    config.verticalCrs = "EPSG:6360";
    const auto withVertical = service.resolveProjectGeoreference(config);
    const auto position = service.sourceToProjectGlobal(
        withVertical, source("EPSG:4326", "EPSG:6360"), geo::GeoCoordinate{15.0, 55.0, 10.0});
    CHECK(position.height == doctest::Approx(3.0480060960121924));
}

TEST_CASE("geocentric CRS reports its own kind") {
    geo::GeoTransformService service;
    const auto described = service.describeCrs("EPSG:4978");
    CHECK(described.kind == geo::CrsKind::Geocentric);
}

TEST_CASE("a source with no valid transformation fails as unsupported") {
    geo::GeoTransformService service;
    const auto project = service.resolveProjectGeoreference(utmConfig());

    // A datumless PROJ-string projected CRS can only reach the project
    // frame through a ballpark estimate; ballpark operations are disabled,
    // so the pair fails explicitly rather than returning a low-accuracy
    // result.
    try {
        (void)service.sourceToProjectGlobal(
            project,
            source("+proj=tmerc +lat_0=0 +lon_0=15 +k=0.9996 +x_0=500000 +y_0=0 +ellps=GRS80 +units=m +no_defs +type=crs"),
            {500000.0, 0.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedTransform);
    }
}

TEST_CASE("invalid source CRS fails as invalid rather than unsupported pair") {
    geo::GeoTransformService service;
    const auto project = service.resolveProjectGeoreference(utmConfig());

    try {
        (void)service.sourceToProjectGlobal(
            project, source("definitely-not-a-crs"), {1.0, 2.0, 0.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::InvalidCrs);
    }
}

TEST_CASE("vertical references compare on resolved identity, not raw text") {
    geo::GeoTransformService service;
    auto config = utmConfig();
    config.verticalCrs = "EPSG:6360"; // NAVD88 height in US survey feet
    const auto project = service.resolveProjectGeoreference(config);

    // The OGC URN form of the same CRS must be treated as the same
    // vertical reference: no datum transformation is built, but the source
    // axis-unit conversion still applies (10 ftUS -> 3.048 m).
    const auto position = service.sourceToProjectGlobal(
        project, source("EPSG:4326", "urn:ogc:def:crs:EPSG::6360"),
        geo::GeoCoordinate{15.0, 55.0, 10.0});
    CHECK(position.easting == doctest::Approx(500000.0).epsilon(kMetreTolerance / 500000.0));
    CHECK(position.northing == doctest::Approx(kControlNorthing).epsilon(kMetreTolerance / kControlNorthing));
    CHECK(position.height == doctest::Approx(3.0480060960121924));
}

TEST_CASE("compound transforms compose authority and WKT2 definitions") {
    geo::GeoTransformService service;
    auto config = utmConfig();
    config.verticalCrs = "EPSG:6360"; // NAVD88 height in US survey feet
    const auto project = service.resolveProjectGeoreference(config);

    // NAVD88 in metres (EPSG:5703) -> NAVD88 in US survey feet (EPSG:6360)
    // is a real compound CRS-to-CRS transformation that needs no grids.
    // Authority identifier source:
    const auto fwd = service.sourceToProjectGlobal(
        project, source("EPSG:4326", "EPSG:5703"), geo::GeoCoordinate{15.0, 55.0, 3.048});
    CHECK(fwd.easting == doctest::Approx(500000.0).epsilon(kMetreTolerance / 500000.0));
    CHECK(fwd.northing == doctest::Approx(kControlNorthing).epsilon(kMetreTolerance / kControlNorthing));
    CHECK(fwd.height == doctest::Approx(3.048).epsilon(0.001));

    const auto inv = service.projectGlobalToSource(
        project, source("EPSG:4326", "EPSG:5703"), fwd);
    CHECK(inv.x == doctest::Approx(15.0).epsilon(1e-9));
    CHECK(inv.y == doctest::Approx(55.0).epsilon(1e-9));
    CHECK(inv.z == doctest::Approx(3.048).epsilon(0.001));

    // The same compound path with a WKT2 horizontal definition — the
    // source CRS need not be an authority identifier.
    const std::string utm33Wkt =
        "PROJCRS[\"WGS 84 / UTM zone 33N\","
        "BASEGEOGCRS[\"WGS 84\",DATUM[\"World Geodetic System 1984\","
        "ELLIPSOID[\"WGS 84\",6378137,298.257223563]],UNIT[\"degree\",0.0174532925199433]],"
        "CONVERSION[\"UTM zone 33N\",METHOD[\"Transverse Mercator\"],"
        "PARAMETER[\"Latitude of natural origin\",0],PARAMETER[\"Longitude of natural origin\",15],"
        "PARAMETER[\"Scale factor at natural origin\",0.9996],"
        "PARAMETER[\"False easting\",500000],PARAMETER[\"False northing\",0]],"
        "CS[Cartesian,2],AXIS[\"easting\",east],AXIS[\"northing\",north],UNIT[\"metre\",1]]";

    const auto wktFwd = service.sourceToProjectGlobal(
        project, source(utm33Wkt, "EPSG:5703"), geo::GeoCoordinate{500000.0, kControlNorthing, 3.048});
    CHECK(wktFwd.easting == doctest::Approx(500000.0).epsilon(kMetreTolerance / 500000.0));
    CHECK(wktFwd.northing == doctest::Approx(kControlNorthing).epsilon(kMetreTolerance / kControlNorthing));
    CHECK(wktFwd.height == doctest::Approx(3.048).epsilon(0.001));

    const auto wktInv = service.projectGlobalToSource(
        project, source(utm33Wkt, "EPSG:5703"), wktFwd);
    CHECK(wktInv.x == doctest::Approx(500000.0).epsilon(kMetreTolerance / 500000.0));
    CHECK(wktInv.y == doctest::Approx(kControlNorthing).epsilon(kMetreTolerance / kControlNorthing));
    CHECK(wktInv.z == doctest::Approx(3.048).epsilon(0.001));
}

TEST_CASE("a compound transform needing unavailable grids fails explicitly") {
    geo::GeoTransformService service;
    auto config = utmConfig();
    config.verticalCrs = "EPSG:6360"; // NAVD88 height in US survey feet
    const auto project = service.resolveProjectGeoreference(config);

    // EGM2008 -> NAVD88 requires a geoid grid that is not part of this
    // deployment; the compound operation must fail explicitly as an
    // unsupported transformation (GEO_UNSUPPORTED at the command layer),
    // never as invalid input or a silently degraded result.
    try {
        (void)service.sourceToProjectGlobal(
            project, source("EPSG:4326", "EPSG:3855"), geo::GeoCoordinate{15.0, 55.0, 10.0});
        FAIL("expected GeoError");
    } catch (const geo::GeoError& error) {
        CHECK(error.code() == geo::GeoErrorCode::UnsupportedTransform);
    }
}

TEST_CASE("bound CRS definitions keep their encoded transformation") {
    geo::GeoTransformService service;

    // A bound CRS wraps a source CRS plus an explicit abridged
    // transformation to a hub datum; that transformation is part of the
    // definition. Even though the source carries an authority ID,
    // canonicalization must not collapse the definition to "EPSG:32633".
    const std::string boundUtm =
        "BOUNDCRS["
        "SOURCECRS[PROJCRS[\"WGS 84 / UTM zone 33N\","
        "BASEGEOGCRS[\"WGS 84\",DATUM[\"World Geodetic System 1984\","
        "ELLIPSOID[\"WGS 84\",6378137,298.257223563]],UNIT[\"degree\",0.0174532925199433]],"
        "CONVERSION[\"UTM zone 33N\",METHOD[\"Transverse Mercator\"],"
        "PARAMETER[\"Latitude of natural origin\",0],PARAMETER[\"Longitude of natural origin\",15],"
        "PARAMETER[\"Scale factor at natural origin\",0.9996],"
        "PARAMETER[\"False easting\",500000],PARAMETER[\"False northing\",0]],"
        "CS[Cartesian,2],AXIS[\"easting\",east],AXIS[\"northing\",north],UNIT[\"metre\",1],"
        "ID[\"EPSG\",32633]]],"
        "TARGETCRS[GEOGCRS[\"WGS 84\",DATUM[\"World Geodetic System 1984\","
        "ELLIPSOID[\"WGS 84\",6378137,298.257223563]],CS[ellipsoidal,2],"
        "AXIS[\"latitude\",north],AXIS[\"longitude\",east],UNIT[\"degree\",0.0174532925199433]]],"
        "ABRIDGEDTRANSFORMATION[\"Custom offset\","
        "METHOD[\"Geocentric translations (geog2D domain)\"],"
        "PARAMETER[\"X-axis translation\",1],PARAMETER[\"Y-axis translation\",2],"
        "PARAMETER[\"Z-axis translation\",3]]]";

    const auto described = service.describeCrs(boundUtm);
    CHECK(described.kind == geo::CrsKind::Projected);
    CHECK(described.identifier == "EPSG:32633");
    CHECK_FALSE(described.authoritativeIdentifier);

    auto config = utmConfig();
    config.horizontalCrs = boundUtm;
    const auto canonical = service.canonicalizeConfig(config);
    CHECK(canonical.horizontalCrs == boundUtm);

    // Equivalent-authority definitions still canonicalize as before.
    auto urn = utmConfig();
    urn.horizontalCrs = "urn:ogc:def:crs:EPSG::32633";
    CHECK(service.canonicalizeConfig(urn).horizontalCrs == "EPSG:32633");
}

TEST_CASE("canonical project coordinates are double precision") {
    static_assert(std::is_same_v<decltype(geo::ProjectGlobalPosition::easting), double>);
    static_assert(std::is_same_v<decltype(geo::ProjectGlobalPosition::northing), double>);
    static_assert(std::is_same_v<decltype(geo::ProjectGlobalPosition::height), double>);
    static_assert(std::is_same_v<decltype(geo::GeoCoordinate::x), double>);
    static_assert(std::is_same_v<decltype(geo::RenderLocalPosition::x), double>);
}

} // TEST_SUITE
