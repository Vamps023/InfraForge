#include "infraforge/domain/geo/GeoTransformService.hpp"

#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/runtime/Logging.hpp"

#include <proj.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace infraforge::domain::geo {
namespace {

[[noreturn]] void fail(const GeoErrorCode code, std::string message) {
    throw GeoError(code, std::move(message));
}

// Directory containing the running executable; used to locate a deployed
// "share/proj" data directory next to the engine.
std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path{buffer}.parent_path();
#else
    std::error_code ioError;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ioError);
    if (ioError) {
        return {};
    }
    return self.parent_path();
#endif
}

bool hasProjDatabase(const std::filesystem::path& directory) {
    std::error_code ioError;
    return !directory.empty() && std::filesystem::is_regular_file(directory / "proj.db", ioError);
}

// std::getenv is deprecated on MSVC; _dupenv_s is the portable-safe variant.
std::optional<std::string> environmentVariable(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result{value};
    free(value);
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string{value};
#endif
}

// Candidate geospatial data directories in priority order. PROJ itself also
// honours the PROJ_DATA/PROJ_LIB environment variables; the explicit list
// additionally covers the compiled-in vcpkg layout and deployment layouts.
std::vector<std::string> dataDirectoryCandidates() {
    std::vector<std::string> candidates;
    const auto addIfPresent = [&candidates](const std::filesystem::path& directory) {
        if (hasProjDatabase(directory)) {
            candidates.push_back(runtime::utf8String(directory));
        }
    };
    if (const auto env = environmentVariable("PROJ_DATA"); env.has_value()) {
        addIfPresent(runtime::pathFromUtf8(*env));
    }
    if (const auto env = environmentVariable("PROJ_LIB"); env.has_value()) {
        addIfPresent(runtime::pathFromUtf8(*env));
    }
#ifdef INFRAFORGE_PROJ_DATA_DIR
    addIfPresent(runtime::pathFromUtf8(INFRAFORGE_PROJ_DATA_DIR));
#endif
    addIfPresent(executableDirectory() / "share" / "proj");
    addIfPresent(executableDirectory().parent_path() / "share" / "proj");
    return candidates;
}

CrsKind mapCrsType(const PJ_TYPE type) {
    switch (type) {
    case PJ_TYPE_PROJECTED_CRS:
    case PJ_TYPE_DERIVED_PROJECTED_CRS:
        return CrsKind::Projected;
    case PJ_TYPE_ENGINEERING_CRS:
        return CrsKind::Engineering;
    case PJ_TYPE_GEOCENTRIC_CRS:
        return CrsKind::Geocentric;
    case PJ_TYPE_GEODETIC_CRS:
    case PJ_TYPE_GEOGRAPHIC_CRS:
    case PJ_TYPE_GEOGRAPHIC_2D_CRS:
    case PJ_TYPE_GEOGRAPHIC_3D_CRS:
        return CrsKind::Geographic;
    case PJ_TYPE_VERTICAL_CRS:
        return CrsKind::Vertical;
    case PJ_TYPE_COMPOUND_CRS:
        return CrsKind::Compound;
    default:
        return CrsKind::Other;
    }
}

bool hasLinearAxisUnit(const CrsKind kind) {
    return kind == CrsKind::Projected || kind == CrsKind::Engineering || kind == CrsKind::Vertical;
}

std::string normalizeUnitKey(const std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const char character : text) {
        if (character != ' ' && character != '_' && character != '-') {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    return normalized;
}

} // namespace

struct GeoTransformService::Impl {
    // RAII owner of one PROJ context plus the lazily created coordinate
    // operation objects. PROJ contexts are not thread-safe; the owning
    // service instance is bound to the application executor thread.
    PJ_CONTEXT* context{nullptr};
    std::unordered_map<std::string, PJ*> transforms;

    ~Impl() {
        for (const auto& [key, transform] : transforms) {
            proj_destroy(transform);
        }
        if (context != nullptr) {
            proj_context_destroy(context);
        }
    }

    [[nodiscard]] std::string lastError(const std::string_view operation) const {
        const int errorCode = proj_context_errno(context);
        const char* text = proj_context_errno_string(context, errorCode);
        return std::string{operation} + ": " + (text != nullptr ? text : "unknown geospatial engine error");
    }

