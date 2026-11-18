#include <nuf/document/Generators.hpp>

#include <nuf/document/Features.hpp>
#include <nuf/geometry/DisplayUnits.hpp>
#include <nuf/sketch/Dxf.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

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
// Como la «Light Box» de MakerLab: una caja abierta por delante (fondo cerrado y paredes), y una tapa delantera
// hecha de un marco, un labio que entra en la caja y la cara central (donde va la imagen). La caja y la tapa son
// dos cuerpos; la tapa es un solo cuerpo (placa más labio), sin piezas sueltas que manejar.

class Lightbox final : public Generator {
public:
    std::string category() const override { return "Cajas"; }
    std::string id() const override { return "lightbox"; }
    std::string title() const override { return "Lightbox"; }
    std::string description() const override
    {
        return "Caja de luz para imprimir, como la de MakerLab: caja abierta por delante y tapa delantera de un solo "
               "cuerpo, con su labio que entra en la caja.";
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
        GeneratorField cableWall;
        cableWall.key = "pared_cable";
        cableWall.label = "Salida del cable";
        cableWall.kind = K::Choice;
        cableWall.choices = {"Atrás (en el fondo)", "Pared de abajo", "Pared de arriba", "Pared izquierda", "Pared derecha"};
        cableWall.hint = "Por dónde sale el cable de la tira LED";
        return {shape,
                length("ancho", "Ancho (exterior)", 175.0, 30.0, 1000.0, "La imagen mide esto menos 2 paredes", "forma", 0),
                length("alto", "Alto (exterior)", 131.0, 30.0, 1000.0, {}, "forma", 0),
                length("diametro", "Diámetro (exterior)", 150.0, 30.0, 1000.0, {}, "forma", 1),
                length("profundidad", "Profundidad total", 37.0, 10.0, 500.0, "Fondo + hueco, sin la tapa delantera"),
                length("pared", "Pared", 2.0, 0.8, 20.0, "Un múltiplo del ancho de la boquilla"),
                length("fondo", "Fondo de la caja", 2.0, 0.8, 20.0, "La pared de atrás"),
                length("frente", "Frente (marco e imagen)", 0.8, 0.4, 10.0, "Grosor de la tapa: más fino, más luz pasa"),
                length("esquina", "Radio de esquinas", 2.0, 0.0, 30.0, "Solo la parte exterior", "forma", 0),
                length("labio", "Alto del labio", 5.0, 1.0, 30.0, "Lo que entra en la caja"),
                length("labio_pared", "Grosor del labio", 1.0, 0.4, 10.0),
                length("holgura", "Holgura del labio", 0.1, 0.0, 2.0, "Aire entre el labio y la pared, por cada lado"),
                length("cable", "Agujero del cable", 6.0, 0.0, 50.0, "0 = sin agujero"),
                cableWall};
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        const auto topCorner = [](const GeneratorValues& v) { return std::array<double, 3>{0.0, 0.0, v.at("profundidad")}; };
        ArrowSpec corner{0, 'x', false};
        corner.at = topCorner;  // el radio de la esquina, a lo largo del borde de arriba que sale de ella
        // La tapa es el cuerpo 1: el frente va de su cara de abajo (Z = 0), el labio se mide desde su borde de arriba
        // y el grosor del labio empieza en su pared exterior (a pared + holgura del borde de la tapa).
        ArrowSpec lipWall{1, 'x', false};
        lipWall.at = [](const GeneratorValues& v) {
            return std::array<double, 3>{v.at("pared") + v.at("holgura"), kCenter, v.at("frente") + v.at("labio") / 2.0};
        };
        return {{"ancho", {0, 'x', false}}, {"diametro", {0, 'x', false}}, {"alto", {0, 'y', false}},
                {"profundidad", {0, 'z', false}}, {"pared", {0, 'x', true}}, {"fondo", {0, 'z', false}},
                {"frente", {1, 'z', false}}, {"labio", {1, 'z', true}}, {"labio_pared", lipWall},
                {"esquina", corner}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto v = [&](const char* key) { return values.at(key); };
        const bool round = v("forma") >= 0.5;
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string profundidad = p("profundidad"), pared = p("pared"), fondo = p("fondo"), frente = p("frente"),
                          esquina = p("esquina"), labio = p("labio"), labioPared = p("labio_pared"),
                          holgura = p("holgura"), cable = p("cable");
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
            param(esquina, v("esquina"));
        }
        param(profundidad, v("profundidad"));
        param(pared, v("pared"));
        param(fondo, v("fondo"));
        param(frente, v("frente"));
        param(labio, v("labio"));
        param(labioPared, v("labio_pared"));
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
        // «Pieza» de planta rectangular o redonda con un tamaño dado por expresiones (ancho/alto o diámetro).
        // Con «x»: la posición X de la pieza (la tapa va al lado).
        const auto shapeAt = [&](const std::string& name, Operation operation, const std::string& w, const std::string& h,
                                 const std::string& z, const std::string& height, const std::string& x,
                                 const std::string& radius) -> std::unique_ptr<Feature> {
            if (round) {
                auto f = addCylinder(name, operation);
                f->expressions = {{"diameter", w}, {"z", z}, {"height", height}};
                if (!x.empty()) {
                    f->expressions["x"] = x;
                }
                return f;
            }
            auto f = addBox(name, operation);
            f->expressions = {{"width", w}, {"depth", h}, {"z", z}, {"height", height}};
            if (!x.empty()) {
                f->expressions["x"] = x;
            }
            if (!radius.empty()) {
                f->expressions["radius"] = radius;
            }
            return f;
        };
        const std::string W = round ? diametro : ancho;   // tamaño exterior (el diámetro hace de ancho y de alto)
        const std::string H = round ? diametro : alto;
        const std::string inW = W + " - 2*" + pared;       // interior de la pared = tamaño de la imagen
        const std::string inH = H + " - 2*" + pared;
        const std::string lipW = inW + " - 2*" + holgura;  // el labio entra con holgura
        const std::string lipH = inH + " - 2*" + holgura;
        const std::string lipInW = lipW + " - 2*" + labioPared;
        const std::string lipInH = lipH + " - 2*" + labioPared;
        const std::string beside = W + " + 10";            // la tapa se imprime al lado

        // 1. La caja, con el fondo en Z = 0 (apoyado en la cama) y abierta por delante (+Z).
        recipe.features.push_back(shapeAt("Lightbox: caja", Operation::NewBody, W, H, "0", profundidad, "", esquina));
        // 2. El hueco, desde el fondo hasta salir por delante (esquinas interiores vivas).
        recipe.features.push_back(shapeAt("Lightbox: hueco", Operation::Cut, inW, inH, fondo, profundidad, "", ""));
        // 3. El agujero del cable: por detrás (atraviesa el fondo) o por una pared.
        const int wall = int(v("pared_cable"));
        if (hasCable) {
            auto f = addCylinder("Lightbox: agujero del cable", Operation::Cut);
            if (wall == 0) {
                f->expressions = {{"diameter", cable}, {"y", "-(" + H + ")/4"}, {"z", "-1"}, {"height", fondo + " + 2"}};
            } else {
                // El plano XZ mira hacia -Y y el YZ hacia +X: «z» es hacia fuera de la pared de abajo / de la
                // derecha, y su opuesto hacia la de arriba / la izquierda.
                const bool alongY = wall <= 2;  // abajo o arriba
                f->base.origin = alongY ? OriginPlane::XZ : OriginPlane::YZ;
                f->symmetric = true;
                const std::string half = (alongY ? H : W) + "/2 - " + pared + "/2";
                const bool positive = wall == 1 || wall == 4;  // abajo y derecha
                f->expressions = {{"diameter", cable},
                                  {"y", fondo + " + (" + profundidad + " - " + fondo + ")/2"},
                                  {"z", positive ? half : "-(" + half + ")"},
                                  {"height", pared + " + 2"}};
            }
            recipe.features.push_back(std::move(f));
        }
        // 4. La tapa, un solo cuerpo: la placa del tamaño de la caja (marco y cara central juntos)...
        recipe.features.push_back(shapeAt("Lightbox: tapa", Operation::NewBody, W, H, "0", frente, beside, esquina));
        // 5. ...y el labio: un anillo que sale hacia atrás desde la cara interior de la tapa y entra en la caja. Se
        // une a la placa (empieza 0,01 mm dentro de ella para que la unión sea firme).
        recipe.features.push_back(
            shapeAt("Lightbox: labio", Operation::Join, lipW, lipH, frente + " - 0.01", labio + " + 0.01", beside, ""));
        recipe.features.push_back(
            shapeAt("Lightbox: hueco del labio", Operation::Cut, lipInW, lipInH, frente, labio + " + 1", beside, ""));
        return recipe;
    }
};

