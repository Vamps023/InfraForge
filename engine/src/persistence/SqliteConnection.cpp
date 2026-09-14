#include "infraforge/persistence/SqliteConnection.hpp"

#include "infraforge/runtime/FileSystemUtf8.hpp"

#include <sqlite3.h>
#include <utility>

namespace infraforge::persistence {
namespace {

constexpr int kBusyTimeoutMillis = 5000;

int mapOpenMode(SqliteOpenMode mode) {
    switch (mode) {
    case SqliteOpenMode::ReadOnly:
        return SQLITE_OPEN_READONLY;
    case SqliteOpenMode::ReadWrite:
        return SQLITE_OPEN_READWRITE;
    case SqliteOpenMode::ReadWriteCreate:
        return SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    }
    return SQLITE_OPEN_READONLY;
}

} // namespace

SqliteError::SqliteError(std::string context, const int resultCode)
    : std::runtime_error(context + " (sqlite code " + std::to_string(resultCode) + ": "
            + sqlite3_errstr(resultCode) + ")"),
      resultCode_(resultCode) {}

SqliteConnection::~SqliteConnection() {
    close();
}

SqliteConnection::SqliteConnection(SqliteConnection&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

SqliteConnection& SqliteConnection::operator=(SqliteConnection&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

SqliteConnection SqliteConnection::open(const std::filesystem::path& databaseFile, const SqliteOpenMode mode) {
    SqliteConnection connection;
    const int flags = mapOpenMode(mode) | SQLITE_OPEN_FULLMUTEX;
    const int resultCode = sqlite3_open_v2(runtime::utf8String(databaseFile).c_str(), &connection.handle_, flags, nullptr);
    if (resultCode != SQLITE_OK) {
        const std::string message = "failed to open SQLite database '" + databaseFile.string() + "'";
        sqlite3_close(connection.handle_);
        connection.handle_ = nullptr;
        throw SqliteError(message, resultCode);
    }

    connection.exec("PRAGMA busy_timeout = 5000");
    connection.exec("PRAGMA foreign_keys = ON");
    if (mode == SqliteOpenMode::ReadOnly) {
        // A read-only connection must never attempt to reconfigure the
        // database file: the schema-version probe relies on this to reject
        // unsupported projects without modifying them. journal_mode in
        // particular is persistent and is therefore only set on writable
        // connections. query_only is defense in depth against accidents.
        connection.exec("PRAGMA query_only = ON");
    } else {
        connection.exec("PRAGMA journal_mode = DELETE");
        connection.exec("PRAGMA synchronous = FULL");
    }

    return connection;
}

void SqliteConnection::close() noexcept {
    if (handle_ != nullptr) {
        sqlite3_close_v2(handle_);
        handle_ = nullptr;
    }
}

void SqliteConnection::exec(const std::string_view sql) {
    if (handle_ == nullptr) {
        throw SqliteError("cannot execute SQL on a closed connection", SQLITE_MISUSE);
    }
    char* errorMessage = nullptr;
    const int resultCode = sqlite3_exec(handle_, sql.data(), nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        const std::string detail = errorMessage != nullptr ? errorMessage : "unknown sqlite error";
        sqlite3_free(errorMessage);
        throw SqliteError(std::string{"SQL execution failed: "} + detail, resultCode);
    }
}

std::int64_t SqliteConnection::lastChanges() const noexcept {
    return handle_ != nullptr ? sqlite3_changes64(handle_) : 0;
}

SqliteStatement::~SqliteStatement() {
    if (handle_ != nullptr) {
        sqlite3_finalize(handle_);
        handle_ = nullptr;
    }
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      handle_(std::exchange(other.handle_, nullptr)) {}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            sqlite3_finalize(handle_);
        }
        db_ = std::exchange(other.db_, nullptr);
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

SqliteStatement::SqliteStatement(const SqliteConnection& connection, const std::string_view sql) {
    if (!connection.isOpen()) {
        throw SqliteError("cannot prepare SQL on a closed connection", SQLITE_MISUSE);
    }
    auto* dbHandle = const_cast<sqlite3*>(std::as_const(connection).handle_);
    const int resultCode = sqlite3_prepare_v2(
        dbHandle, sql.data(), static_cast<int>(sql.size()), &handle_, nullptr);
    if (resultCode != SQLITE_OK) {
        const std::string message = sqlite3_errmsg(dbHandle);
        throw SqliteError(std::string{"failed to prepare statement: "} + message, resultCode);
    }
    db_ = dbHandle;
}

void SqliteStatement::requireOk(const int resultCode, const std::string_view context) const {
    if (resultCode != SQLITE_OK && db_ != nullptr) {
        throw SqliteError(std::string{context} + ": " + sqlite3_errmsg(db_), resultCode);
    }
}

void SqliteStatement::bindText(const int index, const std::string_view value) {
    requireOk(
        sqlite3_bind_text(handle_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT),
        "bind text failed");
}

void SqliteStatement::bindInt64(const int index, const std::int64_t value) {
    requireOk(sqlite3_bind_int64(handle_, index, value), "bind int64 failed");
}

void SqliteStatement::bindDouble(const int index, const double value) {
    requireOk(sqlite3_bind_double(handle_, index, value), "bind double failed");
}

void SqliteStatement::bindNull(const int index) {
    requireOk(sqlite3_bind_null(handle_, index), "bind null failed");
}

bool SqliteStatement::step() {
    const int resultCode = sqlite3_step(handle_);
    if (resultCode == SQLITE_ROW) {
        return true;
    }
    if (resultCode == SQLITE_DONE) {
        return false;
    }
    requireOk(resultCode, "statement step failed");
    return false;
}

std::string_view SqliteStatement::columnText(const int index) const {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(handle_, index));
    const int size = sqlite3_column_bytes(handle_, index);
    return text != nullptr ? std::string_view{text, static_cast<std::size_t>(size)} : std::string_view{};
}

std::int64_t SqliteStatement::columnInt64(const int index) const {
    return sqlite3_column_int64(handle_, index);
}

double SqliteStatement::columnDouble(const int index) const {
    return sqlite3_column_double(handle_, index);
}

bool SqliteStatement::columnIsNull(const int index) const {
    return sqlite3_column_type(handle_, index) == SQLITE_NULL;
}

void SqliteStatement::reset() {
    requireOk(sqlite3_reset(handle_), "statement reset failed");
    requireOk(sqlite3_clear_bindings(handle_), "clear bindings failed");
}

SqliteTransaction::SqliteTransaction(SqliteConnection& connection)
    : connection_(&connection) {
    connection_->exec("BEGIN IMMEDIATE");
}

SqliteTransaction::~SqliteTransaction() {
    if (connection_ != nullptr && !committed_) {
        try {
            connection_->exec("ROLLBACK");
        } catch (...) {
            // Rollback of an implicitly rolled-back transaction can fail when
            // the connection is already broken; the database then rejects the
            // partial transaction by itself, so nothing is silently lost.
        }
    }
}

void SqliteTransaction::commit() {
    if (connection_ == nullptr || committed_) {
        return;
    }
    connection_->exec("COMMIT");
    committed_ = true;
}

} // namespace infraforge::persistence
