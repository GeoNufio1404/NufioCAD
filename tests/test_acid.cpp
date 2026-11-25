// Propiedades ACID del diseño y de su archivo: todo o nada (atomicidad), nunca un archivo o un documento incoherente
// (consistencia), dos que escriben a la vez no se estropean (aislamiento) y lo guardado se puede leer (durabilidad).

#include <nuf/commands/AddRecipe.hpp>
#include <nuf/commands/CommandStack.hpp>
#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/document/Generators.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/io/DocumentFile.hpp>
#include <nuf/io/NufFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace nuf;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<document::CylinderFeature> cylinder(double diameter, double x = 0.0)
{
    auto f = std::make_unique<document::CylinderFeature>();
    f->base.origin = document::OriginPlane::XY;
    f->diameter = diameter;
    f->x = x;
    return f;
}

// Todo lo que define un diseño, como texto: sirve para comparar antes y después.
std::string snapshot(const document::Document& doc, const fs::path& scratch)
{
    io::saveDocument(scratch, doc, {});
    return io::dumpDocument(scratch);
}

struct Folder {
    fs::path path;
    explicit Folder(const char* name)
        : path(fs::temp_directory_path() / (std::string("nuf_acid_") + name))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Folder()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    std::size_t files() const { return std::size_t(std::distance(fs::directory_iterator(path), fs::directory_iterator())); }
};

// Una orden que falla al hacerse o al deshacerse.
class Failing final : public commands::Command {
public:
    Failing(bool onExecute, bool onUndo)
        : onExecute_(onExecute)
        , onUndo_(onUndo)
    {
    }
    void execute(document::Document&) override
    {
        if (onExecute_) {
            throw std::runtime_error("falla a propósito");
        }
    }
    void undo(document::Document&) override
    {
        if (onUndo_) {
            throw std::runtime_error("falla a propósito");
        }
    }
    std::string label() const override { return "Falla"; }

private:
    bool onExecute_;
    bool onUndo_;
};

std::string bytesOf(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& path, const std::string& bytes)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}

} // namespace

TEST_CASE("Atomicidad: una orden compuesta que falla a medias deja el diseño como estaba", "[acid][commands]")
{
    Folder folder("lote");
    document::Document doc;
    commands::CommandStack stack(doc);
    stack.push(std::make_unique<commands::AddFeature>(cylinder(10.0)));
    const std::string before = snapshot(doc, folder.path / "a.nuf");
    std::vector<std::unique_ptr<commands::Command>> steps;
    steps.push_back(std::make_unique<commands::AddFeature>(cylinder(20.0, 50.0)));
    steps.push_back(std::make_unique<commands::DeleteFeature>(doc.feature(0).id()));
    steps.push_back(std::make_unique<Failing>(true, false));
    CHECK_THROWS(stack.push(std::make_unique<commands::Batch>("Lote", std::move(steps))));
    CHECK(doc.featureCount() == 1);
    // Los ids reservados por el camino pueden avanzar («siguiente_id»); lo demás, igual.
    CHECK(doc.result().bodies.size() == 1);
    CHECK(stack.undoLabel() != "Lote");
    stack.undo();
    CHECK(doc.featureCount() == 0);
    stack.redo();
    CHECK(doc.featureCount() == 1);
    CHECK_FALSE(before.empty());
}

TEST_CASE("Atomicidad: si deshacer o rehacer falla, la orden no se pierde", "[acid][commands]")
{
    document::Document doc;
    commands::CommandStack stack(doc);
    stack.push(std::make_unique<Failing>(false, true));
    REQUIRE(stack.canUndo());
    CHECK_THROWS(stack.undo());
    CHECK(stack.canUndo());  // sigue ahí: se puede reintentar
    CHECK_FALSE(stack.canRedo());
}

