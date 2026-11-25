#include <nuf/commands/FeatureCommands.hpp>

#include <nuf/document/Document.hpp>

#include <stdexcept>
#include <utility>

namespace nuf::commands {

// --- AddFeature --------------------------------------------------------------

AddFeature::AddFeature(std::unique_ptr<document::Feature> feature, std::optional<std::size_t> index)
    : feature_(std::move(feature))
    , index_(index)
{
    if (!feature_) {
        throw std::invalid_argument("AddFeature recibió una operación nula");
    }
}

void AddFeature::execute(document::Document& doc)
{
    insertedAt_ = index_.value_or(doc.marker());
    const document::FeatureId id = doc.insertFeature(feature_->clone(), insertedAt_);
    // Rehacer usa el mismo id y nombre.
    feature_->setId(id);
    feature_->setName(doc.findFeature(id)->name());
}

void AddFeature::undo(document::Document& doc)
{
    doc.removeFeature(feature_->id());
}

std::string AddFeature::label() const
{
    return "Agregar " + feature_->name();
}

// --- EditFeature -------------------------------------------------------------

EditFeature::EditFeature(document::FeatureId id, std::unique_ptr<document::Feature> edited, std::string label)
    : id_(id)
    , other_(std::move(edited))
    , label_(std::move(label))
{
    if (!other_) {
        throw std::invalid_argument("EditFeature recibió una operación nula");
    }
    if (label_.empty()) {
        label_ = "Editar " + other_->name();
    }
}

void EditFeature::execute(document::Document& doc)
{
    other_ = doc.replaceFeature(id_, std::move(other_));
}

void EditFeature::undo(document::Document& doc)
{
    other_ = doc.replaceFeature(id_, std::move(other_));
}

// --- DeleteFeature -----------------------------------------------------------

DeleteFeature::DeleteFeature(document::FeatureId id)
    : id_(id)
{
}

void DeleteFeature::execute(document::Document& doc)
{
    const auto index = doc.indexOf(id_);
    if (!index) {
        return;
    }
    index_ = *index;
    wasActive_ = index_ < doc.marker();
    removed_ = doc.removeFeature(id_);
    name_ = removed_->name();
}

void DeleteFeature::undo(document::Document& doc)
{
    if (!removed_) {
        return;
    }
    // insertFeature avanza el marcador si entra antes o en él; si estaba
    // después, se recoloca.
    const std::size_t marker = doc.marker();
    doc.insertFeature(std::move(removed_), index_);
    if (!wasActive_) {
        doc.setMarker(marker);
    }
}

std::string DeleteFeature::label() const
{
    return "Borrar " + name_;
}

// --- SuppressFeature ---------------------------------------------------------

SuppressFeature::SuppressFeature(document::FeatureId id, bool suppressed)
    : id_(id)
    , suppressed_(suppressed)
{
}

void SuppressFeature::execute(document::Document& doc)
{
    if (const auto* f = doc.findFeature(id_)) {
        name_ = f->name();
    }
    doc.setSuppressed(id_, suppressed_);
}

void SuppressFeature::undo(document::Document& doc)
{
    doc.setSuppressed(id_, !suppressed_);
}

std::string SuppressFeature::label() const
{
    return (suppressed_ ? "Suprimir " : "Reactivar ") + name_;
}

// --- MoveFeature -------------------------------------------------------------

MoveFeature::MoveFeature(document::FeatureId id, std::size_t index)
    : id_(id)
    , to_(index)
{
}

void MoveFeature::execute(document::Document& doc)
{
    const auto index = doc.indexOf(id_);
    if (!index) {
        return;
    }
    from_ = *index;
    name_ = doc.findFeature(id_)->name();
    doc.moveFeature(id_, to_);
}

void MoveFeature::undo(document::Document& doc)
{
    doc.moveFeature(id_, from_);
}

std::string MoveFeature::label() const
{
    return "Mover " + name_;
}

// --- Batch -------------------------------------------------------------------

Batch::Batch(std::string label, std::vector<std::unique_ptr<Command>> commands)
    : label_(std::move(label))
    , commands_(std::move(commands))
{
}

void Batch::execute(document::Document& doc)
{
    for (auto& command : commands_) {
        command->execute(doc);
    }
}

void Batch::undo(document::Document& doc)
{
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
        (*it)->undo(doc);
    }
}

// --- SetMarker ---------------------------------------------------------------

SetMarker::SetMarker(std::size_t marker)
    : to_(marker)
{
}

void SetMarker::execute(document::Document& doc)
{
    from_ = doc.marker();
    doc.setMarker(to_);
}

void SetMarker::undo(document::Document& doc)
{
    doc.setMarker(from_);
}

} // namespace nuf::commands
