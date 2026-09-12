#include "infraforge/application/ProjectService.hpp"

#include "infraforge/domain/geo/GeoTransformService.hpp"

#include "StoreErrorTranslation.hpp"

namespace infraforge::application {
namespace {

[[noreturn]] void fail(CommandFailureCode code, std::string message) {
    throw CommandFailure(code, std::move(message));
}

} // namespace

ProjectService::ProjectService(ports::ProjectStore& store, const domain::geo::GeoTransformService& transforms)
    : store_(store),
      transforms_(transforms) {}

ProjectCommandResult ProjectService::create(const domain::project::CreateProjectSpec& spec) {
    if (store_.isOpen()) {
        fail(CommandFailureCode::ProjectAlreadyOpen, "a project is already open; close it before creating another");
    }
    if (const auto nameError = domain::project::validateDisplayName(spec.displayName); nameError.has_value()) {
        fail(CommandFailureCode::InvalidArgument, nameError->field + ": " + nameError->message);
    }

    domain::project::CreateProjectSpec canonical = spec;
    try {
        // The persisted georeference is the canonical form: CRS definitions
        // are resolved and validated (projected/engineering horizontal CRS,
        // resolvable linear unit, vertical CRS when present) and
        // authority-resolvable definitions are stored as "AUTH:CODE".
        canonical.georeference = transforms_.canonicalizeConfig(spec.georeference);
    } catch (const domain::geo::GeoError& error) {
        translateGeoError(error);
    }

    ProjectCommandResult result;
    try {
        result.record = store_.create(canonical);
    } catch (const ports::StoreError& error) {
        translateStoreError(error);
    }
    result.events.push_back({ProjectEventKind::Opened, result.record});
    return result;
}

ProjectCommandResult ProjectService::open(const std::filesystem::path& projectDirectory) {
    if (store_.isOpen()) {
        fail(CommandFailureCode::ProjectAlreadyOpen, "a project is already open; close it before opening another");
    }
    if (projectDirectory.is_relative()) {
        fail(CommandFailureCode::InvalidArgument, "project directory must be an absolute path");
    }

    ProjectCommandResult result;
    try {
        result.record = store_.open(projectDirectory);
    } catch (const ports::StoreError& error) {
        translateStoreError(error);
    }

    // The persisted canonical georeference must resolve in this engine's
    // geospatial runtime. A structurally valid project whose CRS cannot be
    // resolved or is unsupported (for example a geographic-only horizontal
    // CRS written by another tool) must not become an active session —
    // every spatial domain would transform against a broken frame.
    try {
        (void)transforms_.resolveProjectGeoreference(result.record.georeference);
    } catch (const domain::geo::GeoError& error) {
        // Leave the store/session in a clean closed state so a subsequent
        // open/create is not blocked by a half-open project.
        try {
            store_.close();
        } catch (...) {
            // Best-effort cleanup only; the session must not outlive the failure.
        }
        translateGeoError(error);
    }
    result.events.push_back({ProjectEventKind::Opened, result.record});
    return result;
}

ProjectCommandResult ProjectService::save() {
    if (!store_.isOpen()) {
        fail(CommandFailureCode::ProjectNotOpen, "no project is open");
    }

    const bool wasDirty = store_.current().isDirty();
    ProjectCommandResult result;
    try {
        result.record = store_.save();
    } catch (const ports::StoreError& error) {
        translateStoreError(error);
    }
    if (wasDirty) {
        result.events.push_back({ProjectEventKind::DirtyStateChanged, result.record});
    }
    return result;
}

ProjectCommandResult ProjectService::saveAs(const domain::project::SaveAsSpec& spec) {
    if (!store_.isOpen()) {
        fail(CommandFailureCode::ProjectNotOpen, "no project is open");
    }
    if (const auto nameError = domain::project::validateDisplayName(spec.displayName); nameError.has_value()) {
        fail(CommandFailureCode::InvalidArgument, nameError->field + ": " + nameError->message);
    }

    const domain::project::ProjectRecord previousRecord = store_.current();
    ProjectCommandResult result;
    try {
        result.record = store_.saveAs(spec);
    } catch (const ports::StoreError& error) {
        translateStoreError(error);
    }
    result.events.push_back({ProjectEventKind::Closed, previousRecord});
    result.events.push_back({ProjectEventKind::Opened, result.record});
    return result;
}

ProjectCommandResult ProjectService::close() {
    if (!store_.isOpen()) {
        fail(CommandFailureCode::ProjectNotOpen, "no project is open");
    }

    ProjectCommandResult result;
    result.record = store_.current();
    store_.close();
    result.sessionClosed = true;
    result.events.push_back({ProjectEventKind::Closed, result.record});
    return result;
}

ProjectCommandResult ProjectService::getSummary() const {
    if (!store_.isOpen()) {
        fail(CommandFailureCode::ProjectNotOpen, "no project is open");
    }
    ProjectCommandResult result;
    result.record = store_.current();
    return result;
}

} // namespace infraforge::application