    // Resolves a single CRS definition (EPSG/URN/WKT2/PROJJSON/PROJ string)
    // into a PROJ object. The caller owns the returned object.
    [[nodiscard]] PJ* createCrs(const std::string& definition, const char* role) {
        PJ* crs = proj_create(context, definition.c_str());
        if (crs == nullptr) {
            fail(GeoErrorCode::InvalidCrs,
                lastError(std::string{"cannot resolve "} + role + " CRS '" + definition + "'"));
        }
        return crs;
    }

    // Composes a compound horizontal+vertical CRS through PROJ's object API
    // (proj_create_compound_crs) rather than string concatenation, so any
    // accepted definition syntax — authority identifier, OGC URN, WKT2,
    // PROJJSON — can take part in a vertical transformation.
    [[nodiscard]] PJ* createCompoundCrs(
        const std::string& horizontalDefinition,
        const std::string& verticalDefinition) {
        PJ* horizontal = createCrs(horizontalDefinition, "source");
        PJ* vertical = nullptr;
        PJ* compound = nullptr;
        try {
            vertical = createCrs(verticalDefinition, "vertical");
            compound = proj_create_compound_crs(context, "infraforge source compound", horizontal, vertical);
        } catch (...) {
            proj_destroy(horizontal);
            proj_destroy(vertical);
            throw;
        }
        proj_destroy(horizontal);
        proj_destroy(vertical);
        if (compound == nullptr) {
            fail(GeoErrorCode::UnsupportedTransform,
                lastError("cannot compose compound CRS from '" + horizontalDefinition + "' + '"
                    + verticalDefinition + "'"));
        }
        return compound;
    }

    // True when two vertical CRS definitions denote the same reference —
    // for example "EPSG:6360" and its OGC URN form. Compared on resolved
    // PROJ objects, not raw strings.
    [[nodiscard]] bool crsEquivalent(
        const std::string& definitionA,
        const std::string& definitionB) {
        if (definitionA == definitionB) {
            return true;
        }
        PJ* a = createCrs(definitionA, "vertical");
        PJ* b = createCrs(definitionB, "vertical");
        const int equivalent =
            proj_is_equivalent_to_with_ctx(context, a, b, PJ_COMP_EQUIVALENT);
        proj_destroy(a);
        proj_destroy(b);
        return equivalent != 0;
    }

    // The transform cache is keyed by the participating CRS definitions;
    // each entry is an axis-order-normalized PROJ coordinate operation
    // usable in both directions via proj_trans.
    //
    // Operation selection is strict: ALLOW_BALLPARK=NO forbids low-accuracy
    // ballpark fallbacks, and ONLY_BEST=YES makes operation creation itself
    // fail when the best known transformation cannot be instantiated (for
    // example when a required grid is unavailable). The engine never serves
    // silently degraded coordinates.
    [[nodiscard]] PJ* requireTransformObjects(
        const std::string& cacheKey,
        const std::string& sourceLabel,
        const std::string& targetLabel,
        PJ* sourceCrs,
        PJ* targetCrs) {
        if (const auto existing = transforms.find(cacheKey); existing != transforms.end()) {
            return existing->second;
        }

        static constexpr const char* kOperationOptions[] = {
            "ALLOW_BALLPARK=NO",
            "ONLY_BEST=YES",
            nullptr,
        };
        PJ* operation = proj_create_crs_to_crs_from_pj(
            context, sourceCrs, targetCrs, nullptr, kOperationOptions);
        if (operation == nullptr) {
            fail(GeoErrorCode::UnsupportedTransform,
                lastError("no supported transformation between '" + sourceLabel + "' and '"
                    + targetLabel
                    + "' (best transformation unavailable; ballpark fallbacks are disabled)"));
        }
        PJ* normalized = proj_normalize_for_visualization(context, operation);
        proj_destroy(operation);
        if (normalized == nullptr) {
            fail(GeoErrorCode::UnsupportedTransform,
                lastError("cannot normalize transformation between '" + sourceLabel + "' and '"
                    + targetLabel + "'"));
        }
        transforms.emplace(cacheKey, normalized);
        return normalized;
    }

