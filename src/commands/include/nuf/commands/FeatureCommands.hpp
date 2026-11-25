#pragma once

// Comandos del historial. Patrón memento: guardan una copia de la operación
// (sus parámetros, nunca geometría) para restaurarla al deshacer.

#include <nuf/commands/Command.hpp>
#include <nuf/document/Feature.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nuf::commands {

// Agrega una operación en «index» (por defecto, en el marcador).
class AddFeature final : public Command {
public:
    explicit AddFeature(std::unique_ptr<document::Feature> feature, std::optional<std::size_t> index = {});

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override;
    // Id asignado (tras execute).
    document::FeatureId id() const { return feature_->id(); }

private:
    std::unique_ptr<document::Feature> feature_;
    std::optional<std::size_t> index_;
    std::size_t insertedAt_ = 0;
};

// Sustituye los parámetros de una operación (editar, renombrar).
class EditFeature final : public Command {
public:
    EditFeature(document::FeatureId id, std::unique_ptr<document::Feature> edited, std::string label = {});

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override { return label_; }

private:
    document::FeatureId id_;
    std::unique_ptr<document::Feature> other_;  // la versión que no está en el documento
    std::string label_;
};

class DeleteFeature final : public Command {
public:
    explicit DeleteFeature(document::FeatureId id);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override;

private:
    document::FeatureId id_;
    std::unique_ptr<document::Feature> removed_;
    std::size_t index_ = 0;
    bool wasActive_ = true;
    std::string name_;
};

class SuppressFeature final : public Command {
public:
    SuppressFeature(document::FeatureId id, bool suppressed);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override;

private:
    document::FeatureId id_;
    bool suppressed_;
    std::string name_;
};

class MoveFeature final : public Command {
public:
    MoveFeature(document::FeatureId id, std::size_t index);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override;

private:
    document::FeatureId id_;
    std::size_t to_;
    std::size_t from_ = 0;
    std::string name_;
};

// Varias órdenes que se hacen y se deshacen como una sola (borrar o mover varias operaciones a la vez): un solo
// Deshacer las devuelve todas.
class Batch final : public Command {
public:
    Batch(std::string label, std::vector<std::unique_ptr<Command>> commands);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override { return label_; }

private:
    std::string label_;
    std::vector<std::unique_ptr<Command>> commands_;
};

// Mueve el marcador del timeline (volver atrás en el historial).
class SetMarker final : public Command {
public:
    explicit SetMarker(std::size_t marker);

    void execute(document::Document& doc) override;
    void undo(document::Document& doc) override;
    std::string label() const override { return "Mover el marcador"; }

private:
    std::size_t to_;
    std::size_t from_ = 0;
};

} // namespace nuf::commands
