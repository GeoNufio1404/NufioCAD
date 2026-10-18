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