    [[nodiscard]] PJ* requireTransform(
        const std::string& sourceDefinition,
        const std::string& targetDefinition) {
        const std::string key = sourceDefinition + '\x1f' + targetDefinition;
        PJ* sourceCrs = createCrs(sourceDefinition, "source");
        PJ* targetCrs = nullptr;
        PJ* transform = nullptr;
        try {
            targetCrs = createCrs(targetDefinition, "target");
            transform = requireTransformObjects(
                key, sourceDefinition, targetDefinition, sourceCrs, targetCrs);
        } catch (...) {
            proj_destroy(sourceCrs);
            proj_destroy(targetCrs);
            throw;
        }
        proj_destroy(sourceCrs);
        proj_destroy(targetCrs);
        return transform;
    }

    // Compound horizontal+vertical transform. The cache key spans all four
    // constituent definitions.
    [[nodiscard]] PJ* requireCompoundTransform(
        const std::string& sourceHorizontal,
        const std::string& sourceVertical,
        const std::string& targetHorizontal,
        const std::string& targetVertical) {
        const std::string key =
            sourceHorizontal + '\x1f' + sourceVertical + '\x1f' + targetHorizontal + '\x1f' + targetVertical;
        if (const auto existing = transforms.find(key); existing != transforms.end()) {
            return existing->second;
        }
        PJ* source = createCompoundCrs(sourceHorizontal, sourceVertical);
        PJ* target = nullptr;
        PJ* transform = nullptr;
        try {
            target = createCompoundCrs(targetHorizontal, targetVertical);
            transform = requireTransformObjects(
                key,
                sourceHorizontal + " + " + sourceVertical,
                targetHorizontal + " + " + targetVertical,
                source, target);
        } catch (...) {
            proj_destroy(source);
            proj_destroy(target);
            throw;
        }
        proj_destroy(source);
        proj_destroy(target);
        return transform;
    }
};

GeoTransformService::GeoTransformService()
    : impl_(std::make_unique<Impl>()) {
    impl_->context = proj_context_create();
    if (impl_->context == nullptr) {
        fail(GeoErrorCode::LibraryFailure, "cannot create geospatial engine context");
    }

    const std::vector<std::string> candidates = dataDirectoryCandidates();
    if (!candidates.empty()) {
        std::vector<const char*> paths;
        paths.reserve(candidates.size());
        for (const std::string& candidate : candidates) {
            paths.push_back(candidate.c_str());
        }
        proj_context_set_search_paths(impl_->context, static_cast<int>(paths.size()), paths.data());
    }
    if (proj_context_get_database_path(impl_->context) == nullptr) {
        fail(GeoErrorCode::LibraryUnavailable,
            "geospatial runtime database proj.db was not found (checked PROJ_DATA/PROJ_LIB, "
            "the compiled-in dependency layout, and executable-relative share/proj)");
    }
}

GeoTransformService::~GeoTransformService() = default;

