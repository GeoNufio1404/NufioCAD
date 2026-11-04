#include <nuf/commands/CommandStack.hpp>
#include <nuf/commands/RenameDocument.hpp>
#include <nuf/document/Document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>

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
