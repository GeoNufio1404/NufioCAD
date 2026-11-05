#include <nuf/io/NufFile.hpp>

#include <sqlite3.h>

#include <memory>
#include <utility>

namespace nuf::io {

namespace {

constexpr const char* kSchemaSql = R"sql(
CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE features (
    id         INTEGER PRIMARY KEY,
    position   INTEGER NOT NULL,
    type       TEXT    NOT NULL,
    name       TEXT    NOT NULL,
    suppressed INTEGER NOT NULL DEFAULT 0,
    params     TEXT    NOT NULL          -- parámetros en JSON
);
CREATE TABLE parameters (
    name       TEXT PRIMARY KEY,
    expression TEXT NOT NULL
);
CREATE TABLE blobs (
    key  TEXT PRIMARY KEY,
    kind TEXT NOT NULL,                  -- import | cache | thumbnail
    data BLOB NOT NULL
);
)sql";

// SQLite espera rutas en UTF-8 en todos los sistemas, incluido Windows.
std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

void closeDb(sqlite3* db) noexcept
{
    if (db != nullptr) {
        sqlite3_close_v2(db);
    }
}

using DbHandle = std::unique_ptr<sqlite3, decltype(&closeDb)>;

[[noreturn]] void fail(sqlite3* db, const std::string& context)
{
    std::string message = context;
    if (db != nullptr) {
        message += ": ";
        message += sqlite3_errmsg(db);
    }
    throw NufError(message);
}

void exec(sqlite3* db, const char* sql, const char* context)
{
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = std::string(context) + ": " + (error != nullptr ? error : "error desconocido");
        sqlite3_free(error);
        throw NufError(message);
    }
}

int pragmaInt(sqlite3* db, const char* pragma)
{
    sqlite3_stmt* raw = nullptr;
    const std::string sql = std::string("PRAGMA ") + pragma + ";";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
        fail(db, "No se pudo leer el archivo");
    }
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, &sqlite3_finalize);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        fail(db, "No se pudo leer el archivo");
    }
    return sqlite3_column_int(stmt.get(), 0);
}

DbHandle openHandle(const std::filesystem::path& path, int flags)
{
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(toUtf8(path).c_str(), &raw, flags, nullptr);
    DbHandle db(raw, &closeDb);
    if (rc != SQLITE_OK) {
        fail(db.get(), "No se pudo abrir " + toUtf8(path));
    }
    return db;
}

} // namespace

NufFile NufFile::create(const std::filesystem::path& path)
{
    if (std::filesystem::exists(path)) {
        throw NufError("Ya existe un archivo en " + toUtf8(path));
    }

    DbHandle db = openHandle(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);

    const std::string pragmas = "PRAGMA application_id = " + std::to_string(kApplicationId) + ";"
                              + "PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";";
    exec(db.get(), "BEGIN;", "No se pudo iniciar la transacción");
    exec(db.get(), kSchemaSql, "No se pudo crear el esquema");
    exec(db.get(), pragmas.c_str(), "No se pudo marcar el archivo como .nuf");
    exec(db.get(), "COMMIT;", "No se pudo guardar el archivo");

    return NufFile(db.release());
}

NufFile NufFile::open(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) {
        throw NufError("No existe el archivo " + toUtf8(path));
    }

    DbHandle db = openHandle(path, SQLITE_OPEN_READWRITE);

    // Si no es SQLite, la primera lectura falla con "file is not a database".
    if (pragmaInt(db.get(), "application_id") != kApplicationId) {
        throw NufError("El archivo no es un diseño .nuf: " + toUtf8(path));
    }
    if (pragmaInt(db.get(), "user_version") > kSchemaVersion) {
        throw NufError("El archivo se creó con una versión más reciente de la aplicación");
    }

    return NufFile(db.release());
}

NufFile::NufFile(sqlite3* db) noexcept
    : db_(db)
{
}

NufFile::NufFile(NufFile&& other) noexcept
    : db_(std::exchange(other.db_, nullptr))
{
}

NufFile& NufFile::operator=(NufFile&& other) noexcept
{
    if (this != &other) {
        closeDb(db_);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

NufFile::~NufFile()
{
    closeDb(db_);
}

int NufFile::schemaVersion() const
{
    return pragmaInt(db_, "user_version");
}

void NufFile::setMeta(std::string_view key, std::string_view value)
{
    sqlite3_stmt* raw = nullptr;
    constexpr const char* sql =
        "INSERT INTO meta(key, value) VALUES(?1, ?2) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value;";
    if (sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr) != SQLITE_OK) {
        fail(db_, "No se pudo escribir meta");
    }
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, &sqlite3_finalize);
    sqlite3_bind_text(stmt.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        fail(db_, "No se pudo escribir meta");
    }
}

