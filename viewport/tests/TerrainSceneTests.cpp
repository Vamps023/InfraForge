#include <doctest/doctest.h>

#include "infraforge/viewport/control/ControlProtocol.hpp"
#include "infraforge/viewport/renderer/TerrainScene.hpp"

#include <string>
#include <limits>
#include <filesystem>
#include <fstream>
#include <sstream>

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

TEST_CASE("shared TypeScript/native road-only scene fixture parses losslessly") {
    const auto fixture = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path()
        / "contracts" / "testdata" / "road-scene-control.json";
    std::ifstream stream{fixture};
    REQUIRE(stream.good());
    std::ostringstream json;
    json << stream.rdbuf();
    const auto command = infraforge::viewport::parseControlCommand(json.str());
    const auto* scene = std::get_if<infraforge::viewport::SceneCommand>(&command);
    REQUIRE(scene != nullptr);
    CHECK_FALSE(scene->terrain.has_value());
    REQUIRE(scene->roads.has_value());
    CHECK(scene->roads->revision == 9007199254740993ULL);
    REQUIRE(scene->roads->meshes.size() == 1);
    CHECK(scene->roads->meshes[0].chunkX == 12);
    CHECK(scene->roads->meshes[0].chunkY == -4);
}

TEST_CASE("a valid scene round-trips through the control protocol") {
    const auto scene = validScene();

    // The shell forwards the scene as one JSON line of the same shape the
    // frontend produces from the engine's terrain.get_scene result.
    const std::string line =
        R"({"type":"scene","terrain":{"originEasting":500000.0,"originNorthing":4650000.0,"originHeight":120.0,)"
        R"("missingTiles":3,"revision":9,"tiles":[{"datasetUuid":"12345678-1234-5678-1234-567812345678",)"
        R"("datasetRevision":4,"chunkX":-1,"chunkY":2,"path":"D:/proj.iforge/cache/terrain/tile_-1_2.iforgetile",)"
        R"("minE":500000.0,"minN":4650000.0,"maxE":501000.0,"maxN":4651000.0}]}})";

    const auto command = infraforge::viewport::parseControlCommand(line);
    REQUIRE(std::holds_alternative<infraforge::viewport::SceneCommand>(command));
    const auto& parsed = std::get<infraforge::viewport::SceneCommand>(command).terrain;
    REQUIRE(parsed.has_value());
    CHECK(*parsed == scene);
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
    expectParseError(R"({"type":"scene","terrain":{"originEasting":0,"originNorthing":0,"originHeight":0,)"
                     R"("missingTiles":0,"revision":0,"tiles":"not-an-array"}})");
    expectParseError(R"({"type":"scene","terrain":{"originEasting":0,"originNorthing":0,"originHeight":0,)"
                     R"("missingTiles":0,"revision":0,"tiles":[{"datasetUuid":"x","datasetRevision":1,)"
                     R"("chunkX":0,"chunkY":0,"path":"","minE":0,"minN":0,"maxE":1,"maxN":1}]}})");
    expectParseError(R"({"type":"scene","terrain":{"originEasting":0,"originNorthing":0,"originHeight":0,)"
                     R"("missingTiles":0,"revision":0,"tiles":[{"datasetUuid":"x","datasetRevision":1,)"
                     R"("chunkX":0,"chunkY":0,"path":"p","minE":2,"minN":0,"maxE":1,"maxN":1}]}})");
}

TEST_CASE("scene without tiles is the empty-scene clear signal") {
    const std::string line =
        R"({"type":"scene","terrain":{"originEasting":0,"originNorthing":0,"originHeight":0,)"
        R"("missingTiles":0,"revision":0,"tiles":[]}})";
    const auto command = infraforge::viewport::parseControlCommand(line);
    REQUIRE(std::holds_alternative<infraforge::viewport::SceneCommand>(command));
    const auto& terrain = std::get<infraforge::viewport::SceneCommand>(command).terrain;
    REQUIRE(terrain.has_value());
    CHECK(terrain->tiles.empty());
}

TEST_CASE("road-only and combined deltas preserve domain presence") {
    const std::string road =
        R"({"type":"scene","roads":{"originEasting":0,"originNorthing":0,"originHeight":0,)"
        R"("roadRevision":"18446744073709551615","roads":[{"roadId":"road-1",)"
        R"("vertices":[{"x":0,"y":0,"z":0},{"x":1,"y":0,"z":0},{"x":0,"y":1,"z":0}],)"
        R"("indices":[0,1,2]}]}})";
    const auto roadCommand = std::get<infraforge::viewport::SceneCommand>(
        infraforge::viewport::parseControlCommand(road));
    CHECK_FALSE(roadCommand.terrain.has_value());
    REQUIRE(roadCommand.roads.has_value());
    CHECK_EQ(roadCommand.roads->revision, std::numeric_limits<std::uint64_t>::max());

    const std::string combined =
        R"({"type":"scene","terrain":{"originEasting":0,"originNorthing":0,"originHeight":0,)"
        R"("missingTiles":"0","revision":"1","tiles":[]},"roads":{"originEasting":0,)"
        R"("originNorthing":0,"originHeight":0,"roadRevision":"2","roads":[]}})";
    const auto both = std::get<infraforge::viewport::SceneCommand>(
        infraforge::viewport::parseControlCommand(combined));
    CHECK(both.terrain.has_value());
    CHECK(both.roads.has_value());
}

TEST_CASE("road GPU vertices use the terrain and camera northing handedness") {
    const infraforge::viewport::RoadSceneVertex wireVertex{
        .x = 12.0f, .y = 34.0f, .z = 5.0f,
        .nx = 0.25f, .ny = -0.5f, .nz = 0.75f};
    const auto renderVertex = infraforge::viewport::roadVertexBufferData(wireVertex);

    CHECK(renderVertex == std::array<float, 6>{12.0f, -34.0f, 5.0f, 0.25f, 0.5f, 0.75f});
    CHECK(wireVertex.y == 34.0f);
    CHECK(wireVertex.ny == -0.5f);
}

TEST_CASE("malformed road delta is rejected without requiring terrain") {
    CHECK_THROWS_AS(static_cast<void>(infraforge::viewport::parseControlCommand(
        R"({"type":"scene","roads":{"roadRevision":"x","roads":[]}})")),
        infraforge::viewport::CommandParseError);
}

} // TEST_SUITE
