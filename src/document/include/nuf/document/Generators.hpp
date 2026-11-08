#pragma once

// Herramientas generadoras (tipo MakerLab): hacen unas preguntas y producen un diseño normal (operaciones del
// historial ligadas a parámetros con nombre, cuerpos con nombre y un componente), que luego se puede seguir
// editando con el resto del CAD. Aquí solo está la receta; la interfaz y el comando que la aplica, aparte.

#include <nuf/document/Document.hpp>
#include <nuf/document/Feature.hpp>

#include <nuf/sketch/Sketch.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nuf::document {

// Una pregunta del formulario del generador.
struct GeneratorField {
    enum class Kind { Length, Choice, Text, Font, Count, Angle };
    std::string key;
    std::string label;
    std::string hint;                  // ayuda breve bajo el campo
    Kind kind = Kind::Length;
    double value = 0.0;                // valor por defecto, o la opción elegida (Choice)
    double min = 0.0;                  // mm (Length), cantidad (Count) o grados (Angle)
    double max = 100000.0;
    std::vector<std::string> choices;  // Choice
    std::string text;                  // valor por defecto de Text y Font (la fuente vacía = la de la interfaz)
    // Solo se muestra si la pregunta «showKey» (de opciones) vale «showIndex» (vacía: siempre).
    std::string showKey;
    int showIndex = 0;
};

// Lo elegido en el formulario: números (longitudes en mm, opciones como índice) y textos.
struct GeneratorValues : std::map<std::string, double> {
    std::map<std::string, std::string> texts;
};

// El contorno de un texto como curvas del sketch (letras en el plano XY, la base del texto en Y = 0, empezando
// en X = 0): lo pone la interfaz, que es la que sabe de fuentes.
using TextOutliner =
    std::function<std::vector<sketch::Geometry>(const std::string& text, double height, const std::string& family)>;
void setTextOutliner(TextOutliner outliner);
std::vector<sketch::Geometry> textOutline(const std::string& text, double height, const std::string& family);

// Las operaciones de una receta que usan el sketch hecho en la misma receta lo nombran con este id; al aplicarla
// se cambia por el real.
constexpr FeatureId kRecipeSketch = 0x7FFFFFF0;
// Una copia de la operación con el sketch de la receta ya resuelto.
std::unique_ptr<Feature> instantiate(const Feature& feature, FeatureId recipeSketch);

// Lo que hace un generador: parámetros, operaciones y cómo llamar a los cuerpos que salgan.
struct Recipe {
    std::vector<Parameter> parameters;
    std::vector<std::unique_ptr<Feature>> features;
    std::vector<std::string> bodyNames;  // los de los cuerpos nuevos, en el orden en que salen
    std::string component;               // al que se agrupan (vacío: ninguno)
    // De dónde sale (id del generador, sufijo y respuestas codificadas); lo pone quien arma la receta para que
    // las operaciones queden marcadas y se pueda reabrir el formulario. «lead» lo pone el comando.
    GeneratorOrigin origin;
};

class Generator {
public:
    virtual ~Generator() = default;

    virtual std::string id() const = 0;
    virtual std::string title() const = 0;
    virtual std::string description() const = 0;
    // Un resumen de lo que saldría con esos valores (relación de transmisión…), para mostrarlo en el formulario.
    virtual std::string summary(const GeneratorValues& /*values*/) const { return {}; }
    // Nombre del icono (de la interfaz) de su botón.
    virtual std::string icon() const { return "lightbox-solid"; }
    virtual std::vector<GeneratorField> fields() const = 0;
    // Dónde se dibuja la flecha de una medida mientras se edita su pregunta: a lo largo de «axis» (x, y o z), sobre el
    // cuerpo «body» de la receta (por orden de salida), desde su borde mínimo (suma el valor) o máximo (lo resta), por
    // el centro del cuerpo en los otros ejes. Solo las preguntas que son una medida sobre una pieza.
    struct ArrowSpec {
        int body = 0;
        char axis = 'x';
        bool fromMax = false;
        bool centered = false;  // la flecha va centrada en el cuerpo, de -valor/2 a +valor/2 (un diámetro)
    };
    virtual std::map<std::string, ArrowSpec> arrows() const { return {}; }
    // «suffix» se agrega a los nombres de los parámetros (« », «_2», «_3»…) para no chocar con los que ya haya.
    virtual Recipe make(const GeneratorValues& values, const std::string& suffix) const = 0;

    // Los valores por defecto de las preguntas.
    GeneratorValues defaults() const;
};

// Las respuestas de un formulario como texto (para guardarlas en las operaciones) y de vuelta.
std::string encodeValues(const GeneratorValues& values);
GeneratorValues decodeValues(const std::string& text);

// Todos los generadores.
const std::vector<const Generator*>& generators();
const Generator* findGenerator(const std::string& id);

// El primer sufijo («», «_2», «_3»…) con el que ningún parámetro de la receta choca con los del documento.
std::string freeSuffix(const Document& doc, const Generator& generator, const GeneratorValues& values);

// Las operaciones que salieron de un mismo generador (las que tienen ese «lead»), en el orden del historial.
std::vector<FeatureId> generatedGroup(const Document& doc, FeatureId lead);

// Aplica la receta a un documento (parámetros, operaciones al final del historial y nombres de cuerpos).
// Sirve para la vista previa en un documento vacío; el comando de la interfaz hace lo mismo con deshacer.
void applyRecipe(Document& doc, const Recipe& recipe);

} // namespace nuf::document
