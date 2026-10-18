#include <nuf/io/NufFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using nuf::io::NufError;
using nuf::io::NufFile;

namespace {

// Carpeta temporal propia por prueba, con acentos en el nombre a propósito.
struct TempDir {
    fs::path path;

    explicit TempDir(const std::string& name)
        : path(fs::temp_directory_path() / fs::path(u8"nuf_pruebas_ñandú") / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

} // namespace

TEST_CASE("Crear y reabrir un .nuf conserva los datos", "[io]")
{
    TempDir dir("roundtrip");
    const fs::path file = dir.path / fs::path(u8"diseño de prueba.nuf");

    {
        auto nuf = NufFile::create(file);
        REQUIRE(nuf.schemaVersion() == nuf::io::kSchemaVersion);
        nuf.setMeta("unidades", "mm");
        nuf.setMeta("unidades", "cm"); // sobrescribe
    }

    const auto reopened = NufFile::open(file);
    REQUIRE(reopened.meta("unidades") == std::string("cm"));
    REQUIRE_FALSE(reopened.meta("no_existe").has_value());
}

TEST_CASE("create no sobrescribe un archivo existente", "[io]")
{
    TempDir dir("existente");
    const fs::path file = dir.path / "a.nuf";
    { auto nuf = NufFile::create(file); }

    REQUIRE_THROWS_AS(NufFile::create(file), NufError);
}

TEST_CASE("open rechaza archivos que no son .nuf", "[io]")
{
    TempDir dir("invalido");

    const fs::path text = dir.path / "texto.nuf";
    std::ofstream(text) << "esto no es una base de datos";
    REQUIRE_THROWS_AS(NufFile::open(text), NufError);

    REQUIRE_THROWS_AS(NufFile::open(dir.path / "no_existe.nuf"), NufError);
}