ResolvedCrs GeoTransformService::describeCrs(const std::string_view definition) const {
    const std::string text{definition};
    PJ* raw = proj_create(impl_->context, text.c_str());
    if (raw == nullptr) {
        fail(GeoErrorCode::InvalidCrs,
            impl_->lastError("cannot resolve CRS definition '" + text + "'"));
    }

    // Bound CRSs wrap a hub datum (typically WGS 84); unwrap to the real
    // source CRS for kind/axis inspection. Transformation creation still
    // uses the original definition — and the original object is kept for
    // the canonical-equivalence check below.
    PJ* unwrapped = nullptr;
    PJ* crs = raw;
    if (proj_get_type(crs) == PJ_TYPE_BOUND_CRS) {
        unwrapped = proj_get_source_crs(impl_->context, raw);
        if (unwrapped == nullptr) {
            proj_destroy(raw);
            fail(GeoErrorCode::InvalidCrs,
                "cannot unwrap bound CRS definition '" + text + "'");
        }
        crs = unwrapped;
    }

    ResolvedCrs resolved;
    resolved.definition = text;
    resolved.kind = mapCrsType(proj_get_type(crs));
    if (const char* name = proj_get_name(crs); name != nullptr) {
        resolved.name = name;
    }
    const char* authority = proj_get_id_auth_name(crs, 0);
    const char* code = proj_get_id_code(crs, 0);
    if (authority != nullptr && code != nullptr) {
        resolved.authority = authority;
        resolved.code = code;
        resolved.identifier = std::string{authority} + ':' + code;
    }

    PJ* coordinateSystem = proj_crs_get_coordinate_system(impl_->context, crs);
    if (coordinateSystem != nullptr) {
        const char* unitName = nullptr;
        double unitConversion = 0.0;
        // Axis 0 carries the horizontal unit for projected/engineering
        // frames and the vertical unit for vertical frames.
        if (proj_cs_get_axis_info(impl_->context, coordinateSystem, 0,
                nullptr, nullptr, nullptr, &unitConversion, &unitName, nullptr, nullptr)
            == 1
            && hasLinearAxisUnit(resolved.kind)) {
            resolved.axisUnitToMetre = unitConversion;
        }
        proj_destroy(coordinateSystem);
    }

    // The identifier may only substitute the original definition when the
    // original object is equivalent to the authority CRS — e.g. an OGC
    // URN for the same entry. A bound CRS carries an explicit abridged
    // transformation that would be silently lost by substituting the
    // source CRS's identifier, and a custom WKT/PROJJSON may differ from
    // the authority entry despite carrying an ID element.
    if (!resolved.identifier.empty()) {
        PJ* authorityCrs = proj_create(impl_->context, resolved.identifier.c_str());
        if (authorityCrs != nullptr) {
            resolved.authoritativeIdentifier = proj_is_equivalent_to_with_ctx(
                impl_->context, raw, authorityCrs, PJ_COMP_EQUIVALENT) != 0;
            proj_destroy(authorityCrs);
        }
    }
    proj_destroy(crs);
    if (unwrapped != nullptr) {
        proj_destroy(raw);
    }
    return resolved;
}

namespace {

// Resolves a configured linear-unit identifier against the geospatial unit
// database. Matching accepts the canonical unit name, its short symbol, or
// an "AUTH:CODE" reference; comparison ignores case, spaces, underscores,
// and hyphens.
ResolvedUnit resolveLinearUnit(PJ_CONTEXT* context, const std::string_view unitText) {
    const std::string wanted = normalizeUnitKey(unitText);
    int count = 0;
    // All authorities, linear unit-of-measure category only.
    PROJ_UNIT_INFO** units =
        proj_get_units_from_database(context, nullptr, "linear", false, &count);
    if (units == nullptr) {
        fail(GeoErrorCode::LibraryFailure, "cannot query the geospatial unit database");
    }

    ResolvedUnit resolved;
    bool found = false;
    for (int index = 0; index < count && !found; ++index) {
        const PROJ_UNIT_INFO* unit = units[index];
        const std::string authority = unit->auth_name != nullptr ? unit->auth_name : "";
        const std::string code = unit->code != nullptr ? unit->code : "";
        const bool codeMatch = !code.empty()
            && (unitText == code || unitText == authority + ':' + code);
        const bool nameMatch = (unit->name != nullptr && normalizeUnitKey(unit->name) == wanted)
            || (unit->proj_short_name != nullptr && normalizeUnitKey(unit->proj_short_name) == wanted);
        if (codeMatch || nameMatch) {
            resolved.name = unit->name != nullptr ? unit->name : std::string{unitText};
            resolved.toMetre = unit->conv_factor;
            found = true;
        }
    }
    proj_unit_list_destroy(units);

    if (!found) {
        fail(GeoErrorCode::UnsupportedUnit,
            "linear unit '" + std::string{unitText} + "' is not a known linear unit of measure");
    }
    if (!(resolved.toMetre > 0.0) || !std::isfinite(resolved.toMetre)) {
        fail(GeoErrorCode::UnsupportedUnit,
            "linear unit '" + std::string{unitText} + "' does not convert to a length");
    }
    return resolved;
}

} // namespace

