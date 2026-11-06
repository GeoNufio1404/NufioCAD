#include <nuf/document/Generators.hpp>

#include <nuf/document/Features.hpp>
#include <nuf/sketch/Dxf.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
// hecha de un marco, un labio que entra en la caja y la cara central (donde va la imagen). Todo son cuerpos
// aparte para poder darle a cada uno su filamento sin pintar caras.

class Lightbox final : public Generator {
public:
    std::string id() const override { return "lightbox"; }
    std::string title() const override { return "Lightbox"; }
    std::string description() const override
    {
        return "Caja de luz para imprimir, como la de MakerLab: caja abierta por delante y tapa delantera con marco, "
               "labio y una cara central aparte (para la imagen).";
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
        recipe.bodyNames = {"Caja", "Marco", "Labio", "Cara"};
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
        // 4. El marco de la tapa: una placa del tamaño de la caja con el centro hueco (el anillo de la pared).
        recipe.features.push_back(shapeAt("Lightbox: marco", Operation::NewBody, W, H, "0", frente, beside, esquina));
        recipe.features.push_back(
            shapeAt("Lightbox: hueco del marco", Operation::Cut, inW, inH, "-1", frente + " + 2", beside, ""));
        // 5. El labio: un anillo que sale hacia atrás desde la cara interior de la tapa y entra en la caja.
        recipe.features.push_back(shapeAt("Lightbox: labio", Operation::NewBody, lipW, lipH, frente, labio, beside, ""));
        recipe.features.push_back(
            shapeAt("Lightbox: hueco del labio", Operation::Cut, lipInW, lipInH, frente, labio + " + 1", beside, ""));
        // 6. La cara central, donde va la imagen: rellena el hueco del marco (un cuerpo aparte).
        recipe.features.push_back(shapeAt("Lightbox: cara central", Operation::NewBody, inW, inH, "0", frente, beside, ""));
        return recipe;
    }
};

// --- Caja con tapa -------------------------------------------------------------------------
// Una caja rectangular abierta por arriba y su tapa, que cierra con un labio a presión o con tornillos en las
// esquinas. Sirve para electrónica, cajas de regalo, organizadores…

class BoxWithLid final : public Generator {
public:
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
    std::string id() const override { return "letrero"; }
    std::string title() const override { return "Letrero o llavero"; }
    std::string icon() const override { return "box-solid"; }
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
        return {choice("tipo", "Tipo", {"Letrero", "Llavero"}),
                text,
                font,
                length("altura_texto", "Tamaño del texto", 20.0, 3.0, 300.0, "Tamaño de la fuente; las letras mayúsculas miden ~70 %"),
                choice("modo", "Texto", {"En relieve (otro cuerpo)", "Grabado (hundido)", "Solo letras"}),
                length("margen", "Margen de la placa", 5.0, 1.0, 100.0, "Entre el texto y el borde"),
                length("grosor", "Grosor de la placa", 3.0, 0.8, 50.0),
                length("relieve", "Relieve o profundidad", 1.2, 0.2, 50.0, "Cuánto sobresale (o se hunde) el texto"),
                length("esquina", "Radio de esquinas", 3.0, 0.0, 50.0),
                length("anilla", "Diámetro de la anilla", 12.0, 5.0, 60.0, "El círculo que rodea el agujero", "tipo", 1),
                length("agujero", "Agujero de la anilla", 5.0, 2.0, 40.0, {}, "tipo", 1)};
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
        recipe.bodyNames = mode == 2 ? std::vector<std::string>{"Texto"} : std::vector<std::string>{"Placa", "Texto"};
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
        if (mode == 0) {
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
    static const std::vector<const Generator*> all = {&lightbox, &boxWithLid, &sign};
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
