#pragma once

// Herramientas generadoras (tipo MakerLab): hacen unas preguntas y producen un diseño normal (operaciones del
// historial ligadas a parámetros con nombre, cuerpos con nombre y un componente), que luego se puede seguir
// editando con el resto del CAD. Aquí solo está la receta; la interfaz y el comando que la aplica, aparte.

#include <nuf/document/Document.hpp>
#include <nuf/document/Feature.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nuf::document {

// Una pregunta del formulario del generador.
struct GeneratorField {
    enum class Kind { Length, Choice };
    std::string key;
    std::string label;
    std::string hint;                  // ayuda breve bajo el campo
    Kind kind = Kind::Length;
    double value = 0.0;                // valor por defecto, o la opción elegida (Choice)
    double min = 0.0;                  // mm (Length)
    double max = 100000.0;
    std::vector<std::string> choices;  // Choice
    // Solo se muestra si la pregunta «showKey» (de opciones) vale «showIndex» (vacía: siempre).
    std::string showKey;
    int showIndex = 0;
};

using GeneratorValues = std::map<std::string, double>;

// Lo que hace un generador: parámetros, operaciones y cómo llamar a los cuerpos que salgan.
struct Recipe {
    std::vector<Parameter> parameters;
    std::vector<std::unique_ptr<Feature>> features;
    std::vector<std::string> bodyNames;  // los de los cuerpos nuevos, en el orden en que salen
    std::string component;               // al que se agrupan (vacío: ninguno)
};

class Generator {
public:
    virtual ~Generator() = default;

    virtual std::string id() const = 0;
    virtual std::string title() const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<GeneratorField> fields() const = 0;
    // «suffix» se agrega a los nombres de los parámetros (« », «_2», «_3»…) para no chocar con los que ya haya.
    virtual Recipe make(const GeneratorValues& values, const std::string& suffix) const = 0;

    // Los valores por defecto de las preguntas.
    GeneratorValues defaults() const;
};

// Todos los generadores.
const std::vector<const Generator*>& generators();
const Generator* findGenerator(const std::string& id);

// El primer sufijo («», «_2», «_3»…) con el que ningún parámetro de la receta choca con los del documento.
std::string freeSuffix(const Document& doc, const Generator& generator, const GeneratorValues& values);

// Aplica la receta a un documento (parámetros, operaciones al final del historial y nombres de cuerpos).
// Sirve para la vista previa en un documento vacío; el comando de la interfaz hace lo mismo con deshacer.
void applyRecipe(Document& doc, const Recipe& recipe);

} // namespace nuf::document