ProjectGeoreference GeoTransformService::resolveProjectGeoreference(const GeoreferenceConfig& config) const {
    if (const auto error = validateGeoreference(config); error.has_value()) {
        const GeoErrorCode code = error->field.find("origin") != std::string::npos
            ? GeoErrorCode::NotFinite
            : error->field.find("linear_unit") != std::string::npos
                ? GeoErrorCode::UnsupportedUnit
                : GeoErrorCode::InvalidCrs;
        fail(code, error->field + ": " + error->message);
    }

    ProjectGeoreference resolved;
    resolved.horizontalCrs = describeCrs(config.horizontalCrs);
    if (resolved.horizontalCrs.kind != CrsKind::Projected && resolved.horizontalCrs.kind != CrsKind::Engineering) {
        fail(GeoErrorCode::UnsupportedCrs,
            "project horizontal CRS '" + config.horizontalCrs + "' resolves to "
                + std::string{crsKindName(resolved.horizontalCrs.kind)}
                + "; a projected or engineering CRS with linear axes is required");
    }
    if (!(resolved.horizontalCrs.axisUnitToMetre > 0.0)) {
        fail(GeoErrorCode::UnsupportedCrs,
            "project horizontal CRS '" + config.horizontalCrs + "' does not have linear axes");
    }

    resolved.linearUnit = resolveLinearUnit(impl_->context, config.linearUnit);
    resolved.axisConvention = config.axisConvention;
    resolved.origin = ProjectGlobalPosition{config.originEasting, config.originNorthing, config.originHeight};

    if (!config.verticalCrs.empty()) {
        const ResolvedCrs vertical = describeCrs(config.verticalCrs);
        if (vertical.kind != CrsKind::Vertical) {
            fail(GeoErrorCode::UnsupportedCrs,
                "project vertical reference '" + config.verticalCrs + "' resolves to "
                    + std::string{crsKindName(vertical.kind)} + "; a vertical CRS is required");
        }
        resolved.vertical.definition = config.verticalCrs;
        resolved.vertical.identifier =
            vertical.identifier.empty() ? config.verticalCrs : vertical.identifier;
        resolved.vertical.name = vertical.name;
        resolved.vertical.present = true;
        resolved.vertical.authoritativeIdentifier =
            !vertical.identifier.empty() && vertical.authoritativeIdentifier;
        // The reference resolves to a real vertical CRS, so it can take part
        // in compound transformations; grid-dependent operations may still
        // be unavailable and are reported per-transform.
        resolved.vertical.transformSupported = true;
        resolved.vertical.axisUnitToMetre = vertical.axisUnitToMetre;
    }
    return resolved;
}

GeoreferenceConfig GeoTransformService::canonicalizeConfig(const GeoreferenceConfig& config) const {
    const ProjectGeoreference resolved = resolveProjectGeoreference(config);

    GeoreferenceConfig canonical = config;
    // Substitute "AUTH:CODE" only for definitions canonically equivalent
    // to the authority entry. Bound CRSs and custom WKT/PROJJSON that
    // merely carry an authority ID keep their original definition so no
    // encoded transformation semantics are lost.
    if (resolved.horizontalCrs.authoritativeIdentifier) {
        canonical.horizontalCrs = resolved.horizontalCrs.identifier;
    }
    canonical.linearUnit = resolved.linearUnit.name;
    if (resolved.vertical.present && resolved.vertical.authoritativeIdentifier) {
        canonical.verticalCrs = resolved.vertical.identifier;
    }
    return canonical;
}

RenderLocalFrame GeoTransformService::renderLocalFrame(const ProjectGeoreference& project) const noexcept {
    return RenderLocalFrame::atProjectOrigin(project);
}