// --- Caja con tapa -------------------------------------------------------------------------
// Una caja rectangular abierta por arriba y su tapa, que cierra con un labio a presión o con tornillos en las
// esquinas. Sirve para electrónica, cajas de regalo, organizadores…

class BoxWithLid final : public Generator {
public:
    std::string category() const override { return "Cajas"; }
    std::string id() const override { return "caja-con-tapa"; }
    std::string title() const override { return "Caja con tapa"; }
    std::string icon() const override { return "box-solid"; }
    std::string description() const override
    {
        return "Caja rectangular abierta por arriba y su tapa, que cierra con un labio a presión o con tornillos "
               "en las esquinas.";
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
        GeneratorField closure;
        closure.key = "cierre";
        closure.label = "Cierre";
        closure.kind = K::Choice;
        closure.choices = {"Labio a presión", "Tornillos en las esquinas"};
        return {length("ancho", "Ancho (exterior)", 100.0, 20.0, 1000.0),
                length("fondo", "Fondo (exterior)", 60.0, 20.0, 1000.0, "El otro lado de la planta"),
                length("alto", "Alto de la caja", 40.0, 10.0, 500.0, "Sin la tapa"),
                length("pared", "Pared", 2.0, 0.8, 20.0, "Un múltiplo del ancho de la boquilla"),
                length("base", "Base", 2.0, 0.8, 20.0, "Grosor del fondo"),
                length("esquina", "Radio de esquinas", 3.0, 0.0, 30.0),
                length("tapa", "Grosor de la tapa", 2.0, 0.8, 20.0),
                closure,
                length("labio", "Alto del labio", 3.0, 1.0, 30.0, "Lo que entra la tapa en la caja", "cierre", 0),
                length("labio_pared", "Grosor del labio", 1.2, 0.4, 10.0, {}, "cierre", 0),
                length("holgura", "Holgura del labio", 0.2, 0.0, 2.0, "Aire entre el labio y la pared, por cada lado",
                       "cierre", 0),
                length("poste", "Diámetro de los postes", 8.0, 4.0, 30.0, "Uno en cada esquina, por dentro", "cierre", 1),
                length("tornillo", "Agujero del poste", 2.5, 1.0, 10.0, "Para roscar el tornillo (M3: 2,5)", "cierre", 1),
                length("pasante", "Agujero de la tapa", 3.4, 1.0, 12.0, "Para que el tornillo pase (M3: 3,4)", "cierre", 1)};
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        // Los postes están en las cuatro esquinas, por dentro: se marca el de la esquina de X e Y máximos, en lo alto.
        const auto post = [](const char* heightKey, int body) {
            ArrowSpec spec{body, 'x', false, true};
            spec.at = [heightKey](const GeneratorValues& v) {
                const double inset = v.at("pared") + v.at("poste") / 2.0 - 0.5;
                return std::array<double, 3>{v.at("ancho") - inset, v.at("fondo") - inset, v.at(heightKey)};
            };
            return spec;
        };
        ArrowSpec corner{0, 'x', false};
        corner.at = [](const GeneratorValues& v) { return std::array<double, 3>{0.0, 0.0, v.at("alto")}; };
        ArrowSpec lipWall{1, 'x', false};
        lipWall.at = [](const GeneratorValues& v) {
            return std::array<double, 3>{v.at("pared") + v.at("holgura"), kCenter, v.at("tapa") + v.at("labio") / 2.0};
        };
        return {{"ancho", {0, 'x', false}}, {"fondo", {0, 'y', false}}, {"alto", {0, 'z', false}},
                {"pared", {0, 'x', true}}, {"base", {0, 'z', false}}, {"tapa", {1, 'z', false}}, {"labio", {1, 'z', true}},
                {"esquina", corner}, {"labio_pared", lipWall}, {"poste", post("alto", 0)}, {"tornillo", post("alto", 0)},
                {"pasante", post("tapa", 1)}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto v = [&](const char* key) { return values.at(key); };
        const bool screws = v("cierre") >= 0.5;
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string ancho = p("ancho"), fondo = p("fondo"), alto = p("alto"), pared = p("pared"), base = p("base"),
                          esquina = p("esquina"), tapa = p("tapa"), labio = p("labio"), labioPared = p("labio_pared"),
                          holgura = p("holgura"), poste = p("poste"), tornillo = p("tornillo"), pasante = p("pasante");

        Recipe recipe;
        recipe.component = "Caja con tapa";
        recipe.bodyNames = {"Caja", "Tapa"};
        const auto param = [&](const std::string& name, double value) {
            recipe.parameters.push_back({name, number(value)});
        };
        param(ancho, v("ancho"));
        param(fondo, v("fondo"));
        param(alto, v("alto"));
        param(pared, v("pared"));
        param(base, v("base"));
        param(esquina, v("esquina"));
        param(tapa, v("tapa"));
        if (screws) {
            param(poste, v("poste"));
            param(tornillo, v("tornillo"));
            param(pasante, v("pasante"));
        } else {
            param(labio, v("labio"));
            param(labioPared, v("labio_pared"));
            param(holgura, v("holgura"));
        }

        const auto addBox = [&](const std::string& name, Operation operation, const std::string& w, const std::string& d,
                                const std::string& z, const std::string& h, const std::string& x, const std::string& radius) {
            auto f = std::make_unique<BoxFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->centered = true;
            f->operation = operation;
            f->expressions = {{"width", w}, {"depth", d}, {"z", z}, {"height", h}};
            if (!x.empty()) {
                f->expressions["x"] = x;
            }
            if (!radius.empty()) {
                f->expressions["radius"] = radius;
            }
            return f;
        };
        const auto addCylinder = [&](const std::string& name, Operation operation, const std::string& diameter,
                                     const std::string& x, const std::string& y, const std::string& z, const std::string& h) {
            auto f = std::make_unique<CylinderFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->operation = operation;
            f->expressions = {{"diameter", diameter}, {"x", x}, {"y", y}, {"z", z}, {"height", h}};
            return f;
        };
        const std::string inW = ancho + " - 2*" + pared;
        const std::string inD = fondo + " - 2*" + pared;
        const std::string beside = ancho + " + 10";

        // 1. La caja y su hueco (abierta por arriba).
        recipe.features.push_back(addBox("Caja: cuerpo", Operation::NewBody, ancho, fondo, "0", alto, "", esquina));
        recipe.features.push_back(addBox("Caja: hueco", Operation::Cut, inW, inD, base, alto, "", ""));

        // Los postes de las cuatro esquinas, tangentes a la pared por dentro (con 0,5 mm metidos en ella para que
        // se unan), con su agujero para roscar.
        const std::string px = ancho + "/2 - " + pared + " - " + poste + "/2 + 0.5";
        const std::string py = fondo + "/2 - " + pared + " - " + poste + "/2 + 0.5";
        const std::string depth = "min(10; " + alto + " - " + base + " - 1)";  // el agujero, como mucho 10 mm
        const char* corners[4][2] = {{"", ""}, {"-", ""}, {"", "-"}, {"-", "-"}};
        if (screws) {
            for (int i = 0; i < 4; ++i) {
                const std::string sx = corners[i][0], sy = corners[i][1];
                recipe.features.push_back(addCylinder("Caja: poste " + std::to_string(i + 1), Operation::Join, poste,
                                                       sx + "(" + px + ")", sy + "(" + py + ")", base + " - 0.5",
                                                       alto + " - " + base + " + 0.5"));
            }
            for (int i = 0; i < 4; ++i) {
                const std::string sx = corners[i][0], sy = corners[i][1];
                recipe.features.push_back(addCylinder("Caja: agujero del poste " + std::to_string(i + 1), Operation::Cut,
                                                       tornillo, sx + "(" + px + ")", sy + "(" + py + ")",
                                                       alto + " - " + depth, depth + " + 1"));
            }
        }

        // 2. La tapa, al lado (apoyada en la cama).
        recipe.features.push_back(addBox("Tapa: placa", Operation::NewBody, ancho, fondo, "0", tapa, beside, esquina));
        if (screws) {
            for (int i = 0; i < 4; ++i) {
                const std::string sx = corners[i][0], sy = corners[i][1];
                recipe.features.push_back(addCylinder("Tapa: agujero " + std::to_string(i + 1), Operation::Cut, pasante,
                                                       beside + (sx.empty() ? " + " : " - ") + "(" + px + ")", sy + "(" + py + ")", "-1",
                                                       tapa + " + 2"));
            }
        } else {
            // El labio: un anillo que sale hacia arriba de la placa y entra en la caja con holgura.
            const std::string lipW = inW + " - 2*" + holgura, lipD = inD + " - 2*" + holgura;
            recipe.features.push_back(addBox("Tapa: labio", Operation::Join, lipW, lipD, tapa + " - 0.01", labio + " + 0.01", beside, ""));
            recipe.features.push_back(addBox("Tapa: hueco del labio", Operation::Cut, lipW + " - 2*" + labioPared,
                                             lipD + " - 2*" + labioPared, tapa, labio + " + 1", beside, ""));
        }
        return recipe;
    }
};

// --- Letrero y llavero con texto -----------------------------------------------------------
// Una placa con el texto en relieve (cuerpo aparte, para otro color) o grabado, con o sin anilla para llavero. Las
// letras salen del contorno de la fuente elegida y quedan en un sketch editable.

class Sign final : public Generator {
public:
    std::string category() const override { return "Letreros"; }
    std::string id() const override { return "letrero"; }
    std::string title() const override { return "Letrero o llavero"; }
    std::string icon() const override { return "sign-solid"; }
    std::string description() const override
    {
        return "Placa con tu texto en relieve (en otro cuerpo, para otro color) o grabado, con una anilla opcional "
               "para llavero.";
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
        const auto choice = [](std::string key, std::string label, std::vector<std::string> choices) {
            GeneratorField f;
            f.key = std::move(key);
            f.label = std::move(label);
            f.kind = K::Choice;
            f.choices = std::move(choices);
            return f;
        };
        GeneratorField text;
        text.key = "texto";
        text.label = "Texto";
        text.kind = K::Text;
        text.text = "Nufio";
        GeneratorField font;
        font.key = "fuente";
        font.label = "Fuente";
        font.kind = K::Font;
        // El orden de las opciones no cambia (los diseños guardados guardan el índice): la nueva va al final y es la
        // que sale por defecto.
        GeneratorField mode = choice("modo", "Texto", {"En relieve (otro cuerpo)", "Grabado (hundido)", "Solo letras",
                                                       "En relieve (un solo cuerpo)"});
        mode.value = 3.0;
        mode.hint = "«Otro cuerpo» deja el texto aparte para imprimirlo en otro color";
        return {choice("tipo", "Tipo", {"Letrero", "Llavero"}),
                text,
                font,
                length("altura_texto", "Tamaño del texto", 20.0, 3.0, 300.0, "Tamaño de la fuente; las letras mayúsculas miden ~70 %"),
                mode,
                length("margen", "Margen de la placa", 5.0, 1.0, 100.0, "Entre el texto y el borde"),
                length("grosor", "Grosor de la placa", 3.0, 0.8, 50.0),
                length("relieve", "Relieve o profundidad", 1.2, 0.2, 50.0, "Cuánto sobresale (o se hunde) el texto"),
                length("esquina", "Radio de esquinas", 3.0, 0.0, 50.0),
                length("anilla", "Diámetro de la anilla", 12.0, 5.0, 60.0, "El círculo que rodea el agujero", "tipo", 1),
                length("agujero", "Agujero de la anilla", 5.0, 2.0, 40.0, {}, "tipo", 1)};
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        // En un llavero la placa empieza a la derecha de la anilla, que sobresale (anilla − 2 mm) por la izquierda.
        ArrowSpec margin{0, 'x', false};
        margin.at = [](const GeneratorValues& v) {
            return std::array<double, 3>{v.at("tipo") >= 0.5 ? v.at("anilla") - 2.0 : 0.0, kCenter, v.at("grosor")};
        };
        ArrowSpec corner{0, 'x', false};
        corner.at = [](const GeneratorValues& v) {
            return std::array<double, 3>{v.at("tipo") >= 0.5 ? v.at("anilla") - 2.0 : 0.0, 0.0, v.at("grosor")};
        };
        ArrowSpec ring{0, 'x', false};
        ring.at = [](const GeneratorValues& v) { return std::array<double, 3>{0.0, kCenter, v.at("grosor")}; };
        ArrowSpec hole{0, 'x', false, true};
        hole.at = [](const GeneratorValues& v) { return std::array<double, 3>{v.at("anilla") / 2.0, kCenter, v.at("grosor")}; };
        return {{"grosor", {0, 'z', false}}, {"relieve", {1, 'z', false}}, {"margen", margin}, {"esquina", corner},
                {"anilla", ring}, {"agujero", hole}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto v = [&](const char* key) { return values.at(key); };
        const bool keychain = v("tipo") >= 0.5;
        const int mode = int(v("modo"));  // 0 relieve, 1 grabado, 2 solo letras
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string textKey = values.texts.count("texto") ? values.texts.at("texto") : std::string();
        const std::string family = values.texts.count("fuente") ? values.texts.at("fuente") : std::string();

        // El contorno de las letras, centrado en el origen.
        const std::vector<sketch::Geometry> outline = textOutline(textKey, v("altura_texto"), family);
        if (outline.empty()) {
            throw std::runtime_error("No se pudo crear el texto (¿está vacío o la fuente no existe?)");
        }
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        for (const sketch::Geometry& g : outline) {
            for (const sketch::Vec2& point : sketch::polyline(g)) {
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }
        }
        const sketch::Vec2 shift{-(minX + maxX) / 2.0, -(minY + maxY) / 2.0};
        sketch::Sketch lettering;
        for (const sketch::EntityId id : sketch::importDrawing(lettering, {outline, {}}, shift)) {
            lettering.entity(id)->locked = true;
        }
        // Las regiones que rellenan el hueco de una letra (el interior de una «O») no son letra: se descartan.
        const std::vector<sketch::Profile> found = sketch::findProfiles(lettering);
        const auto idsOf = [](const sketch::Loop& loop) {
            std::vector<sketch::EntityId> ids;
            for (const sketch::ProfileEdge& edge : loop.edges) {
                ids.push_back(edge.entity);
            }
            std::sort(ids.begin(), ids.end());
            return ids;
        };
        std::vector<std::vector<sketch::EntityId>> holeLoops;
        for (const sketch::Profile& profile : found) {
            for (const sketch::Loop& hole : profile.holes) {
                holeLoops.push_back(idsOf(hole));
            }
        }
        std::vector<sketch::Profile> profiles;
        for (const sketch::Profile& profile : found) {
            if (std::find(holeLoops.begin(), holeLoops.end(), idsOf(profile.outer)) == holeLoops.end()) {
                profiles.push_back(profile);
            }
        }
        if (profiles.empty()) {
            throw std::runtime_error("El texto no forma letras cerradas con esa fuente");
        }

        const std::string plateW = p("placa_ancho"), plateH = p("placa_alto"), thick = p("grosor"), relief = p("relieve"),
                          corner = p("esquina"), ring = p("anilla"), hole = p("agujero");
        const double width = maxX - minX + 2.0 * v("margen");
        const double height = maxY - minY + 2.0 * v("margen");

        Recipe recipe;
        recipe.component = keychain ? "Llavero" : "Letrero";
        recipe.bodyNames = mode == 2   ? std::vector<std::string>{"Texto"}
                           : mode == 3 ? std::vector<std::string>{"Placa"}
                                       : std::vector<std::string>{"Placa", "Texto"};
        const auto param = [&](const std::string& name, double value) {
            recipe.parameters.push_back({name, number(value)});
        };
        if (mode != 2) {
            param(plateW, width);
            param(plateH, height);
            param(thick, v("grosor"));
            param(corner, v("esquina"));
        }
        param(relief, v("relieve"));
        if (keychain && mode != 2) {
            param(ring, v("anilla"));
            param(hole, v("agujero"));
        }

        // El sketch de las letras.
        auto sketchFeature = std::make_unique<SketchFeature>(lettering, SketchAttachment{});
        sketchFeature->setName(std::string(keychain ? "Llavero" : "Letrero") + ": letras");
        recipe.features.push_back(std::move(sketchFeature));

        if (mode != 2) {
            auto plate = std::make_unique<BoxFeature>();
            plate->setName(std::string(keychain ? "Llavero" : "Letrero") + ": placa");
            plate->base.origin = OriginPlane::XY;
            plate->centered = true;
            plate->expressions = {{"width", plateW}, {"depth", plateH}, {"height", thick}, {"radius", corner}};
            recipe.features.push_back(std::move(plate));
            if (keychain) {
                // La anilla: un disco que sale por la izquierda (metido 2 mm en la placa) con su agujero.
                const std::string cx = "-(" + plateW + "/2 + " + ring + "/2 - 2)";
                auto boss = std::make_unique<CylinderFeature>();
                boss->setName("Llavero: anilla");
                boss->base.origin = OriginPlane::XY;
                boss->operation = Operation::Join;
                boss->expressions = {{"diameter", ring}, {"x", cx}, {"height", thick}};
                recipe.features.push_back(std::move(boss));
                auto cut = std::make_unique<CylinderFeature>();
                cut->setName("Llavero: agujero");
                cut->base.origin = OriginPlane::XY;
                cut->operation = Operation::Cut;
                cut->expressions = {{"diameter", hole}, {"x", cx}, {"z", "-1"}, {"height", thick + " + 2"}};
                recipe.features.push_back(std::move(cut));
            }
        }

        // Las letras: en relieve sobre la placa, grabadas en ella o sueltas.
        auto letters = std::make_unique<ExtrudeFeature>();
        letters->setName(std::string(keychain ? "Llavero" : "Letrero") + ": texto");
        for (const sketch::Profile& profile : profiles) {
            letters->profiles.push_back(profileRef(kRecipeSketch, profile));
        }
        if (mode == 3) {
            // Un solo cuerpo: el texto se une a la placa (empieza 0,01 mm dentro de ella para que la unión sea firme).
            letters->operation = Operation::Join;
            letters->spec.startOffset = v("grosor") - 0.01;
            letters->spec.side1.distance = v("relieve") + 0.01;
            letters->expressions = {{"startOffset", thick + " - 0.01"}, {"distance", relief + " + 0.01"}};
        } else if (mode == 0) {
            letters->operation = Operation::NewBody;
            letters->spec.startOffset = v("grosor");
            letters->spec.side1.distance = v("relieve");
            letters->expressions = {{"startOffset", thick}, {"distance", relief}};
        } else if (mode == 1) {
            letters->operation = Operation::Cut;
            letters->spec.startOffset = v("grosor") - v("relieve");
            letters->spec.side1.distance = v("relieve") + 1.0;
            letters->expressions = {{"startOffset", thick + " - " + relief}, {"distance", relief + " + 1"}};
        } else {
            letters->operation = Operation::NewBody;
            letters->spec.side1.distance = v("relieve");
            letters->expressions = {{"distance", relief}};
        }
        recipe.features.push_back(std::move(letters));
        return recipe;
    }
};

// --- Ajustes de impresión comunes a los generadores de engranajes -------------------------------
// «Adelgazar el diente» (juego como grueso del diente, sin mover los ejes) y «chaflán de la base» (pata de elefante).
// Los diseños hechos antes de que existieran no traen estas claves: cuentan como 0.

double tuningValue(const GeneratorValues& values, const char* key)
{
    return values.count(key) != 0 ? values.at(key) : 0.0;
}

std::vector<GeneratorField> tuningFields()
{
    GeneratorField thin;
    thin.kind = GeneratorField::Kind::Length;
    thin.key = "adelgazar";
    thin.label = "Adelgazar el diente";
    thin.value = 0.0;
    thin.min = 0.0;
    thin.max = 1.0;
    thin.hint = "Quita este grueso a cada diente (mm sobre la circunferencia primitiva) sin mover los ejes: otra forma de dar juego";
    GeneratorField chamfer;
    chamfer.kind = GeneratorField::Kind::Length;
    chamfer.key = "chaflan_base";
    chamfer.label = "Chaflán de la base";
    chamfer.value = 0.0;
    chamfer.min = 0.0;
    chamfer.max = 1.0;
    chamfer.hint = "Chaflán en el borde de la cara de abajo contra la cama (pata de elefante); solo con dientes rectos; con algunas medidas y dientes falla: prueba otra cercana";
    return {thin, chamfer};
}

void addTuningParameters(Recipe& recipe, const GeneratorValues& values, const std::string& suffix)
{
    recipe.parameters.push_back({"adelgazar" + suffix, number(tuningValue(values, "adelgazar"))});
    recipe.parameters.push_back({"chaflan_base" + suffix, number(tuningValue(values, "chaflan_base"))});
}

void applyTuning(GearFeature& gear, const std::string& suffix)
{
    gear.expressions["backlash"] = "adelgazar" + suffix;
    gear.expressions["baseChamfer"] = "chaflan_base" + suffix;
}

// --- Par de engranajes ---------------------------------------------------------------------
// Dos ruedas que engranan: mismo módulo y ángulo de presión; hélice de sentido contrario; a la distancia entre
// ejes módulo·(z1 + z2)/2 (más el juego) y con la segunda girada para que un diente caiga en el hueco de la primera.

class GearPair final : public Generator {
public:
    std::string category() const override { return "Engranajes"; }
    std::string id() const override { return "par-engranajes"; }
    std::string title() const override { return "Par de engranajes"; }
    std::string icon() const override { return "gear-solid"; }
    std::string description() const override
    {
        return "Dos engranajes que engranan: mismo módulo y ángulo de presión, a la distancia entre ejes correcta y "
               "con la segunda rueda girada para que los dientes encajen.";
    }

