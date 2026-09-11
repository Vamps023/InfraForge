#include "infraforge/application/ProjectService.hpp"

namespace infraforge::application {
namespace {

[[noreturn]] void fail(CommandFailureCode code, std::string message) {
    throw CommandFailure(code, std::move(message));
}

[[noreturn]] void translate(const ports::StoreError& error) {
    switch (error.category()) {
    case ports::StoreErrorCategory::DirectoryInvalid:
        throw CommandFailure(CommandFailureCode::ProjectDirectoryInvalid, error.what());
    case ports::StoreErrorCategory::FormatUnsupported:
        throw CommandFailure(CommandFailureCode::ProjectFormatUnsupported, error.what());
    case ports::StoreErrorCategory::SchemaUnsupported:
        throw CommandFailure(CommandFailureCode::SchemaVersionUnsupported, error.what());
    case ports::StoreErrorCategory::PersistenceFailure:
        throw CommandFailure(CommandFailureCode::PersistenceFailure, error.what());
    }
    throw CommandFailure(CommandFailureCode::Internal, error.what());
}

} // namespace

ProjectService::ProjectService(ports::ProjectStore& store)
    : store_(store) {}

ProjectCommandResult ProjectService::create(const domain::project::CreateProjectSpec& spec) {
    if (store_.isOpen()) {
        fail(CommandFailureCode::ProjectAlreadyOpen, "a project is already open; close it before creating another");
    }
    if (const auto nameError = domain::project::validateDisplayName(spec.displayName); nameError.has_value()) {
        fail(CommandFailureCode::InvalidArgument, nameError->field + ": " + nameError->message);
    }
    if (const auto geoError = domain::project::validateGeoreference(spec.georeference); geoError.has_value()) {
        fail(CommandFailureCode::InvalidArgument, geoError->field + ": " + geoError->message);
    }

    ProjectCommandResult result;
    try {
        result.record = store_.create(spec);
    } catch (const ports::StoreError& error) {
        translate(error);
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
        translate(error);
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
        translate(error);
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
        translate(error);
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
