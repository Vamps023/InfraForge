#include "infraforge/persistence/ProjectManifest.hpp"

#include "infraforge/ports/ProjectStore.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <compare>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "infraforge/runtime/Uuid.hpp"
#include "infraforge/version.hpp"

namespace infraforge::persistence {
namespace {

using nlohmann::ordered_json;

[[noreturn]] void fail(ports::StoreErrorCategory category, std::string message) {
    throw ports::StoreError(category, std::move(message));
}

[[noreturn]] void failFormat(std::string message) {
    fail(ports::StoreErrorCategory::FormatUnsupported, std::move(message));
}

struct SemanticVersion {
    int major{0};
    int minor{0};
    int patch{0};

    friend std::strong_ordering operator<=>(const SemanticVersion&, const SemanticVersion&) = default;
};

std::optional<SemanticVersion> parseSemanticVersion(std::string_view text) {
    const auto parseNumber = [](std::string_view segment) -> std::optional<int> {
        if (segment.empty() || segment.size() > 9) {
            return std::nullopt;
        }
        int value = 0;
        for (const char character : segment) {
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            value = value * 10 + (character - '0');
        }
        return value;
    };

    const std::size_t firstDot = text.find('.');
    if (firstDot == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t secondDot = text.find('.', firstDot + 1);
    if (secondDot == std::string_view::npos) {
        return std::nullopt;
    }
    if (text.find('.', secondDot + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    const auto major = parseNumber(text.substr(0, firstDot));
    const auto minor = parseNumber(text.substr(firstDot + 1, secondDot - firstDot - 1));
    const auto patch = parseNumber(text.substr(secondDot + 1));
    if (!major.has_value() || !minor.has_value() || !patch.has_value()) {
        return std::nullopt;
    }
    return SemanticVersion{*major, *minor, *patch};
}

bool isSupportedApplicationVersion(const std::string& minimumVersion) {
    const auto minimum = parseSemanticVersion(minimumVersion);
    const auto current = parseSemanticVersion(std::string{infraforge::kEngineVersion});
    if (!minimum.has_value() || !current.has_value()) {
        failFormat("manifest compatibility version is not a valid semantic version");
    }
    return *current >= *minimum;
}

bool isPlausibleTimestamp(std::string_view value) {
    // Light structural check: ISO 8601 UTC timestamps produced by this
    // engine ("YYYY-MM-DDTHH:MM:SSZ"). Full calendar validation is not the
    // manifest's job; writers are engine-controlled.
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T'
        || value[13] != ':' || value[16] != ':' || value.back() != 'Z') {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16 || index == 19) {
            continue;
        }
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

void validateTimestampField(const ordered_json& json, std::string_view key) {
    if (!json.at(key).is_string() || !isPlausibleTimestamp(json.at(key).get<std::string>())) {
        failFormat(std::string{"manifest field '"} + std::string{key} + "' is not a valid UTC timestamp");
    }
}

ProjectManifest manifestFromJson(const ordered_json& json) {
    static constexpr std::array<std::string_view, 8> kKnownKeys{
        "format", "formatVersion", "projectUuid", "displayName", "createdAt", "database", "compatibility",
        "georeference",
    };
    for (auto iterator = json.begin(); iterator != json.end(); ++iterator) {
        bool known = false;
        for (const std::string_view key : kKnownKeys) {
            known = known || iterator.key() == key;
        }
        if (!known) {
            failFormat("manifest contains unknown key '" + iterator.key() + "'");
        }
    }

    if (!json.at("format").is_string() || json.at("format").get<std::string>() != kProjectFormatId) {
        failFormat("manifest format identifier is not '" + std::string{kProjectFormatId} + "'");
    }
    if (!json.at("formatVersion").is_number_integer()) {
        failFormat("manifest formatVersion must be an integer");
    }
    const int formatVersion = json.at("formatVersion").get<int>();
    if (formatVersion > kProjectFormatVersion) {
        failFormat("project format version " + std::to_string(formatVersion)
            + " is newer than the supported version " + std::to_string(kProjectFormatVersion));
    }
    if (formatVersion != kProjectFormatVersion) {
        failFormat("project format version " + std::to_string(formatVersion) + " is not supported");
    }

    ProjectManifest manifest;
    manifest.formatVersion = formatVersion;

    manifest.projectUuid = json.at("projectUuid").get<std::string>();
    if (!runtime::isValidUuidText(manifest.projectUuid)) {
        failFormat("manifest projectUuid is not a canonical UUID");
    }
    manifest.displayName = json.at("displayName").get<std::string>();
    if (manifest.displayName.empty() || manifest.displayName.size() > 128) {
        failFormat("manifest displayName is missing or too long");
    }

    validateTimestampField(json, "createdAt");
    manifest.createdAt = json.at("createdAt").get<std::string>();

    const auto& database = json.at("database");
    if (!database.is_object() || database.size() != 1 || !database.at("path").is_string()) {
        failFormat("manifest database section is malformed");
    }
    manifest.databasePath = database.at("path").get<std::string>();
    if (manifest.databasePath != kDatabaseFileName) {
        failFormat("manifest database path must be '" + std::string{kDatabaseFileName} + "'");
    }

    // The database owns the schema version; the manifest only records the
    // minimum application version so future engines can migrate on open.
    const auto& compatibility = json.at("compatibility");
    if (!compatibility.is_object() || compatibility.size() != 1
        || !compatibility.at("minimumApplicationVersion").is_string()) {
        failFormat("manifest compatibility section is malformed");
    }
    manifest.minimumApplicationVersion = compatibility.at("minimumApplicationVersion").get<std::string>();
    if (!isSupportedApplicationVersion(manifest.minimumApplicationVersion)) {
        failFormat("project requires application version " + manifest.minimumApplicationVersion
            + " or newer; this engine is " + std::string{infraforge::kEngineVersion});
    }

    const auto& georeference = json.at("georeference");
    static constexpr std::array<std::string_view, 7> kGeoKeys{
        "horizontalCrs", "linearUnit", "axisConvention", "originEasting", "originNorthing",
        "originHeight", "verticalCrs",
    };
    if (!georeference.is_object()) {
        failFormat("manifest georeference section is malformed");
    }
    for (auto iterator = georeference.begin(); iterator != georeference.end(); ++iterator) {
        bool known = false;
        for (const std::string_view key : kGeoKeys) {
            known = known || iterator.key() == key;
        }
        if (!known) {
            failFormat("manifest georeference contains unknown key '" + iterator.key() + "'");
        }
    }
    if (!georeference.at("horizontalCrs").is_string() || !georeference.at("linearUnit").is_string()
        || !georeference.at("axisConvention").is_string() || !georeference.at("originEasting").is_number()
        || !georeference.at("originNorthing").is_number() || !georeference.at("verticalCrs").is_string()) {
        failFormat("manifest georeference fields have invalid types");
    }
    manifest.georeference.horizontalCrs = georeference.at("horizontalCrs").get<std::string>();
    manifest.georeference.linearUnit = georeference.at("linearUnit").get<std::string>();
    const auto axisConvention =
        domain::geo::axisConventionFromName(georeference.at("axisConvention").get<std::string>());
    if (!axisConvention.has_value()) {
        failFormat("manifest georeference axisConvention is not recognized");
    }
    manifest.georeference.axisConvention = *axisConvention;
    manifest.georeference.originEasting = georeference.at("originEasting").get<double>();
    manifest.georeference.originNorthing = georeference.at("originNorthing").get<double>();
    // originHeight joins the format within version 1: manifests written
    // before the field existed carry the schema default of 0.
    manifest.georeference.originHeight = georeference.contains("originHeight")
        ? georeference.at("originHeight").get<double>()
        : 0.0;
    manifest.georeference.verticalCrs = georeference.at("verticalCrs").get<std::string>();

    if (const auto error = domain::geo::validateGeoreference(manifest.georeference); error.has_value()) {
        failFormat("manifest georeference is invalid: " + error->message);
    }
    return manifest;
}

} // namespace

ProjectManifest readProjectManifest(const std::filesystem::path& projectDirectory) {
    if (!std::filesystem::is_directory(projectDirectory)) {
        fail(ports::StoreErrorCategory::DirectoryInvalid,
            "project directory does not exist: " + projectDirectory.string());
    }

    const auto manifestFile = projectDirectory / kManifestFileName;
    std::error_code ioError;
    if (!std::filesystem::is_regular_file(manifestFile, ioError)) {
        fail(ports::StoreErrorCategory::DirectoryInvalid,
            "not an InfraForge project directory (missing " + std::string{kManifestFileName} + "): "
                + projectDirectory.string());
    }

    std::ifstream input(manifestFile, std::ios::binary);
    if (!input.is_open()) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "cannot read project manifest: " + manifestFile.string());
    }

    ordered_json json;
    try {
        input >> json;
        if (!json.is_object()) {
            failFormat("project manifest must be a JSON object");
        }
        return manifestFromJson(json);
    } catch (const nlohmann::json::exception& error) {
        failFormat(std::string{"project manifest is not valid JSON: "} + error.what());
    }
}

void writeProjectManifest(const std::filesystem::path& projectDirectory, const ProjectManifest& manifest) {
    ordered_json georeference;
    georeference["horizontalCrs"] = manifest.georeference.horizontalCrs;
    georeference["linearUnit"] = manifest.georeference.linearUnit;
    georeference["axisConvention"] = domain::geo::axisConventionName(manifest.georeference.axisConvention);
    georeference["originEasting"] = manifest.georeference.originEasting;
    georeference["originNorthing"] = manifest.georeference.originNorthing;
    georeference["originHeight"] = manifest.georeference.originHeight;
    georeference["verticalCrs"] = manifest.georeference.verticalCrs;

    ordered_json database;
    database["path"] = manifest.databasePath;

    ordered_json compatibility;
    compatibility["minimumApplicationVersion"] = manifest.minimumApplicationVersion;

    ordered_json json;
    json["format"] = kProjectFormatId;
    json["formatVersion"] = manifest.formatVersion;
    json["projectUuid"] = manifest.projectUuid;
    json["displayName"] = manifest.displayName;
    json["createdAt"] = manifest.createdAt;
    json["database"] = std::move(database);
    json["compatibility"] = std::move(compatibility);
    json["georeference"] = std::move(georeference);

    const auto manifestFile = projectDirectory / kManifestFileName;
    const auto temporaryFile = projectDirectory / (std::string{kManifestFileName} + ".tmp");

    {
        std::ofstream output(temporaryFile, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "cannot write project manifest: " + temporaryFile.string());
        }
        output << json.dump(2) << '\n';
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code discardError;
            (void)std::filesystem::remove(temporaryFile, discardError);
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "failed to fully write project manifest: " + temporaryFile.string());
        }
    }

    std::error_code renameError;
    std::filesystem::rename(temporaryFile, manifestFile, renameError);
    if (renameError) {
        std::error_code discardError;
        (void)std::filesystem::remove(temporaryFile, discardError);
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "cannot finalize project manifest: " + manifestFile.string());
    }
}

} // namespace infraforge::persistence