    std::vector<GeneratorField> fields() const override
    {
        using K = GeneratorField::Kind;
        const auto make = [](K kind, std::string key, std::string label, double value, double min, double max,
                             std::string hint = {}) {
            GeneratorField f;
            f.kind = kind;
            f.key = std::move(key);
            f.label = std::move(label);
            f.value = value;
            f.min = min;
            f.max = max;
            f.hint = std::move(hint);
            return f;
        };
        return {make(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "El tamaño del diente: igual en las dos ruedas"),
                make(K::Count, "dientes_1", "Dientes de la rueda 1", 12.0, 6.0, 300.0),
                make(K::Count, "dientes_2", "Dientes de la rueda 2", 24.0, 6.0, 300.0),
                make(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 35.0, "Igual en las dos ruedas (20° es lo normal)"),
                make(K::Length, "ancho_rueda", "Ancho de las ruedas", 8.0, 1.0, 200.0, "A lo largo del eje"),
                make(K::Angle, "helice", "Ángulo de hélice", 0.0, 0.0, 45.0,
                     "0 = dientes rectos; si no, la rueda 2 lleva el sentido contrario"),
                make(K::Length, "agujero_1", "Agujero de la rueda 1", 5.0, 0.0, 100.0, "0 = macizo"),
                make(K::Length, "agujero_2", "Agujero de la rueda 2", 5.0, 0.0, 100.0, "0 = macizo"),
                make(K::Length, "juego", "Juego entre dientes", 0.15, 0.0, 2.0,
                     "Aire para que giren sin atascarse al imprimir (se suma a la distancia entre ejes)"),
                tuningFields()[0], tuningFields()[1]};
    }

