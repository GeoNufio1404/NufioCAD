#include <nuf/document/Generators.hpp>

#include <nuf/document/Features.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nuf::document {

namespace {

std::string number(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.6f", value);
    std::string text(buffer);
    while (text.back() == '0') {
        text.pop_back();
    }
    if (text.back() == '.') {
        text.pop_back();
    }
    return text;
}

// --- Lightbox ------------------------------------------------------------------------------

class Lightbox final : public Generator {
public:
    std::string id() const override { return "lightbox"; }
    std::string title() const override { return "Lightbox"; }
    std::string description() const override
    {
        return "Caja de luz para imprimir: frente fino que difunde la luz, paredes, tapa trasera con labio y "
               "holgura, y un agujero para el cable.";
    }

    std::vector<GeneratorField> fields() const override
    {
        using K = GeneratorField::Kind;
        const auto length = [](std::string key, std::string label, double value, double min, double max,
                               std::string hint = {}, std::string showKey = {}, int showIndex = 0) {
            GeneratorField f;
            f.key = std::move(key);
            f.label = std::move(label);
            f.kind = K::Length;
            f.value = value;
            f.min = min;
            f.max = max;
            f.hint = std::move(hint);
            f.showKey = std::move(showKey);
            f.showIndex = showIndex;
            return f;
        };
        GeneratorField shape;
        shape.key = "forma";
        shape.label = "Forma";
        shape.kind = K::Choice;
        shape.choices = {"Rectangular", "Redonda"};
        return {shape,
                length("ancho", "Ancho", 150.0, 20.0, 1000.0, {}, "forma", 0),
                length("alto", "Alto", 100.0, 20.0, 1000.0, {}, "forma", 0),
                length("diametro", "Diámetro", 120.0, 20.0, 1000.0, {}, "forma", 1),
                length("profundidad", "Profundidad", 40.0, 10.0, 500.0, "De adelante hacia atrás"),
                length("pared", "Pared", 2.4, 0.8, 20.0, "Un múltiplo del ancho de la boquilla"),
                length("frente", "Frente", 1.2, 0.4, 10.0, "Más fino = más luz pasa"),
                length("tapa", "Tapa trasera", 2.4, 0.8, 20.0),
                length("labio", "Labio de la tapa", 3.0, 1.0, 30.0, "Lo que entra en la caja"),
                length("holgura", "Holgura de la tapa", 0.2, 0.0, 2.0, "Para que la tapa encaje sin forzar"),
                length("cable", "Agujero del cable", 6.0, 0.0, 50.0, "0 = sin agujero")};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto v = [&](const char* key) { return values.at(key); };
        const bool round = v("forma") >= 0.5;
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string profundidad = p("profundidad"), pared = p("pared"), frente = p("frente"), tapa = p("tapa"),
                          labio = p("labio"), holgura = p("holgura"), cable = p("cable");
        const std::string ancho = p("ancho"), alto = p("alto"), diametro = p("diametro");

        Recipe recipe;
        recipe.component = "Lightbox";
        recipe.bodyNames = {"Caja", "Tapa"};
        const auto param = [&](const std::string& name, double value) {
            recipe.parameters.push_back({name, number(value)});
        };
        if (round) {
            param(diametro, v("diametro"));
        } else {
            param(ancho, v("ancho"));
            param(alto, v("alto"));
        }
        param(profundidad, v("profundidad"));
        param(pared, v("pared"));
        param(frente, v("frente"));
        param(tapa, v("tapa"));
        param(labio, v("labio"));
        param(holgura, v("holgura"));
        const bool hasCable = v("cable") > 0.0;
        if (hasCable) {
            param(cable, v("cable"));
        }

        const auto addBox = [&](const std::string& name, Operation operation) {
            auto f = std::make_unique<BoxFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->centered = true;
            f->operation = operation;
            return f;
        };
        const auto addCylinder = [&](const std::string& name, Operation operation) {
            auto f = std::make_unique<CylinderFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->operation = operation;
            return f;
        };
        // El tamaño exterior, y lo que hay que restar para una pieza que entre por dentro de la pared.
        const std::string across = round ? diametro : ancho;
        const auto inner = [&](const std::string& extra) { return across + " - 2*" + pared + extra; };
        const std::string innerH = round ? "" : alto + " - 2*" + pared;

        // 1. La caja entera.
        if (round) {
            auto f = addCylinder("Lightbox: caja", Operation::NewBody);
            f->expressions = {{"diameter", diametro}, {"height", profundidad}};
            recipe.features.push_back(std::move(f));
        } else {
            auto f = addBox("Lightbox: caja", Operation::NewBody);
            f->expressions = {{"width", ancho}, {"depth", alto}, {"height", profundidad}};
            recipe.features.push_back(std::move(f));
        }
        // 2. El hueco: desde el frente (que queda con su grosor) hasta salir por detrás.
        if (round) {
            auto f = addCylinder("Lightbox: hueco", Operation::Cut);
            f->expressions = {{"diameter", inner("")}, {"z", frente}, {"height", profundidad}};
            recipe.features.push_back(std::move(f));
        } else {
            auto f = addBox("Lightbox: hueco", Operation::Cut);
            f->expressions = {{"width", inner("")}, {"depth", innerH}, {"z", frente}, {"height", profundidad}};
            recipe.features.push_back(std::move(f));
        }
        // 3. El agujero del cable, por la pared de abajo (el plano XZ mira hacia -Y: «z» es hacia abajo).
        if (hasCable) {
            auto f = addCylinder("Lightbox: agujero del cable", Operation::Cut);
            f->base.origin = OriginPlane::XZ;
            f->symmetric = true;
            f->expressions = {{"diameter", cable},
                              {"y", frente + " + (" + profundidad + " - " + frente + ")/2"},
                              {"z", (round ? diametro : alto) + "/2 - " + pared + "/2"},
                              {"height", pared + " + 2"}};
            recipe.features.push_back(std::move(f));
        }
        // 4. La tapa: una placa al lado, con un labio que entra en la caja.
        const std::string beside = across + " + 10";
        if (round) {
            auto f = addCylinder("Lightbox: tapa", Operation::NewBody);
            f->expressions = {{"x", beside}, {"diameter", diametro}, {"height", tapa}};
            recipe.features.push_back(std::move(f));
            auto lip = addCylinder("Lightbox: labio de la tapa", Operation::Join);
            lip->expressions = {{"x", beside}, {"diameter", inner(" - 2*" + holgura)}, {"height", tapa + " + " + labio}};
            recipe.features.push_back(std::move(lip));
        } else {
            auto f = addBox("Lightbox: tapa", Operation::NewBody);
            f->expressions = {{"x", beside}, {"width", ancho}, {"depth", alto}, {"height", tapa}};
            recipe.features.push_back(std::move(f));
            auto lip = addBox("Lightbox: labio de la tapa", Operation::Join);
            lip->expressions = {{"x", beside},
                                {"width", inner(" - 2*" + holgura)},
                                {"depth", innerH + " - 2*" + holgura},
                                {"height", tapa + " + " + labio}};
            recipe.features.push_back(std::move(lip));
        }
        return recipe;
    }
};

} // namespace

