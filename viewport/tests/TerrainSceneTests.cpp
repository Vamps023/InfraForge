#include <doctest/doctest.h>

#include "infraforge/viewport/control/ControlProtocol.hpp"
#include "infraforge/viewport/renderer/TerrainScene.hpp"

#include <string>

namespace {

infraforge::viewport::TerrainScene validScene() {
    infraforge::viewport::TerrainScene scene;
    scene.originEasting = 500000.0;
    scene.originNorthing = 4650000.0;
    scene.originHeight = 120.0;
    scene.missingTiles = 3;
    scene.revision = 9;
    infraforge::viewport::TerrainSceneTile tile;
    tile.datasetUuid = "12345678-1234-5678-1234-567812345678";
    tile.datasetRevision = 4;
    tile.chunkX = -1;
    tile.chunkY = 2;
    tile.path = "D:/proj.iforge/cache/terrain/tile_-1_2.iforgetile";
    tile.minEasting = 500000.0;
    tile.minNorthing = 4650000.0;
    tile.maxEasting = 501000.0;
    tile.maxNorthing = 4651000.0;
    scene.tiles.push_back(tile);
    return scene;
}

} // namespace

TEST_SUITE("terrain scene projection") {

TEST_CASE("a valid scene round-trips through the control protocol") {
    const auto scene = validScene();

    // The shell forwards the scene as one JSON line of the same shape the
    // frontend produces from the engine's terrain.get_scene result.
    const std::string line =
        R"({"type":"scene","originEasting":500000.0,"originNorthing":4650000.0,"originHeight":120.0,)"
        R"("missingTiles":3,"revision":9,"tiles":[{"datasetUuid":"12345678-1234-5678-1234-567812345678",)"
        R"("datasetRevision":4,"chunkX":-1,"chunkY":2,"path":"D:/proj.iforge/cache/terrain/tile_-1_2.iforgetile",)"
        R"("minE":500000.0,"minN":4650000.0,"maxE":501000.0,"maxN":4651000.0}]})";

    const auto command = infraforge::viewport::parseControlCommand(line);
    REQUIRE(std::holds_alternative<infraforge::viewport::SceneCommand>(command));
    const auto& parsed = std::get<infraforge::viewport::SceneCommand>(command).scene;
    CHECK(parsed == scene);
}

TEST_CASE("malformed scenes are rejected explicitly") {
    const auto expectParseError = [](const std::string& line) {
        bool threw = false;
        try {
            (void)infraforge::viewport::parseControlCommand(line);
        } catch (const infraforge::viewport::CommandParseError&) {
            threw = true;
        }
        CHECK(threw);
    };

    expectParseError(R"({"type":"scene"})");
    expectParseError(R"({"type":"scene","originEasting":0,"originNorthing":0,"originHeight":0,)"
                     R"("missingTiles":0,"revision":0,"tiles":"not-an-array"})");
    expectParseError(R"({"type":"scene","originEasting":0,"originNorthing":0,"originHeight":0,)"
                     R"("missingTiles":0,"revision":0,"tiles":[{"datasetUuid":"x","datasetRevision":1,)"
                     R"("chunkX":0,"chunkY":0,"path":"","minE":0,"minN":0,"maxE":1,"maxN":1}]})");
    expectParseError(R"({"type":"scene","originEasting":0,"originNorthing":0,"originHeight":0,)"
                     R"("missingTiles":0,"revision":0,"tiles":[{"datasetUuid":"x","datasetRevision":1,)"
                     R"("chunkX":0,"chunkY":0,"path":"p","minE":2,"minN":0,"maxE":1,"maxN":1}]})");
}

TEST_CASE("scene without tiles is the empty-scene clear signal") {
    const std::string line =
        R"({"type":"scene","originEasting":0,"originNorthing":0,"originHeight":0,)"
        R"("missingTiles":0,"revision":0,"tiles":[]})";
    const auto command = infraforge::viewport::parseControlCommand(line);
    REQUIRE(std::holds_alternative<infraforge::viewport::SceneCommand>(command));
    CHECK(std::get<infraforge::viewport::SceneCommand>(command).scene.tiles.empty());
}

} // TEST_SUITE