    std::string summary(const GeneratorValues& values) const override
    {
        const double m = values.at("modulo"), z1 = values.at("dientes_1"), z2 = values.at("dientes_2");
        char text[256];
        std::snprintf(text, sizeof text,
                      "Relación %g:%g (la rueda 2 gira %.3f veces por vuelta de la 1) · distancia entre ejes %s · "
                      "Ø primitivos %s y %s",
                      z1, z2, z1 / z2, geometry::display::length(m * (z1 + z2) / 2.0 + values.at("juego")).c_str(),
                      geometry::display::length(m * z1).c_str(), geometry::display::length(m * z2).c_str());
        return text;
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        // Los diámetros van centrados en la rueda (los dientes son simétricos, así que el centro de la caja es el eje).
        const auto centeredOn = [](int body, std::function<double(const GeneratorValues&)> length = {}) {
            ArrowSpec spec{body, 'x', false, true};
            spec.at = [](const GeneratorValues&) { return std::array<double, 3>{kCenter, kCenter, kCenter}; };
            spec.length = std::move(length);
            return spec;
        };
        const auto pitch = [](const char* teeth) {
            return [teeth](const GeneratorValues& v) { return v.at("modulo") * v.at(teeth); };
        };
        return {{"ancho_rueda", {0, 'z', false}}, {"agujero_1", centeredOn(0)}, {"agujero_2", centeredOn(1)},
                {"modulo", centeredOn(0, pitch("dientes_1"))}, {"dientes_1", centeredOn(0, pitch("dientes_1"))},
                {"dientes_2", centeredOn(1, pitch("dientes_2"))}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), z1 = p("dientes_1"), z2 = p("dientes_2"), presion = p("presion"),
                          ancho = p("ancho_rueda"), helice = p("helice"), bore1 = p("agujero_1"), bore2 = p("agujero_2"),
                          juego = p("juego"), giro = p("giro_rueda_2");

        Recipe recipe;
        recipe.component = "Par de engranajes";
        recipe.bodyNames = {"Rueda 1", "Rueda 2"};
        const auto param = [&](const std::string& name, const std::string& expression) {
            recipe.parameters.push_back({name, expression});
        };
        param(modulo, number(values.at("modulo")));
        param(z1, number(values.at("dientes_1")));
        param(z2, number(values.at("dientes_2")));
        param(presion, number(values.at("presion")));
        param(ancho, number(values.at("ancho_rueda")));
        param(helice, number(values.at("helice")));
        param(bore1, number(values.at("agujero_1")));
        param(bore2, number(values.at("agujero_2")));
        param(juego, number(values.at("juego")));
        addTuningParameters(recipe, values, suffix);
        // El giro de la rueda 2 (en grados): con un diente de la rueda 1 apuntando a la 2, la 2 debe tener un hueco
        // apuntando a la 1, es decir, 180° − medio paso angular, reducido a un paso (360°/dientes).
        const std::string halfStep = "180/" + z2;
        const std::string step = "360/" + z2;
        param(giro, "(180 - " + halfStep + ") - floor((180 - " + halfStep + ")/(" + step + "))*(" + step + ")");

        const auto wheel = [&](const std::string& name, const std::string& teeth, const std::string& bore,
                               const std::string& sign) {
            auto f = std::make_unique<GearFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->expressions = {{"module", modulo}, {"teeth", teeth}, {"pressureAngle", presion}, {"thickness", ancho},
                              {"bore", bore}, {"helixAngle", sign + helice}};
            applyTuning(*f, suffix);
            return f;
        };
        recipe.features.push_back(wheel("Engranaje: rueda 1", z1, bore1, ""));
        auto second = wheel("Engranaje: rueda 2", z2, bore2, "-");
        second->expressions["x"] = modulo + "*(" + z1 + " + " + z2 + ")/2 + " + juego;
        second->expressions["rotation"] = giro;
        recipe.features.push_back(std::move(second));
        return recipe;
    }
};

// --- Engranaje interno (corona + piñón) ---------------------------------------------------------
// Una corona con los dientes hacia dentro y un piñón que engrana por dentro: mismo módulo y ángulo de presión, a la
// distancia entre ejes módulo·(z corona − z piñón)/2 (menos el juego: en un engranaje interno acercar los ejes
// separa los flancos). El primer hueco de la corona y el primer diente del piñón apuntan los dos por +X, así que
// encajan sin girar nada.

GeneratorField gearField(GeneratorField::Kind kind, std::string key, std::string label, double value, double min,
                         double max, std::string hint = {})
{
    GeneratorField f;
    f.kind = kind;
    f.key = std::move(key);
    f.label = std::move(label);
    f.value = value;
    f.min = min;
    f.max = max;
    f.hint = std::move(hint);
    return f;
}

// Una flecha de diámetro centrada en el cuerpo «body» (los dientes son simétricos: el centro de la caja es el eje).
Generator::ArrowSpec centeredArrow(int body, std::function<double(const GeneratorValues&)> length = {})
{
    Generator::ArrowSpec spec{body, 'x', false, true};
    spec.at = [](const GeneratorValues&) {
        return std::array<double, 3>{Generator::kCenter, Generator::kCenter, Generator::kCenter};
    };
    spec.length = std::move(length);
    return spec;
}

class InternalGear final : public Generator {
public:
    std::string category() const override { return "Engranajes"; }
    std::string id() const override { return "engranaje-interno"; }
    std::string title() const override { return "Engranaje interno"; }
    std::string icon() const override { return "gear-solid"; }
    std::string description() const override
    {
        return "Una corona con los dientes hacia dentro y un piñón que engrana en su interior, a la distancia entre "
               "ejes correcta y con el mismo módulo y ángulo de presión.";
    }

