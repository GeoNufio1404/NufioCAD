// El texto del sketch se acuerda de lo que dice (Fase 15, U9): se guarda en el archivo y se lee de vuelta.
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/io/DocumentFile.hpp>
#include <nuf/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>

using namespace nuf;

TEST_CASE("El texto, su fuente y su altura viajan en el archivo y las letras siguen unidas", "[sketch][text][io]")
{
    sketch::Sketch s;
    const sketch::EntityId first = s.addLine({0, 0}, {5, 0});
    const sketch::EntityId second = s.addLine({5, 0}, {5, 8});
    for (const sketch::EntityId id : {first, second}) {
        s.entity(id)->locked = true;
        s.entity(id)->textOwner = first;
    }
    s.entity(first)->text = sketch::TextInfo{"Hola ñ", "Arial", 12.5, {3.0, -4.0}};
    const sketch::EntityId loose = s.addLine({20, 0}, {30, 0});  // una línea que no es texto

    document::Document doc;
    document::SketchAttachment at;
    at.origin = document::OriginPlane::XY;
    const document::FeatureId id = doc.addFeature(std::make_unique<document::SketchFeature>(std::move(s), at));

    const auto path = std::filesystem::temp_directory_path() / "nuf_texto.nuf";
    std::filesystem::remove(path);
    io::saveDocument(path, doc, {});
    document::Document reopened;
    io::ViewState view;
    io::loadDocument(path, reopened, view);
    std::filesystem::remove(path);

    const auto* sketchFeature = static_cast<const document::SketchFeature*>(reopened.findFeature(id));
    REQUIRE(sketchFeature != nullptr);
    const sketch::Sketch& back = sketchFeature->sketch();
    const sketch::Entity* owner = back.entity(first);
    REQUIRE(owner != nullptr);
    REQUIRE(owner->text.has_value());
    REQUIRE(owner->text->text == "Hola ñ");
    REQUIRE(owner->text->family == "Arial");
    REQUIRE_THAT(owner->text->height, Catch::Matchers::WithinAbs(12.5, 1e-12));
    REQUIRE_THAT(owner->text->at.x, Catch::Matchers::WithinAbs(3.0, 1e-12));
    REQUIRE_THAT(owner->text->at.y, Catch::Matchers::WithinAbs(-4.0, 1e-12));
    REQUIRE(back.entity(second)->textOwner == first);
    REQUIRE(!back.entity(second)->text.has_value());
    // Lo que no es texto no se marca.
    REQUIRE(back.entity(loose)->textOwner == 0);
    REQUIRE(!back.entity(loose)->text.has_value());
}