std::optional<std::string> NufFile::meta(std::string_view key) const
{
    sqlite3_stmt* raw = nullptr;
    constexpr const char* sql = "SELECT value FROM meta WHERE key = ?1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr) != SQLITE_OK) {
        fail(db_, "No se pudo leer meta");
    }
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, &sqlite3_finalize);
    sqlite3_bind_text(stmt.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    if (rc != SQLITE_ROW) {
        fail(db_, "No se pudo leer meta");
    }
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    return std::string(text != nullptr ? text : "");
}

void NufFile::begin()
{
    exec(db_, "BEGIN;", "No se pudo iniciar la transacción");
}

void NufFile::commit()
{
    exec(db_, "COMMIT;", "No se pudo guardar el archivo");
}

void NufFile::rollback() noexcept
{
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
}

namespace {

using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Stmt prepare(sqlite3* db, const char* sql, const char* context)
{
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
        fail(db, context);
    }
    return Stmt(raw, &sqlite3_finalize);
}

std::string columnText(sqlite3_stmt* stmt, int column)
{
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
    return text != nullptr ? std::string(text, std::size_t(sqlite3_column_bytes(stmt, column))) : std::string();
}

void bindText(sqlite3_stmt* stmt, int index, const std::string& value)
{
    sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

} // namespace

void NufFile::writeFeatures(const std::vector<FeatureRow>& rows)
{
    exec(db_, "DELETE FROM features;", "No se pudo escribir las operaciones");
    Stmt stmt = prepare(db_, "INSERT INTO features(id, position, type, name, suppressed, params) VALUES(?1,?2,?3,?4,?5,?6);",
                        "No se pudo escribir las operaciones");
    int position = 0;
    for (const FeatureRow& row : rows) {
        sqlite3_reset(stmt.get());
        sqlite3_bind_int64(stmt.get(), 1, row.id);
        sqlite3_bind_int(stmt.get(), 2, position++);
        bindText(stmt.get(), 3, row.type);
        bindText(stmt.get(), 4, row.name);
        sqlite3_bind_int(stmt.get(), 5, row.suppressed ? 1 : 0);
        bindText(stmt.get(), 6, row.params);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
            fail(db_, "No se pudo escribir las operaciones");
        }
    }
}

std::vector<FeatureRow> NufFile::readFeatures() const
{
    Stmt stmt = prepare(db_, "SELECT id, type, name, suppressed, params FROM features ORDER BY position;",
                        "No se pudo leer las operaciones");
    std::vector<FeatureRow> rows;
    int rc = 0;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        FeatureRow row;
        row.id = sqlite3_column_int64(stmt.get(), 0);
        row.type = columnText(stmt.get(), 1);
        row.name = columnText(stmt.get(), 2);
        row.suppressed = sqlite3_column_int(stmt.get(), 3) != 0;
        row.params = columnText(stmt.get(), 4);
        rows.push_back(std::move(row));
    }
    if (rc != SQLITE_DONE) {
        fail(db_, "No se pudo leer las operaciones");
    }
    return rows;
}

void NufFile::writeBlob(std::string_view key, std::string_view kind, std::string_view data)
{
    Stmt stmt = prepare(db_,
                        "INSERT INTO blobs(key, kind, data) VALUES(?1, ?2, ?3) "
                        "ON CONFLICT(key) DO UPDATE SET kind = excluded.kind, data = excluded.data;",
                        "No se pudo escribir los datos");
    sqlite3_bind_text(stmt.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, kind.data(), static_cast<int>(kind.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob64(stmt.get(), 3, data.data(), data.size(), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        fail(db_, "No se pudo escribir los datos");
    }
}

std::optional<std::string> NufFile::readBlob(std::string_view key) const
{
    Stmt stmt = prepare(db_, "SELECT data FROM blobs WHERE key = ?1;", "No se pudo leer los datos");
    sqlite3_bind_text(stmt.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    if (rc != SQLITE_ROW) {
        fail(db_, "No se pudo leer los datos");
    }
    const auto* data = static_cast<const char*>(sqlite3_column_blob(stmt.get(), 0));
    return std::string(data != nullptr ? data : "", std::size_t(sqlite3_column_bytes(stmt.get(), 0)));
}

std::vector<std::pair<std::string, std::string>> NufFile::allMeta() const
{
    Stmt stmt = prepare(db_, "SELECT key, value FROM meta ORDER BY key;", "No se pudo leer meta");
    std::vector<std::pair<std::string, std::string>> entries;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        entries.emplace_back(columnText(stmt.get(), 0), columnText(stmt.get(), 1));
    }
    return entries;
}

std::string NufFile::sqliteVersion()
{
    return sqlite3_libversion();
}

} // namespace nuf::io