    std::vector<GeneratorField> fields() const override
    {
        using K = GeneratorField::Kind;
        return {gearField(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "El tamaño del diente: igual en la corona y el piñón"),
                gearField(K::Count, "dientes_corona", "Dientes de la corona", 36.0, 20.0, 300.0,
                          "Mejor al menos 8 más que el piñón (si no, los dientes se estorban)"),
                gearField(K::Count, "dientes_pinon", "Dientes del piñón", 12.0, 6.0, 300.0),
                gearField(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 35.0, "Igual en los dos (20° es lo normal)"),
                gearField(K::Length, "ancho_rueda", "Ancho", 8.0, 1.0, 200.0, "A lo largo del eje"),
                gearField(K::Length, "aro", "Grosor del aro de la corona", 6.0, 1.0, 100.0,
                          "Material por fuera de los huecos de los dientes"),
                gearField(K::Length, "agujero_pinon", "Agujero del piñón", 5.0, 0.0, 100.0, "0 = macizo"),
                gearField(K::Length, "juego", "Juego entre dientes", 0.15, 0.0, 2.0,
                          "Aire para que giren sin atascarse al imprimir (acerca los ejes)"),
                tuningFields()[0], tuningFields()[1]};
    }

    std::string summary(const GeneratorValues& values) const override
    {
        const double m = values.at("modulo"), zc = values.at("dientes_corona"), zp = values.at("dientes_pinon");
        char text[320];
        std::snprintf(text, sizeof text,
                      "Relación %g:%g (el piñón da %.3f vueltas por una de la corona, en el mismo sentido) · distancia "
                      "entre ejes %s · Ø primitivos %s y %s%s",
                      zp, zc, zc / zp, geometry::display::length(m * (zc - zp) / 2.0 - values.at("juego")).c_str(),
                      geometry::display::length(m * zc).c_str(), geometry::display::length(m * zp).c_str(),
                      zc - zp < 8.0 ? " · AVISO: con menos de 8 dientes de diferencia los dientes se estorban" : "");
        return text;
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        const auto pitch = [](const char* teeth) {
            return [teeth](const GeneratorValues& v) { return v.at("modulo") * v.at(teeth); };
        };
        return {{"ancho_rueda", {0, 'z', false}}, {"agujero_pinon", centeredArrow(1)},
                {"modulo", centeredArrow(0, pitch("dientes_corona"))},
                {"dientes_corona", centeredArrow(0, pitch("dientes_corona"))},
                {"dientes_pinon", centeredArrow(1, pitch("dientes_pinon"))}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), zc = p("dientes_corona"), zp = p("dientes_pinon"), presion = p("presion"),
                          ancho = p("ancho_rueda"), aro = p("aro"), bore = p("agujero_pinon"), juego = p("juego");

        Recipe recipe;
        recipe.component = "Engranaje interno";
        recipe.bodyNames = {"Corona", "Piñón"};
        const auto param = [&](const std::string& name, const char* key) {
            recipe.parameters.push_back({name, number(values.at(key))});
        };
        param(modulo, "modulo");
        param(zc, "dientes_corona");
        param(zp, "dientes_pinon");
        param(presion, "presion");
        param(ancho, "ancho_rueda");
        param(aro, "aro");
        param(bore, "agujero_pinon");
        param(juego, "juego");
        addTuningParameters(recipe, values, suffix);

        auto ring = std::make_unique<GearFeature>();
        ring->setName("Engranaje: corona");
        ring->base.origin = OriginPlane::XY;
        ring->spec.type = geometry::GearType::Internal;
        ring->expressions = {{"module", modulo}, {"teeth", zc}, {"pressureAngle", presion}, {"thickness", ancho},
                             {"rim", aro}};
        applyTuning(*ring, suffix);
        recipe.features.push_back(std::move(ring));

        auto pinion = std::make_unique<GearFeature>();
        pinion->setName("Engranaje: piñón");
        pinion->base.origin = OriginPlane::XY;
        pinion->expressions = {{"module", modulo}, {"teeth", zp}, {"pressureAngle", presion}, {"thickness", ancho},
                               {"bore", bore},
                               {"x", modulo + "*(" + zc + " - " + zp + ")/2 - " + juego}};
        applyTuning(*pinion, suffix);
        recipe.features.push_back(std::move(pinion));
        return recipe;
    }
};

// --- Cremallera con rueda ----------------------------------------------------------------------
// Una barra dentada (dientes trapezoidales: la involuta de radio infinito) y la rueda que engrana con ella. La
// rueda está en el origen y la cremallera debajo, con la línea primitiva tangente a la circunferencia primitiva
// (más el juego); la rueda lleva un diente apuntando hacia la cremallera, justo sobre un hueco de ésta.

class RackAndPinion final : public Generator {
public:
    std::string category() const override { return "Engranajes"; }
    std::string id() const override { return "cremallera"; }
    std::string title() const override { return "Cremallera"; }
    std::string icon() const override { return "gear-solid"; }
    std::string description() const override
    {
        return "Una barra dentada (cremallera) y la rueda que engrana con ella: mismo módulo y ángulo de presión, a la "
               "distancia correcta y con un diente de la rueda sobre un hueco de la barra. El largo de la barra se da "
               "por número de dientes o en milímetros.";
    }