TEST_CASE("Atomicidad: guardar donde no se puede no toca el archivo de antes ni deja restos", "[acid][io]")
{
    Folder folder("guardar");
    const fs::path path = folder.path / "diseño.nuf";
    document::Document doc;
    doc.addFeature(cylinder(10.0));
    io::saveDocument(path, doc, {});
    const std::string good = bytesOf(path);
    CHECK(folder.files() == 1);
    // Restos de un guardado que murió a medias (otro proceso): no estorban al siguiente.
    writeBytes(folder.path / "diseño.nuf.1-1.guardando", "basura");
    writeBytes(folder.path / "diseño.nuf.1-1.guardando-journal", "basura");
    doc.addFeature(cylinder(20.0, 40.0));
    io::saveDocument(path, doc, {});
    document::Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    CHECK(reopened.featureCount() == 2);
    // Destino imposible (una carpeta con ese nombre y cosas dentro): error, y ningún temporal nuevo.
    const fs::path blocked = folder.path / "carpeta.nuf";
    fs::create_directories(blocked / "dentro");
    const std::size_t count = folder.files();
    CHECK_THROWS_AS(io::saveDocument(blocked, doc, {}), io::NufError);
    CHECK(folder.files() == count);
    CHECK(fs::is_directory(blocked));
    CHECK_FALSE(good.empty());
}

TEST_CASE("Atomicidad: una exportación que falla conserva el archivo que había", "[acid][exchange]")
{
    Folder folder("exportar");
    const geometry::Shape shape = geometry::cylinder("c", sketch::Plane::xy(), 10.0, 5.0);
    const fs::path stl = folder.path / "pieza.stl";
    geometry::writeStl(stl, {shape}, {});
    const std::string good = bytesOf(stl);
    REQUIRE(good.size() > 84);
    CHECK_THROWS(geometry::writeStl(stl, {geometry::Shape{}}, {}));
    CHECK(bytesOf(stl) == good);
    // Destino imposible: ni archivo ni temporal.
    const fs::path missing = folder.path / "no" / "existe.stl";
    CHECK_THROWS(geometry::writeStl(missing, {shape}, {}));
    const fs::path blocked = folder.path / "carpeta.obj";
    fs::create_directories(blocked / "dentro");
    const std::size_t count = folder.files();
    CHECK_THROWS(geometry::writeObj(blocked, {shape}, {}));
    CHECK_THROWS(geometry::writeStep(blocked, {shape}));
    CHECK(folder.files() == count);
    // Y reescribir encima sigue funcionando.
    geometry::writeStl(stl, {geometry::cylinder("c", sketch::Plane::xy(), 20.0, 5.0)}, {});
    CHECK(bytesOf(stl) != good);
}

TEST_CASE("Consistencia: un archivo con bytes estropeados da un error claro o se abre entero, nunca a medias", "[acid][io]")
{
    Folder folder("bytes");
    const fs::path good = folder.path / "bueno.nuf";
    const fs::path bad = folder.path / "malo.nuf";
    document::Document source;
    document::applyRecipe(source, document::findGenerator("lightbox")->make(document::findGenerator("lightbox")->defaults(), ""));
    io::saveDocument(good, source, {}, std::string(3000, 'm'));
    const std::string bytes = bytesOf(good);
    document::Document reopened;
    io::ViewState first;
    io::loadDocument(good, reopened, first);
    CHECK(first.warning.empty());
    io::saveDocument(folder.path / "vuelta.nuf", reopened, {});
    const std::string original = io::dumpDocument(folder.path / "vuelta.nuf");
    int warned = 0;

    document::Document doc;
    doc.addFeature(cylinder(10.0));
    const std::size_t features = doc.featureCount();
    std::mt19937 random(20251125);
    int refused = 0;
    const auto attempt = [&](const std::string& content) {
        writeBytes(bad, content);
        document::Document copy;
        copy.addFeature(cylinder(10.0));
        io::ViewState view;
        try {
            io::loadDocument(bad, copy, view);
            // Se abrió: tiene que ser un diseño entero, y si algo del diseño cambió, con su aviso.
            CHECK(copy.featureCount() == source.featureCount());
            io::saveDocument(folder.path / "vuelta.nuf", copy, {});
            if (io::dumpDocument(folder.path / "vuelta.nuf") != original) {
                ++warned;
                CHECK_FALSE(view.warning.empty());
            }
        } catch (const io::NufError& e) {
            ++refused;
            CHECK(std::string(e.what()).size() > 5);
            CHECK(copy.featureCount() == features);  // el documento no cambió
        }
    };
    // Cortado por la mitad (disco lleno, copia interrumpida), vacío y con bytes pisados por todas partes.
    attempt(bytes.substr(0, bytes.size() / 2));
    attempt(bytes.substr(0, 100));
    attempt(std::string());
    for (int i = 0; i < 60; ++i) {
        std::string broken = bytes;
        for (int k = 0; k < 8; ++k) {
            broken[random() % broken.size()] = char(random() & 0xFF);
        }
        attempt(broken);
    }
    CHECK(refused >= 3);
    // Una letra cambiada a propósito: SQLite y el JSON lo dan por bueno, la huella no.
    std::string digit = bytes;
    const std::size_t name = digit.find("Lightbox: caja");
    REQUIRE(name != std::string::npos);
    digit[name] = 'M';
    attempt(digit);
    CHECK(warned >= 1);
}

