#pragma once

// Archivo nativo .nuf: una base de datos SQLite (ver arquitectura, sección 7).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

struct sqlite3;

namespace nuf::io {

// Identifica el archivo como .nuf en la cabecera de SQLite ("NUF1").
inline constexpr std::int32_t kApplicationId = 0x4E554631;

// Versión del esquema que escribe esta compilación.
inline constexpr int kSchemaVersion = 1;

class NufError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class NufFile {
public:
    // Crea un archivo nuevo con el esquema vacío. Falla si ya existe.
    static NufFile create(const std::filesystem::path& path);

    // Abre un archivo existente y valida que sea un .nuf compatible.
    static NufFile open(const std::filesystem::path& path);

    NufFile(const NufFile&) = delete;
    NufFile& operator=(const NufFile&) = delete;
    NufFile(NufFile&& other) noexcept;
    NufFile& operator=(NufFile&& other) noexcept;
    ~NufFile();

    int schemaVersion() const;

    // Tabla meta: unidades, versión de la app, etc.
    void setMeta(std::string_view key, std::string_view value);
    std::optional<std::string> meta(std::string_view key) const;

    static std::string sqliteVersion();

private:
    explicit NufFile(sqlite3* db) noexcept;

    sqlite3* db_ = nullptr;
};

} // namespace nuf::io