    std::vector<GeneratorField> fields() const override
    {
        using K = GeneratorField::Kind;
        GeneratorField mode;
        mode.kind = K::Choice;
        mode.key = "largo_modo";
        mode.label = "Largo de la barra";
        mode.choices = {"Por número de dientes", "Largo exacto (el último diente queda cortado)",
                        "Largo con dientes completos"};
        mode.hint = "Elige cómo darlo: contando dientes, o en milímetros (exacto, o ajustado a dientes enteros)";
        GeneratorField exact = gearField(K::Length, "largo_exacto", "Largo de la barra", 60.0, 8.0, 2000.0,
                                         "La barra mide justo esto; el último diente se corta si no cabe entero");
        exact.showKey = "largo_modo";
        exact.showIndex = 1;
        GeneratorField rounded = gearField(K::Length, "largo_aprox", "Largo aproximado", 60.0, 8.0, 2000.0,
                                           "Se ajusta al número entero de dientes más cercano (largo = dientes · π · módulo)");
        rounded.showKey = "largo_modo";
        rounded.showIndex = 2;
        GeneratorField teeth = gearField(K::Count, "dientes_cremallera", "Dientes de la cremallera", 10.0, 2.0, 300.0,
                                         "Largo de la barra = dientes · π · módulo");
        teeth.showKey = "largo_modo";
        teeth.showIndex = 0;
        return {gearField(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "El tamaño del diente: igual en la rueda y la barra"),
                gearField(K::Count, "dientes_rueda", "Dientes de la rueda", 20.0, 6.0, 300.0),
                mode,
                teeth,
                exact,
                rounded,
                gearField(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 30.0, "Igual en los dos (20° es lo normal)"),
                gearField(K::Length, "ancho_rueda", "Ancho", 8.0, 1.0, 200.0, "A lo largo del eje"),
                gearField(K::Length, "base", "Grosor de la base de la barra", 5.0, 1.0, 100.0, "Bajo la raíz de los dientes"),
                gearField(K::Length, "agujero_rueda", "Agujero de la rueda", 5.0, 0.0, 100.0, "0 = macizo"),
                gearField(K::Length, "juego", "Juego entre dientes", 0.15, 0.0, 2.0,
                          "Aire para que engranen sin atascarse al imprimir (aleja la barra de la rueda)"),
                tuningFields()[0], tuningFields()[1]};
    }