GeneratorValues Generator::defaults() const
{
    GeneratorValues values;
    for (const GeneratorField& f : fields()) {
        values[f.key] = f.value;
    }
    return values;
}

const std::vector<const Generator*>& generators()
{
    static const Lightbox lightbox;
    static const std::vector<const Generator*> all = {&lightbox};
    return all;
}

const Generator* findGenerator(const std::string& id)
{
    for (const Generator* g : generators()) {
        if (g->id() == id) {
            return g;
        }
    }
    return nullptr;
}

std::string freeSuffix(const Document& doc, const Generator& generator, const GeneratorValues& values)
{
    for (int n = 1; n < 1000; ++n) {
        const std::string suffix = n == 1 ? std::string() : "_" + std::to_string(n);
        const Recipe recipe = generator.make(values, suffix);
        const bool clash = std::ranges::any_of(recipe.parameters, [&](const Parameter& p) {
            return std::ranges::any_of(doc.parameters(), [&](const Parameter& old) { return old.name == p.name; });
        });
        if (!clash) {
            return suffix;
        }
    }
    return "_x";
}

void applyRecipe(Document& doc, const Recipe& recipe)
{
    std::vector<Parameter> parameters = doc.parameters();
    for (const Parameter& p : recipe.parameters) {
        parameters.push_back(p);
    }
    doc.setParameters(parameters);
    std::vector<std::string> before;
    for (const Body& body : doc.result().bodies) {
        before.push_back(body.key);
    }
    for (const auto& feature : recipe.features) {
        doc.insertFeature(feature->clone(), doc.marker());
    }
    std::map<std::string, BodyInfo> info = doc.bodyInfo();
    std::size_t next = 0;
    for (const Body& body : doc.result().bodies) {
        if (std::ranges::find(before, body.key) != before.end()) {
            continue;
        }
        info[body.key] = {next < recipe.bodyNames.size() ? recipe.bodyNames[next] : std::string(), recipe.component};
        ++next;
    }
    doc.setBodyInfo(info);
}

} // namespace nuf::document