namespace {

// A resolved source spatial reference: the horizontal CRS metadata, the
// optional resolved vertical reference, and the factor that converts a
// source height into metres.
struct ResolvedSource {
    ResolvedCrs horizontal;
    ResolvedCrs vertical;
    bool hasVertical{false};
    double heightToMetre{1.0};
};

// Horizontal source roles that are actually supported by the transform
// path. Geographic 2D/3D, projected, and engineering CRSs carry height as
// a separate component handled by the vertical-unit conversion. Geocentric,
// compound, vertical, and other CRS forms participate in the CRS
// transformation with all three coordinates and are rejected explicitly
// rather than being fed z=0 through a horizontal-only operation.
bool isSupportedSourceHorizontalKind(const CrsKind kind) {
    return kind == CrsKind::Geographic || kind == CrsKind::Projected || kind == CrsKind::Engineering;
}

// Resolves and role-validates a source spatial reference. A malformed
// definition fails InvalidCrs; a well-formed CRS in the wrong role fails
// UnsupportedCrs.
[[nodiscard]] ResolvedSource resolveSource(
    const GeoTransformService& service,
    const SourceSpatialReference& source) {
    if (source.horizontalCrs.empty()) {
        fail(GeoErrorCode::InvalidCrs, "source horizontal CRS must not be empty");
    }

    ResolvedSource resolved;
    resolved.horizontal = service.describeCrs(source.horizontalCrs);
    if (!isSupportedSourceHorizontalKind(resolved.horizontal.kind)) {
        fail(GeoErrorCode::UnsupportedCrs,
            "source horizontal CRS '" + source.horizontalCrs + "' resolves to "
                + std::string{crsKindName(resolved.horizontal.kind)}
                + "; a geographic, projected, or engineering CRS is required");
    }

    // Source heights are interpreted in the source vertical CRS's axis
    // unit; without one they are assumed to be metres (PROJ's convention
    // for 2D/geographic source data).
    if (!source.verticalCrs.empty()) {
        const ResolvedCrs sourceVertical = service.describeCrs(source.verticalCrs);
        if (sourceVertical.kind != CrsKind::Vertical) {
            fail(GeoErrorCode::UnsupportedCrs,
                "source vertical CRS '" + source.verticalCrs + "' resolves to "
                    + std::string{crsKindName(sourceVertical.kind)}
                    + "; a vertical CRS is required");
        }
        resolved.vertical = sourceVertical;
        resolved.hasVertical = true;
        if (sourceVertical.axisUnitToMetre > 0.0) {
            resolved.heightToMetre = sourceVertical.axisUnitToMetre;
        }
    }
    return resolved;
}

// Runs one normalized coordinate operation and reports engine errors
// explicitly. A finite input that produces a non-finite output is an
// execution failure of the operation — typically a transformation whose
// support is partially unavailable — so it maps to UnsupportedTransform
// (GEO_UNSUPPORTED), never to the invalid-argument path. Only the
// horizontal components are verified here; the vertical component is
// checked by 3D call sites that transform it.
PJ_COORD runTransform(
    PJ* transform,
    const PJ_DIRECTION direction,
    const double x,
    const double y,
    const double z,
    const std::string_view operationLabel) {
    const PJ_COORD output = proj_trans(transform, direction, proj_coord(x, y, z, HUGE_VAL));
    if (const int errorCode = proj_errno(transform); errorCode != 0) {
        const int contextCode = errorCode;
        proj_errno_reset(transform);
        fail(GeoErrorCode::UnsupportedTransform,
            std::string{operationLabel} + " failed: geospatial engine error " + std::to_string(contextCode));
    }
    if (!std::isfinite(output.xyz.x) || !std::isfinite(output.xyz.y)) {
        fail(GeoErrorCode::UnsupportedTransform,
            std::string{operationLabel} + " produced a non-finite coordinate");
    }
    return output;
}

void requireFiniteHeight(const PJ_COORD output, const std::string_view operationLabel) {
    if (!std::isfinite(output.xyz.z)) {
        fail(GeoErrorCode::UnsupportedTransform,
            std::string{operationLabel} + " produced a non-finite height");
    }
}

} // namespace