    // Lo que sale del modo de largo: cuántos dientes y cuánto mide la barra (los diseños hechos antes de que existiera
    // el modo no lo traen: cuentan como «por número de dientes»).
    struct RackSize {
        int mode = 0;
        int teeth = 10;
        double length = 0.0;
    };
    static RackSize rackSize(const GeneratorValues& values)
    {
        constexpr double kPi = 3.14159265358979323846;
        const auto get = [&](const char* key, double fallback) { return values.count(key) != 0 ? values.at(key) : fallback; };
        RackSize size;
        size.mode = int(get("largo_modo", 0.0));
        const double pitch = kPi * get("modulo", 2.0);
        if (size.mode == 1) {
            size.length = get("largo_exacto", 60.0);
            size.teeth = std::max(2, int(std::ceil(size.length / pitch - 1e-9)));
        } else if (size.mode == 2) {
            size.teeth = std::max(2, int(std::lround(get("largo_aprox", 60.0) / pitch)));
            size.length = size.teeth * pitch;
        } else {
            size.teeth = int(get("dientes_cremallera", 10.0));
            size.length = size.teeth * pitch;
        }
        return size;
    }

    std::string summary(const GeneratorValues& values) const override
    {
        const double m = values.at("modulo"), z = values.at("dientes_rueda");
        const RackSize rack = rackSize(values);
        constexpr double kPi = 3.14159265358979323846;
        char text[320];
        std::snprintf(text, sizeof text,
                      "La barra avanza %s por vuelta de la rueda · largo de la barra %s, %d dientes%s (%.2f "
                      "vueltas de la rueda) · Ø primitivo de la rueda %s",
                      geometry::display::length(kPi * m * z).c_str(), geometry::display::length(rack.length, 1).c_str(), rack.teeth,
                      rack.mode == 1 ? " (el último cortado)" : "", rack.length / (kPi * m * z),
                      geometry::display::length(m * z).c_str());
        return text;
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        const auto pitch = [](const GeneratorValues& v) { return v.at("modulo") * v.at("dientes_rueda"); };
        return {{"ancho_rueda", {0, 'z', false}}, {"agujero_rueda", centeredArrow(0)},
                {"modulo", centeredArrow(0, pitch)}, {"dientes_rueda", centeredArrow(0, pitch)}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), z = p("dientes_rueda"), n = p("dientes_cremallera"), presion = p("presion"),
                          ancho = p("ancho_rueda"), base = p("base"), bore = p("agujero_rueda"), juego = p("juego"),
                          giro = p("giro_rueda"), largo = p("largo");
        const RackSize rack = rackSize(values);

        Recipe recipe;
        recipe.component = "Cremallera";
        recipe.bodyNames = {"Rueda", "Cremallera"};
        const auto param = [&](const std::string& name, const std::string& expression) {
            recipe.parameters.push_back({name, expression});
        };
        param(modulo, number(values.at("modulo")));
        param(z, number(values.at("dientes_rueda")));
        // El largo de la barra: contando dientes, o en milímetros (y entonces los dientes salen de ahí, ligados al
        // parámetro: al cambiar el largo o el módulo, se rehacen).
        if (rack.mode == 1) {
            param(largo, number(values.at("largo_exacto")));
            param(n, "ceil(" + largo + "/(pi*" + modulo + "))");
        } else if (rack.mode == 2) {
            param(largo, number(values.at("largo_aprox")));
            param(n, "max(2; round(" + largo + "/(pi*" + modulo + ")))");
        } else {
            param(n, number(values.at("dientes_cremallera")));
        }
        param(presion, number(values.at("presion")));
        param(ancho, number(values.at("ancho_rueda")));
        param(base, number(values.at("base")));
        param(bore, number(values.at("agujero_rueda")));
        param(juego, number(values.at("juego")));
        addTuningParameters(recipe, values, suffix);
        // Un diente de la rueda apunta hacia abajo (270°): el giro inicial se reduce a un paso angular (360°/dientes).
        const std::string step = "360/" + z;
        param(giro, "270 - floor(270/(" + step + "))*(" + step + ")");

        auto wheel = std::make_unique<GearFeature>();
        wheel->setName("Engranaje: rueda");
        wheel->base.origin = OriginPlane::XY;
        wheel->expressions = {{"module", modulo}, {"teeth", z}, {"pressureAngle", presion}, {"thickness", ancho},
                              {"bore", bore}, {"rotation", giro}};
        applyTuning(*wheel, suffix);
        recipe.features.push_back(std::move(wheel));

        auto bar = std::make_unique<GearFeature>();
        bar->setName("Engranaje: cremallera");
        bar->base.origin = OriginPlane::XY;
        bar->spec.type = geometry::GearType::Rack;
        // El origen de la barra es un hueco; se corre un número entero de pasos para que quede uno bajo la rueda.
        bar->expressions = {{"module", modulo}, {"teeth", n}, {"pressureAngle", presion}, {"thickness", ancho},
                            {"rim", base},
                            {"x", "-floor(" + n + "/2)*" + modulo + "*pi"},
                            {"y", "-(" + modulo + "*" + z + "/2) - " + juego}};
        if (rack.mode == 1) {
            bar->expressions["length"] = largo;  // la barra se corta a esa medida
        }
        applyTuning(*bar, suffix);
        recipe.features.push_back(std::move(bar));
        return recipe;
    }
};

} // namespace

GeneratorValues Generator::defaults() const
{
    GeneratorValues values;
    for (const GeneratorField& f : fields()) {
        if (f.kind == GeneratorField::Kind::Text || f.kind == GeneratorField::Kind::Font) {
            values.texts[f.key] = f.text;
        } else {
            values[f.key] = f.value;
        }
    }
    return values;
}

namespace {
TextOutliner gOutliner;
}

void setTextOutliner(TextOutliner outliner)
{
    gOutliner = std::move(outliner);
}

std::vector<sketch::Geometry> textOutline(const std::string& text, double height, const std::string& family)
{
    return gOutliner && !text.empty() ? gOutliner(text, height, family) : std::vector<sketch::Geometry>{};
}

std::unique_ptr<Feature> instantiate(const Feature& feature, FeatureId recipeSketch)
{
    std::unique_ptr<Feature> copy = feature.clone();
    if (auto* extrude = dynamic_cast<ExtrudeFeature*>(copy.get())) {
        for (ProfileRef& profile : extrude->profiles) {
            if (profile.sketch == kRecipeSketch) {
                profile.sketch = recipeSketch;
            }
        }
    }
    return copy;
}

const std::vector<const Generator*>& generators()
{
    static const Lightbox lightbox;
    static const BoxWithLid boxWithLid;
    static const Sign sign;
    static const GearPair gearPair;
    static const InternalGear internalGear;
    static const RackAndPinion rackAndPinion;
    static const std::vector<const Generator*> all = {&lightbox, &boxWithLid, &sign, &gearPair, &internalGear,
                                                      &rackAndPinion};
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

std::string encodeValues(const GeneratorValues& values)
{
    // Una línea por respuesta: «n<TAB>clave<TAB>número» o «t<TAB>clave<TAB>texto» (con barra, tabulador y salto
    // de línea escapados).
    const auto escape = [](const std::string& text) {
        std::string out;
        for (const char c : text) {
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '\t') {
                out += "\\t";
            } else if (c == '\n') {
                out += "\\n";
            } else {
                out += c;
            }
        }
        return out;
    };
    std::string out;
    for (const auto& [key, value] : values) {
        char buffer[40];
        std::snprintf(buffer, sizeof buffer, "%.17g", value);
        out += "n\t" + escape(key) + "\t" + buffer + "\n";
    }
    for (const auto& [key, text] : values.texts) {
        out += "t\t" + escape(key) + "\t" + escape(text) + "\n";
    }
    return out;
}

GeneratorValues decodeValues(const std::string& text)
{
    const auto unescape = [](const std::string& in) {
        std::string out;
        for (std::size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '\\' && i + 1 < in.size()) {
                ++i;
                out += in[i] == 't' ? '\t' : in[i] == 'n' ? '\n' : in[i];
            } else {
                out += in[i];
            }
        }
        return out;
    };
    GeneratorValues values;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        const std::size_t a = line.find('\t');
        const std::size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) {
            continue;
        }
        const std::string key = unescape(line.substr(a + 1, b - a - 1));
        if (line[0] == 'n') {
            values[key] = std::strtod(line.c_str() + b + 1, nullptr);
        } else if (line[0] == 't') {
            values.texts[key] = unescape(line.substr(b + 1));
        }
    }
    return values;
}

std::vector<FeatureId> generatedGroup(const Document& doc, FeatureId lead)
{
    std::vector<FeatureId> ids;
    if (lead == 0) {
        return ids;
    }
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        if (doc.feature(i).origin.lead == lead) {
            ids.push_back(doc.feature(i).id());
        }
    }
    return ids;
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
    FeatureId recipeSketch = 0;
    for (const auto& feature : recipe.features) {
        const FeatureId id = doc.insertFeature(instantiate(*feature, recipeSketch), doc.marker());
        if (feature->kind() == FeatureKind::Sketch) {
            recipeSketch = id;
        }
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
