#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace infraforge::persistence {

// Thrown for all SQLite usage errors; wraps the native error message with
// context. Persistence policy failures (corruption, missing files) are
// reported through this type as well.
class SqliteError : public std::runtime_error {
public:
    SqliteError(std::string context, int resultCode);
    [[nodiscard]] int resultCode() const noexcept { return resultCode_; }

private:
    int resultCode_;
};

enum class SqliteOpenMode {
    ReadOnly,
    ReadWrite,
    ReadWriteCreate,
};

// RAII wrapper over a sqlite3 handle. One connection per project database;
// the owning thread is the application executor.
class SqliteConnection final {
public:
    SqliteConnection() = default;
    ~SqliteConnection();

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    SqliteConnection(SqliteConnection&& other) noexcept;
    SqliteConnection& operator=(SqliteConnection&& other) noexcept;

    static SqliteConnection open(const std::filesystem::path& databaseFile, SqliteOpenMode mode);

    [[nodiscard]] bool isOpen() const noexcept { return handle_ != nullptr; }
    void close() noexcept;

    void exec(std::string_view sql);
    [[nodiscard]] std::int64_t lastChanges() const noexcept;

private:
    friend class SqliteStatement;

    sqlite3* handle_{nullptr};
};

// RAII statement wrapper supporting positional text/int64 binding.
class SqliteStatement final {
public:
    SqliteStatement() = default;
    ~SqliteStatement();

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;

    // Prepares a statement. Accepts a const connection because SQLite's C API
    // is not const-correct; preparation never mutates logical database state.
    SqliteStatement(const SqliteConnection& connection, std::string_view sql);

    void bindText(int index, std::string_view value);
    void bindInt64(int index, std::int64_t value);
    void bindDouble(int index, double value);
    void bindNull(int index);

    // Returns true when a row is available; false on SQLITE_DONE.
    [[nodiscard]] bool step();
    [[nodiscard]] std::string_view columnText(int index) const;
    [[nodiscard]] std::int64_t columnInt64(int index) const;
    [[nodiscard]] double columnDouble(int index) const;
    [[nodiscard]] bool columnIsNull(int index) const;

    void reset();

private:
    void requireOk(int resultCode, std::string_view context) const;

    sqlite3* db_{nullptr};
    sqlite3_stmt* handle_{nullptr};
};

// BEGIN IMMEDIATE ... COMMIT/ROLLBACK guard for one write transaction.
class SqliteTransaction final {
public:
    explicit SqliteTransaction(SqliteConnection& connection);
    ~SqliteTransaction();

    SqliteTransaction(const SqliteTransaction&) = delete;
    SqliteTransaction& operator=(const SqliteTransaction&) = delete;

    void commit();

private:
    SqliteConnection* connection_{nullptr};
    bool committed_{false};
};

} // namespace infraforge::persistence