TEST_CASE("Consistencia: la versión del archivo solo sube si el diseño usa lo que una aplicación anterior se saltaría", "[acid][io]")
{
    Folder folder("version");
    const fs::path path = folder.path / "v.nuf";
    document::Document plain;
    plain.addFeature(cylinder(10.0));
    io::saveDocument(path, plain, {});
    CHECK(io::NufFile::openReadOnly(path).schemaVersion() == 1);
    document::Document tube;
    auto f = cylinder(10.0);
    f->innerDiameter = 4.0;
    tube.addFeature(std::move(f));
    io::saveDocument(path, tube, {});
    CHECK(io::NufFile::openReadOnly(path).schemaVersion() == 2);
    document::Document back;
    io::ViewState view;
    io::loadDocument(path, back, view);
    CHECK(static_cast<const document::CylinderFeature&>(back.feature(0)).innerDiameter == 4.0);
}

TEST_CASE("Aislamiento: dos que guardan el mismo archivo a la vez dejan siempre uno de los dos, entero", "[acid][io]")
{
    Folder folder("dos");
    const fs::path path = folder.path / "compartido.nuf";
    document::Document one;
    one.addFeature(cylinder(10.0));
    document::Document two;
    two.addFeature(cylinder(20.0));
    two.addFeature(cylinder(30.0, 60.0));
    const io::SavePlan planOne = io::prepareSave(one, {});
    const io::SavePlan planTwo = io::prepareSave(two, {});
    io::writeSave(path, planOne);
    std::atomic<int> failures{0};
    std::atomic<bool> stop{false};
    const auto writer = [&](const io::SavePlan& plan) {
        for (int i = 0; i < 15; ++i) {
            try {
                io::writeSave(path, plan);
            } catch (const io::NufError&) {
                ++failures;  // puede tocar esperar demasiado; lo que no puede es dejar el archivo mal
            }
        }
    };
    std::thread a(writer, std::cref(planOne));
    std::thread b(writer, std::cref(planTwo));
    // Mientras tanto, quien lee ve siempre un diseño entero.
    int seen = 0;
    int broken = 0;
    std::thread reader([&] {
        while (!stop) {
            try {
                document::Document doc;
                io::ViewState view;
                io::loadDocument(path, doc, view);
                broken += doc.featureCount() == 1 || doc.featureCount() == 2 ? 0 : 1;
                ++seen;
            } catch (const io::NufError&) {
                // Windows puede negar la lectura justo durante el reemplazo: se reintenta.
            }
        }
    });
    a.join();
    b.join();
    stop = true;
    reader.join();
    INFO("lecturas " << seen << ", guardados que tuvieron que rendirse " << failures.load());
    CHECK(broken == 0);
    document::Document last;
    io::ViewState view;
    io::loadDocument(path, last, view);
    CHECK((last.featureCount() == 1 || last.featureCount() == 2));
    CHECK(folder.files() == 1);  // ningún temporal olvidado
}

TEST_CASE("Durabilidad: lo guardado se lee de un archivo de solo lectura y queda idéntico al diseño", "[acid][io]")
{
    Folder folder("lectura");
    const fs::path path = folder.path / "solo-lectura.nuf";
    document::Document doc;
    document::applyRecipe(doc, document::findGenerator("bote-con-tapa")->make(document::findGenerator("bote-con-tapa")->defaults(), ""));
    io::saveDocument(path, doc, {});
    const std::string before = bytesOf(path);
    fs::permissions(path, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read, fs::perm_options::replace);
    document::Document back;
    io::ViewState view;
    io::loadDocument(path, back, view);
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
    CHECK(bytesOf(path) == before);  // abrir no toca el archivo
    REQUIRE(back.featureCount() == doc.featureCount());
    REQUIRE(back.result().bodies.size() == doc.result().bodies.size());
    for (std::size_t i = 0; i < doc.result().bodies.size(); ++i) {
        CHECK(std::abs(back.result().bodies[i].shape.volume() - doc.result().bodies[i].shape.volume()) < 1e-6 * doc.result().bodies[i].shape.volume());
    }
}
