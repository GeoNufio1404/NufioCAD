#include <nuf/commands/CommandStack.hpp>
#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/commands/RenameDocument.hpp>
#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using nuf::commands::CommandStack;
using nuf::commands::RenameDocument;
using nuf::document::Document;

TEST_CASE("Deshacer y rehacer restauran el estado", "[commands]")
{
    Document doc;
    CommandStack stack(doc);

    stack.push(std::make_unique<RenameDocument>("soporte_sensor"));
    REQUIRE(doc.name() == "soporte_sensor");
    REQUIRE(stack.canUndo());
    REQUIRE(stack.undoLabel() == "Renombrar diseño");

    stack.undo();
    REQUIRE(doc.name() == "Sin título");
    REQUIRE(stack.canRedo());

    stack.redo();
    REQUIRE(doc.name() == "soporte_sensor");
}

TEST_CASE("Un comando nuevo borra lo que se podía rehacer", "[commands]")
{
    Document doc;
    CommandStack stack(doc);

    stack.push(std::make_unique<RenameDocument>("a"));
    stack.push(std::make_unique<RenameDocument>("b"));
    stack.undo();
    REQUIRE(stack.canRedo());

    stack.push(std::make_unique<RenameDocument>("c"));
    REQUIRE_FALSE(stack.canRedo());

    stack.undo();
    REQUIRE(doc.name() == "a");
}

TEST_CASE("Deshacer con la pila vacía no hace nada", "[commands]")
{
    Document doc;
    CommandStack stack(doc);

    stack.undo();
    stack.redo();

    REQUIRE(doc.name() == "Sin título");
    REQUIRE(doc.revision() == 0);
}

TEST_CASE("El documento notifica cada cambio", "[commands]")
{
    Document doc;
    CommandStack stack(doc);
    int notifications = 0;
    doc.changed.connect([&] { ++notifications; });

    stack.push(std::make_unique<RenameDocument>("x"));
    stack.undo();

    REQUIRE(notifications == 2);
    REQUIRE(doc.revision() == 2);
}

TEST_CASE("Punto guardado: limpio al volver a él, sucio si se pierde", "[commands]")
{
    Document doc;
    CommandStack stack(doc);
    REQUIRE(stack.isClean());  // sin cambios

    stack.push(std::make_unique<RenameDocument>("a"));
    REQUIRE_FALSE(stack.isClean());
    stack.markSaved();
    REQUIRE(stack.isClean());

    stack.push(std::make_unique<RenameDocument>("b"));
    REQUIRE_FALSE(stack.isClean());
    stack.undo();
    REQUIRE(stack.isClean());  // deshacer hasta el guardado
    stack.redo();
    REQUIRE_FALSE(stack.isClean());
    stack.undo();

    // Un cambio nuevo descarta lo rehacible: el estado guardado sigue en el pasado.
    stack.undo();  // por debajo del guardado
    REQUIRE_FALSE(stack.isClean());
    stack.push(std::make_unique<RenameDocument>("c"));
    REQUIRE_FALSE(stack.isClean());
    stack.undo();
    REQUIRE_FALSE(stack.isClean());  // el punto guardado ya no existe

    // Tras abrir otro diseño: se vacía y se marca limpio.
    stack.clear();
    REQUIRE_FALSE(stack.isClean());
    stack.markSaved();
    REQUIRE(stack.isClean());
}

TEST_CASE("Un Batch borra y mueve varias operaciones con un solo Deshacer", "[commands]")
{
    Document doc;
    CommandStack stack(doc);
    for (int i = 0; i < 5; ++i) {
        stack.push(std::make_unique<nuf::commands::AddFeature>(std::make_unique<nuf::document::BoxFeature>()));
    }
    std::vector<nuf::document::FeatureId> original;
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        original.push_back(doc.feature(i).id());
    }
    const auto order = [&] {
        std::vector<nuf::document::FeatureId> ids;
        for (std::size_t i = 0; i < doc.featureCount(); ++i) {
            ids.push_back(doc.feature(i).id());
        }
        return ids;
    };

    std::vector<std::unique_ptr<nuf::commands::Command>> steps;
    steps.push_back(std::make_unique<nuf::commands::DeleteFeature>(original[3]));
    steps.push_back(std::make_unique<nuf::commands::DeleteFeature>(original[1]));
    stack.push(std::make_unique<nuf::commands::Batch>("Borrar 2 operaciones", std::move(steps)));
    REQUIRE(order() == std::vector{original[0], original[2], original[4]});
    REQUIRE(stack.undoLabel() == "Borrar 2 operaciones");

    stack.undo();  // un solo paso las devuelve, en su sitio
    REQUIRE(order() == original);

    std::vector<std::unique_ptr<nuf::commands::Command>> moves;
    moves.push_back(std::make_unique<nuf::commands::MoveFeature>(original[0], 4));
    moves.push_back(std::make_unique<nuf::commands::MoveFeature>(original[1], 4));
    stack.push(std::make_unique<nuf::commands::Batch>("Mover 2 operaciones", std::move(moves)));
    REQUIRE(order() == std::vector{original[2], original[3], original[4], original[0], original[1]});
    stack.undo();
    REQUIRE(order() == original);
}