ProjectGlobalPosition GeoTransformService::sourceToProjectGlobal(
    const ProjectGeoreference& project,
    const SourceSpatialReference& source,
    const GeoCoordinate& coordinate) const {
    if (!isFinite(coordinate)) {
        fail(GeoErrorCode::NotFinite, "source coordinate must be finite");
    }
    const ResolvedSource resolved = resolveSource(*this, source);
    const double sourceHeightToMetre = resolved.heightToMetre;

    // Whether a real 3D compound transformation applies: both sides carry
    // a vertical reference and they resolve to different vertical CRSs.
    // Identity is compared on resolved PROJ objects, so equivalent
    // definition syntaxes (e.g. an EPSG identifier and its OGC URN) do not
    // trigger an unnecessary datum transformation. All other cases keep
    // the datum height but still convert axis units into the project
    // linear unit.
    const bool verticalTransformApplies = project.vertical.present && resolved.hasVertical
        && !impl_->crsEquivalent(resolved.vertical.definition, project.vertical.definition);

    if (verticalTransformApplies) {
        PJ* transform = impl_->requireCompoundTransform(
            source.horizontalCrs,
            source.verticalCrs,
            project.horizontalCrs.definition,
            project.vertical.definition);
        const PJ_COORD output = runTransform(
            transform, PJ_FWD, coordinate.x, coordinate.y, coordinate.z, "compound source-to-project transform");
        requireFiniteHeight(output, "compound source-to-project transform");
        const double horizontalScale =
            project.horizontalCrs.axisUnitToMetre / project.linearUnit.toMetre;
        const double verticalScale = project.vertical.axisUnitToMetre > 0.0
            ? project.vertical.axisUnitToMetre / project.linearUnit.toMetre
            : 1.0;
        return ProjectGlobalPosition{
            output.xyz.x * horizontalScale,
            output.xyz.y * horizontalScale,
            output.xyz.z * verticalScale};
    }

    PJ* transform = impl_->requireTransform(source.horizontalCrs, project.horizontalCrs.definition);
    const PJ_COORD output = runTransform(
        transform, PJ_FWD, coordinate.x, coordinate.y, 0.0, "source-to-project transform");
    const double scale = project.horizontalCrs.axisUnitToMetre / project.linearUnit.toMetre;
    const double heightScale = sourceHeightToMetre / project.linearUnit.toMetre;
    return ProjectGlobalPosition{
        output.xyz.x * scale,
        output.xyz.y * scale,
        coordinate.z * heightScale};
}

GeoCoordinate GeoTransformService::projectGlobalToSource(
    const ProjectGeoreference& project,
    const SourceSpatialReference& source,
    const ProjectGlobalPosition& position) const {
    if (!isFinite(position)) {
        fail(GeoErrorCode::NotFinite, "project-global position must be finite");
    }
    // Mirror of the forward contract: heights are converted from project
    // linear units back into the source vertical axis unit (metres when
    // the source carries no vertical CRS).
    const ResolvedSource resolved = resolveSource(*this, source);
    const double sourceHeightToMetre = resolved.heightToMetre;

    const bool verticalTransformApplies = project.vertical.present && resolved.hasVertical
        && !impl_->crsEquivalent(resolved.vertical.definition, project.vertical.definition);

    if (verticalTransformApplies) {
        PJ* transform = impl_->requireCompoundTransform(
            source.horizontalCrs,
            source.verticalCrs,
            project.horizontalCrs.definition,
            project.vertical.definition);
        const double horizontalScale =
            project.linearUnit.toMetre / project.horizontalCrs.axisUnitToMetre;
        const double verticalScale = project.vertical.axisUnitToMetre > 0.0
            ? project.linearUnit.toMetre / project.vertical.axisUnitToMetre
            : 1.0;
        const PJ_COORD output = runTransform(transform, PJ_INV,
            position.easting * horizontalScale,
            position.northing * horizontalScale,
            position.height * verticalScale,
            "compound project-to-source transform");
        requireFiniteHeight(output, "compound project-to-source transform");
        return GeoCoordinate{output.xyz.x, output.xyz.y, output.xyz.z};
    }

    PJ* transform = impl_->requireTransform(source.horizontalCrs, project.horizontalCrs.definition);
    const double unscale = project.linearUnit.toMetre / project.horizontalCrs.axisUnitToMetre;
    const double heightUnscale = project.linearUnit.toMetre / sourceHeightToMetre;
    const PJ_COORD output = runTransform(transform, PJ_INV,
        position.easting * unscale,
        position.northing * unscale,
        0.0,
        "project-to-source transform");
    return GeoCoordinate{output.xyz.x, output.xyz.y, position.height * heightUnscale};
}

} // namespace infraforge::domain::geo
