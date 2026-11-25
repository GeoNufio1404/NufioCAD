#include <nuf/document/Generators.hpp>

#include <nuf/document/Features.hpp>
#include <nuf/geometry/DisplayUnits.hpp>
#include <nuf/geometry/Modeling.hpp>
#include <nuf/sketch/Dxf.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
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

// Pone sección plegable a las preguntas: desde la que tiene la clave de cada par hasta la siguiente (las anteriores quedan sin sección).
std::vector<GeneratorField> grouped(std::vector<GeneratorField> fields, std::initializer_list<std::pair<const char*, const char*>> starts)
{
    std::string current;
    for (GeneratorField& f : fields) {
        for (const auto& [key, name] : starts) {
            if (f.key == key) {
                current = name;
            }
        }
        f.group = current;
    }
    return fields;
}

// --- Lightbox ------------------------------------------------------------------------------
// Como la «Light Box» de MakerLab: una caja abierta por delante (fondo cerrado y paredes), y una tapa delantera
// hecha de un marco, un labio que entra en la caja y la cara central (donde va la imagen). La caja y la tapa son
// dos cuerpos; la tapa es un solo cuerpo (placa más labio), sin piezas sueltas que manejar.

// Una respuesta, o «missing» si el diseño es anterior a esa pregunta.
double answer(const GeneratorValues& v, const char* key, double missing = 0.0)
{
    const auto it = v.find(key);
    return it != v.end() ? it->second : missing;
}

// Una flecha que empieza en un punto del diseño (ver Generator::kWorld): mide el valor de la pregunta, o «length».
Generator::ArrowSpec worldArrow(char axis, std::function<std::array<double, 3>(const GeneratorValues&)> at,
                                std::function<double(const GeneratorValues&)> length = {}, bool centered = false)
{
    Generator::ArrowSpec spec{Generator::kWorld, axis, false, centered};
    spec.at = std::move(at);
    spec.length = std::move(length);
    return spec;
}

// La misma medida hacia el lado negativo del eje.
std::function<double(const GeneratorValues&)> backwards(const char* key)
{
    return [key](const GeneratorValues& v) { return -answer(v, key); };
}

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
        GeneratorField channel;
        channel.key = "canal";
        channel.label = "Canal para tira LED";
        channel.kind = K::Choice;
        channel.choices = {"Sin canal", "Canal por dentro, junto a las paredes"};
        channel.hint = "Un muro bajo que deja entre él y la pared un canal donde va pegada la tira LED";
        GeneratorField cableWall;
        cableWall.key = "pared_cable";
        cableWall.label = "Salida del cable";
        cableWall.kind = K::Choice;
        cableWall.choices = {"Atrás (en el fondo)", "Pared de abajo", "Pared de arriba", "Pared izquierda", "Pared derecha"};
        cableWall.hint = "Por dónde sale el cable de la tira LED";
        return grouped({shape,
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
                cableWall,
                channel,
                length("tira_ancho", "Ancho de la tira LED", 10.0, 3.0, 50.0, "El ancho del canal entre la pared y el muro (una tira de 8 mm: 10)", "canal", 1),
                length("canal_alto", "Alto del muro del canal", 8.0, 2.0, 60.0, "Desde el fondo de la caja", "canal", 1),
                length("canal_pared", "Grosor del muro del canal", 1.2, 0.4, 10.0, {}, "canal", 1)}, {{"forma", "Medidas de la caja"}, {"labio", "Tapa y labio"}, {"cable", "Cable y tira LED"}});
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
        // El cable: por el fondo, o por una pared (las de arriba y abajo se miden a lo ancho; las de los lados, a lo alto).
        const auto width = [](const GeneratorValues& v) { return v.at("forma") >= 0.5 ? v.at("diametro") : v.at("ancho"); };
        const auto height = [](const GeneratorValues& v) { return v.at("forma") >= 0.5 ? v.at("diametro") : v.at("alto"); };
        ArrowSpec cable = worldArrow('x', [width, height](const GeneratorValues& v) {
            const double z = v.at("fondo") + (v.at("profundidad") - v.at("fondo")) / 2.0;
            switch (int(answer(v, "pared_cable"))) {
            case 1: return std::array<double, 3>{0.0, -height(v) / 2.0, z};
            case 2: return std::array<double, 3>{0.0, height(v) / 2.0, z};
            case 3: return std::array<double, 3>{-width(v) / 2.0, 0.0, z};
            case 4: return std::array<double, 3>{width(v) / 2.0, 0.0, z};
            default: return std::array<double, 3>{0.0, -height(v) / 4.0, 0.0};
            }
        }, {}, true);
        cable.axisOf = [](const GeneratorValues& v) { return answer(v, "pared_cable") >= 2.5 ? 'y' : 'x'; };
        // El canal de la tira LED, en la pared izquierda: el hueco, y el muro con su alto y su grosor.
        const ArrowSpec strip = worldArrow('x', [width](const GeneratorValues& v) {
            return std::array<double, 3>{-width(v) / 2.0 + v.at("pared"), 0.0, v.at("fondo")};
        });
        const ArrowSpec wallHeight = worldArrow('z', [width](const GeneratorValues& v) {
            return std::array<double, 3>{-width(v) / 2.0 + v.at("pared") + answer(v, "tira_ancho"), 0.0, v.at("fondo")};
        });
        const ArrowSpec wallThick = worldArrow('x', [width](const GeneratorValues& v) {
            return std::array<double, 3>{-width(v) / 2.0 + v.at("pared") + answer(v, "tira_ancho"), 0.0,
                                         v.at("fondo") + answer(v, "canal_alto")};
        });
        // La holgura: el aire entre la cara interior de la pared de la caja y el labio de la tapa.
        ArrowSpec gap{1, 'x', false};
        gap.at = [](const GeneratorValues& v) {
            return std::array<double, 3>{v.at("pared"), kCenter, v.at("frente") + v.at("labio") / 2.0};
        };
        return {{"ancho", {0, 'x', false}}, {"diametro", {0, 'x', false}}, {"alto", {0, 'y', false}},
                {"profundidad", {0, 'z', false}}, {"pared", {0, 'x', true}}, {"fondo", {0, 'z', false}},
                {"frente", {1, 'z', false}}, {"labio", {1, 'z', true}}, {"labio_pared", lipWall},
                {"esquina", corner}, {"holgura", gap}, {"cable", cable}, {"tira_ancho", strip}, {"canal_alto", wallHeight},
                {"canal_pared", wallThick}};
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
        // 3b. El canal para la tira LED: un muro (un anillo) a «tira_ancho» de la pared, desde el fondo; la tira se pega en el
        // fondo entre la pared y el muro y la luz sale por encima de él.
        if (values.count("canal") != 0 && values.at("canal") >= 0.5) {
            const double strip = v("tira_ancho"), wallThick = v("canal_pared");
            const double inner = (round ? v("diametro") : std::min(v("ancho"), v("alto"))) - 2.0 * v("pared");
            if (inner - 2.0 * strip - 2.0 * wallThick < 10.0) {
                throw std::runtime_error("La tira LED es demasiado ancha para esa caja: no queda sitio para el muro del canal");
            }
            const std::string stripP = p("tira_ancho"), wallP = p("canal_pared"), highP = p("canal_alto");
            param(stripP, strip);
            param(wallP, wallThick);
            param(highP, v("canal_alto"));
            const std::string ringW = inW + " - 2*" + stripP, ringH = inH + " - 2*" + stripP;
            recipe.features.push_back(shapeAt("Lightbox: muro del canal", Operation::Join, ringW, ringH, fondo + " - 0.01", highP + " + 0.01", "", ""));
            recipe.features.push_back(shapeAt("Lightbox: hueco del muro", Operation::Cut, ringW + " - 2*" + wallP, ringH + " - 2*" + wallP,
                                              fondo, highP + " + 1", "", ""));
            // El cable tiene que poder pasar: el muro del canal se agujerea por donde sale el cable (si no se cruzan, no quita nada).
            if (hasCable) {
                auto pass = addCylinder("Lightbox: paso del cable por el muro", Operation::Cut);
                if (wall == 0) {
                    pass->expressions = {{"diameter", cable}, {"y", "-(" + H + ")/4"}, {"z", "-1"}, {"height", fondo + " + " + highP + " + 2"}};
                } else {
                    const bool alongY = wall <= 2;
                    pass->base.origin = alongY ? OriginPlane::XZ : OriginPlane::YZ;
                    pass->symmetric = true;
                    // Desde 1 mm fuera de la pared hasta 1 mm más allá del muro: pared + canal + muro + 2 de largo.
                    const std::string reach = pared + " + " + stripP + " + " + wallP + " + 2";
                    const std::string centre = (alongY ? H : W) + "/2 + 1 - (" + reach + ")/2";
                    const bool positive = wall == 1 || wall == 4;
                    pass->expressions = {{"diameter", cable},
                                         {"y", fondo + " + (" + profundidad + " - " + fondo + ")/2"},
                                         {"z", positive ? centre : "-(" + centre + ")"},
                                         {"height", reach}};
                }
                recipe.features.push_back(std::move(pass));
            }
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
        return "Caja rectangular o redonda, abierta por arriba, y su tapa, que cierra con un labio a presión o con "
               "tornillos en las esquinas; opcionalmente con bisagra, clips, ranuras de ventilación, divisiones y patas.";
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
        GeneratorField screwSize;
        screwSize.key = "medida_tornillo";
        screwSize.label = "Tornillo";
        screwSize.kind = K::Choice;
        screwSize.choices = {"Medidas a mano", "M2", "M2,5", "M3", "M4", "M5", "M6"};
        screwSize.hint = "Pone el poste, el agujero para roscar y el de la tapa de ese tornillo";
        screwSize.showKey = "cierre";
        screwSize.showIndex = 1;
        GeneratorField feet;
        feet.key = "patas";
        feet.label = "Patas";
        feet.kind = K::Choice;
        feet.choices = {"Sin patas", "4 patas redondas"};
        GeneratorField dividers;
        dividers.key = "divisiones";
        dividers.label = "Divisiones interiores";
        dividers.kind = K::Count;
        dividers.value = 0.0;
        dividers.min = 0.0;
        dividers.max = 8.0;
        dividers.hint = "Paredes que reparten la caja a lo ancho en compartimentos iguales";
        GeneratorField slots;
        slots.key = "ranuras";
        slots.label = "Ranuras de ventilación";
        slots.kind = K::Count;
        slots.value = 0.0;
        slots.min = 0.0;
        slots.max = 20.0;
        slots.hint = "Ranuras que atraviesan la tapa, repartidas por el centro";
        GeneratorField shape;
        shape.key = "forma";
        shape.label = "Forma";
        shape.kind = K::Choice;
        shape.choices = {"Rectangular", "Redonda"};
        GeneratorField hinge;
        hinge.key = "bisagra";
        hinge.label = "Bisagra";
        hinge.kind = K::Choice;
        hinge.choices = {"Sin bisagra (la tapa va aparte)", "Bisagra atrás (con pasador de filamento)"};
        hinge.hint = "La tapa sale cerrada sobre la caja, unida por una bisagra de nudillos que se imprime en su sitio; el pasador es un trozo de filamento";
        hinge.showKey = "forma";
        hinge.showIndex = 0;
        GeneratorField wallVents;
        wallVents.key = "paredes_ranuras";
        wallVents.label = "Paredes con ranuras";
        wallVents.kind = K::Choice;
        wallVents.choices = {"Las largas (delante y detrás)", "Las cortas (los lados)", "Las cuatro"};
        wallVents.showKey = "forma";
        wallVents.showIndex = 0;
        GeneratorField wallSlotCount = length("ranuras_pared", "Ranuras por pared", 0.0, 0.0, 8.0,
                                              "0 = sin ranuras; cada una de 2 mm de alto, una sobre otra", "forma", 0);
        wallSlotCount.kind = K::Count;  // una cantidad: sin «mm»
        GeneratorField clips;
        clips.key = "clips";
        clips.label = "Clips para cerrarla";
        clips.kind = K::Choice;
        clips.choices = {"Sin clips", "2 clips en los lados"};
        clips.hint = "Dos lengüetas con gancho en la tapa que encajan en un resalte de la caja; el cierre por labio o por tornillos sigue funcionando";
        clips.showKey = "forma";
        clips.showIndex = 0;
        return grouped({shape,
                length("ancho", "Ancho (exterior)", 100.0, 20.0, 1000.0, {}, "forma", 0),
                length("fondo", "Fondo (exterior)", 60.0, 20.0, 1000.0, "El otro lado de la planta", "forma", 0),
                length("diametro", "Diámetro (exterior)", 100.0, 20.0, 1000.0, {}, "forma", 1),
                length("alto", "Alto de la caja", 40.0, 10.0, 500.0, "Sin la tapa"),
                length("pared", "Pared", 2.0, 0.8, 20.0, "Un múltiplo del ancho de la boquilla"),
                length("base", "Base", 2.0, 0.8, 20.0, "Grosor del fondo"),
                length("interior", "Redondeo del fondo por dentro", 2.0, 0.0, 10.0,
                       "Redondea por dentro la unión de la pared con el fondo: refuerza donde más fuerza se aplica (0 = esquina viva)",
                       "forma", 1),
                length("esquina", "Radio de esquinas", 3.0, 0.0, 30.0),
                length("tapa", "Grosor de la tapa", 2.0, 0.8, 20.0),
                closure,
                length("labio", "Alto del labio", 3.0, 1.0, 30.0, "Lo que entra la tapa en la caja", "cierre", 0),
                length("labio_pared", "Grosor del labio", 1.2, 0.4, 10.0, {}, "cierre", 0),
                length("holgura", "Holgura del labio", 0.2, 0.0, 2.0, "Aire entre el labio y la pared, por cada lado",
                       "cierre", 0),
                length("poste", "Diámetro de los postes", 8.0, 4.0, 30.0, "Uno en cada esquina, por dentro", "cierre", 1),
                length("tornillo", "Agujero del poste", 2.5, 1.0, 10.0, "Para roscar el tornillo (M3: 2,5)", "cierre", 1),
                length("pasante", "Agujero de la tapa", 3.4, 1.0, 12.0, "Para que el tornillo pase (M3: 3,4)", "cierre", 1),
                screwSize,
                dividers,
                slots,
                feet,
                length("pata_diametro", "Diámetro de las patas", 10.0, 4.0, 40.0, {}, "patas", 1),
                length("pata_alto", "Alto de las patas", 3.0, 1.0, 20.0, "Salen por debajo del fondo", "patas", 1),
                wallSlotCount,
                wallVents,
                hinge,
                length("bisagra_diametro", "Diámetro de los nudillos", 7.0, 5.0, 20.0, {}, "bisagra", 1),
                length("bisagra_largo", "Largo de la bisagra", 40.0, 15.0, 400.0, "Tres nudillos: dos de la caja y uno de la tapa", "bisagra", 1),
                length("bisagra_pasador", "Pasador (filamento)", 1.75, 1.0, 6.0, "El agujero queda 0,3 mm más ancho", "bisagra", 1),
                clips,
                length("clip_ancho", "Ancho de los clips", 10.0, 4.0, 60.0, {}, "clips", 1),
                length("clip_largo", "Largo de los clips", 8.0, 5.0, 30.0, "Lo que baja la lengüeta por fuera de la caja", "clips", 1)}, {{"forma", "Medidas de la caja"}, {"tapa", "Tapa y cierre"}, {"divisiones", "Interior y ventilación"}, {"patas", "Patas"}, {"ranuras_pared", "Ranuras en las paredes"}, {"bisagra", "Bisagra"}, {"clips", "Clips"}});
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        // Todo en coordenadas del diseño: la caja de cada cuerpo se corre con las patas, los clips o la bisagra.
        using Point = std::array<double, 3>;
        const auto round = [](const GeneratorValues& v) { return answer(v, "forma") >= 0.5; };
        const auto width = [round](const GeneratorValues& v) { return round(v) ? v.at("diametro") : v.at("ancho"); };
        const auto depth = [round](const GeneratorValues& v) { return round(v) ? v.at("diametro") : v.at("fondo"); };
        const auto hinged = [round](const GeneratorValues& v) { return !round(v) && answer(v, "bisagra") >= 0.5; };
        // El centro de la tapa y la altura de su cara de abajo.
        const auto lidX = [width, hinged](const GeneratorValues& v) { return hinged(v) ? 0.0 : width(v) + 10.0; };
        const auto lidZ = [hinged](const GeneratorValues& v) { return hinged(v) ? v.at("alto") + 0.2 : 0.0; };
        // A media altura del labio (sale hacia arriba con la tapa aparte y hacia abajo con la tapa puesta).
        const auto lipZ = [hinged, lidZ](const GeneratorValues& v) {
            return hinged(v) ? lidZ(v) - answer(v, "labio") / 2.0 : v.at("tapa") + answer(v, "labio") / 2.0;
        };
        // El poste y la pata de la esquina de X e Y máximos (en la redonda, a 45°).
        const auto corner = [round](const GeneratorValues& v, double inset) {
            const double r = std::cos(std::numbers::pi / 4.0);
            return round(v) ? std::array<double, 2>{(v.at("diametro") / 2.0 - inset) * r, (v.at("diametro") / 2.0 - inset) * r}
                            : std::array<double, 2>{v.at("ancho") / 2.0 - inset, v.at("fondo") / 2.0 - inset};
        };
        const auto post = [corner](const GeneratorValues& v) { return corner(v, v.at("pared") + answer(v, "poste") / 2.0 - 0.5); };
        const auto leg = [corner](const GeneratorValues& v) { return corner(v, v.at("pared") + answer(v, "pata_diametro") / 2.0); };
        const auto onPost = worldArrow('x', [post](const GeneratorValues& v) {
            return Point{post(v)[0], post(v)[1], v.at("alto")};
        }, {}, true);
        const auto hingeAt = [](const GeneratorValues& v) {
            const double d = answer(v, "bisagra_diametro");
            return Point{-answer(v, "bisagra_largo") / 2.0, v.at("fondo") / 2.0 + d / 2.0 - 1.0, v.at("alto") - d / 2.0};
        };
        return {
            {"ancho", worldArrow('x', [depth, width](const GeneratorValues& v) { return Point{-width(v) / 2.0, -depth(v) / 2.0, v.at("alto")}; })},
            {"fondo", worldArrow('y', [depth, width](const GeneratorValues& v) { return Point{-width(v) / 2.0, -depth(v) / 2.0, v.at("alto")}; })},
            {"diametro", worldArrow('x', [](const GeneratorValues& v) { return Point{0.0, 0.0, v.at("alto")}; }, {}, true)},
            {"alto", worldArrow('z', [width, depth, round](const GeneratorValues& v) {
                 return Point{-width(v) / 2.0, round(v) ? 0.0 : -depth(v) / 2.0, 0.0};
             })},
            {"pared", worldArrow('x', [width](const GeneratorValues& v) { return Point{width(v) / 2.0, 0.0, v.at("alto")}; }, backwards("pared"))},
            {"base", worldArrow('z', [depth](const GeneratorValues& v) { return Point{0.0, -depth(v) / 2.0, 0.0}; })},
            {"interior", worldArrow('z', [width](const GeneratorValues& v) { return Point{width(v) / 2.0 - v.at("pared"), 0.0, v.at("base")}; })},
            {"esquina", worldArrow('x', [depth, width](const GeneratorValues& v) { return Point{-width(v) / 2.0, -depth(v) / 2.0, v.at("alto")}; })},
            {"tapa", worldArrow('z', [lidX, lidZ, width](const GeneratorValues& v) { return Point{lidX(v) + width(v) / 2.0, 0.0, lidZ(v)}; })},
            {"labio", worldArrow('z', [lidX, lidZ, width, hinged](const GeneratorValues& v) {
                 return Point{lidX(v) - width(v) / 2.0 + v.at("pared") + v.at("holgura"), 0.0, hinged(v) ? lidZ(v) : v.at("tapa")};
             }, [hinged](const GeneratorValues& v) { return hinged(v) ? -v.at("labio") : v.at("labio"); })},
            {"labio_pared", worldArrow('x', [lidX, lipZ, width](const GeneratorValues& v) {
                 return Point{lidX(v) - width(v) / 2.0 + v.at("pared") + v.at("holgura"), 0.0, lipZ(v)};
             })},
            {"holgura", worldArrow('x', [lidX, lipZ, width](const GeneratorValues& v) {
                 return Point{lidX(v) - width(v) / 2.0 + v.at("pared"), 0.0, lipZ(v)};
             })},
            {"poste", onPost},
            {"tornillo", onPost},
            {"pasante", worldArrow('x', [post, lidX, lidZ](const GeneratorValues& v) {
                 return Point{lidX(v) + post(v)[0], post(v)[1], lidZ(v) + v.at("tapa")};
             }, {}, true)},
            {"pata_diametro", worldArrow('x', [leg](const GeneratorValues& v) { return Point{leg(v)[0], leg(v)[1], -answer(v, "pata_alto")}; }, {}, true)},
            {"pata_alto", worldArrow('z', [leg](const GeneratorValues& v) { return Point{leg(v)[0], leg(v)[1], 0.0}; }, backwards("pata_alto"))},
            {"bisagra_diametro", worldArrow('z', hingeAt, {}, true)},
            {"bisagra_largo", worldArrow('x', hingeAt)},
            {"bisagra_pasador", worldArrow('z', hingeAt, {}, true)},
            {"clip_ancho", worldArrow('y', [lidX, lidZ, hinged, width](const GeneratorValues& v) {
                 const double reach = answer(v, "clip_largo");
                 return Point{lidX(v) + width(v) / 2.0 + 2.6, 0.0, hinged(v) ? lidZ(v) - reach : v.at("tapa") + reach};
             }, {}, true)},
            {"clip_largo", worldArrow('z', [lidX, lidZ, hinged, width](const GeneratorValues& v) {
                 return Point{lidX(v) + width(v) / 2.0 + 2.6, 0.0, hinged(v) ? lidZ(v) : v.at("tapa")};
             }, [hinged](const GeneratorValues& v) { return hinged(v) ? -answer(v, "clip_largo") : answer(v, "clip_largo"); })}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const bool screws = values.at("cierre") >= 0.5;
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        // La medida de tornillo del catálogo manda sobre las tres medidas sueltas (poste, agujero de roscar y agujero de
        // paso, en mm); «medidas a mano» las deja como están. Los diseños anteriores no traen las preguntas nuevas.
        GeneratorValues used = values;
        if (const int size = values.count("medida_tornillo") != 0 ? int(values.at("medida_tornillo")) : 0; size >= 1 && size <= 6) {
            struct Screw {
                double post, pilot, clear;
            };
            static constexpr Screw kCatalog[] = {{6.0, 1.6, 2.4}, {7.0, 2.05, 2.9}, {8.0, 2.5, 3.4}, {10.0, 3.3, 4.5}, {12.0, 4.2, 5.5}, {14.0, 5.0, 6.6}};
            used["poste"] = kCatalog[size - 1].post;
            used["tornillo"] = kCatalog[size - 1].pilot;
            used["pasante"] = kCatalog[size - 1].clear;
        }
        const auto v = [&](const char* key) { return used.at(key); };
        const auto opt = [&](const char* key) { return values.count(key) != 0 ? values.at(key) : 0.0; };
        const bool round = opt("forma") >= 0.5;
        const bool hinge = !round && opt("bisagra") >= 0.5;
        const bool clips = !round && opt("clips") >= 0.5;
        const std::string ancho = p("ancho"), fondo = p("fondo"), diametro = p("diametro"), alto = p("alto"), pared = p("pared"),
                          base = p("base"), esquina = p("esquina"), tapa = p("tapa"), labio = p("labio"), labioPared = p("labio_pared"),
                          holgura = p("holgura"), poste = p("poste"), tornillo = p("tornillo"), pasante = p("pasante");

        Recipe recipe;
        recipe.component = "Caja con tapa";
        recipe.bodyNames = {"Caja", "Tapa"};
        const auto param = [&](const std::string& name, double value) {
            recipe.parameters.push_back({name, number(value)});
        };
        if (round) {
            param(diametro, v("diametro"));
        } else {
            param(ancho, v("ancho"));
            param(fondo, v("fondo"));
            param(esquina, v("esquina"));
        }
        param(alto, v("alto"));
        param(pared, v("pared"));
        param(base, v("base"));
        param(tapa, v("tapa"));
        const std::string interior = p("interior");
        if (round && opt("interior") > 0.0) {
            param(interior, opt("interior"));
        }
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
                                const std::string& z, const std::string& h, const std::string& x, const std::string& radius,
                                const std::string& y = {}) {
            auto f = std::make_unique<BoxFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->centered = true;
            f->operation = operation;
            f->expressions = {{"width", w}, {"depth", d}, {"z", z}, {"height", h}};
            if (!x.empty()) {
                f->expressions["x"] = x;
            }
            if (!y.empty()) {
                f->expressions["y"] = y;
            }
            if (!radius.empty()) {
                f->expressions["radius"] = radius;
            }
            return f;
        };
        const auto addCylinder = [&](const std::string& name, Operation operation, const std::string& diameterExpr,
                                     const std::string& x, const std::string& y, const std::string& z, const std::string& h) {
            auto f = std::make_unique<CylinderFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->operation = operation;
            f->expressions = {{"diameter", diameterExpr}, {"x", x}, {"y", y}, {"z", z}, {"height", h}};
            return f;
        };
        // Un cilindro con el eje a lo largo de X (la bisagra): va por el plano YZ, donde «x» es Y, «y» es Z y «z» es X.
        const auto addBarrel = [&](const std::string& name, Operation operation, const std::string& diameterExpr, const std::string& y,
                                   const std::string& z, const std::string& xStart, const std::string& length) {
            auto f = std::make_unique<CylinderFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::YZ;
            f->operation = operation;
            f->expressions = {{"diameter", diameterExpr}, {"x", y}, {"y", z}, {"z", xStart}, {"height", length}};
            return f;
        };
        // La planta de una pieza: rectangular (ancho × fondo, con el radio de las esquinas) o redonda (un cilindro de ese
        // diámetro). «w» es el ancho o el diámetro y «d» el fondo (el cilindro lo ignora).
        const auto piece = [&](const std::string& name, Operation operation, const std::string& w, const std::string& d,
                               const std::string& z, const std::string& h, const std::string& x, const std::string& radius) -> std::unique_ptr<Feature> {
            if (!round) {
                return addBox(name, operation, w, d, z, h, x, radius);
            }
            auto f = std::make_unique<CylinderFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->operation = operation;
            f->expressions = {{"diameter", w}, {"z", z}, {"height", h}};
            if (!x.empty()) {
                f->expressions["x"] = x;
            }
            return f;
        };
        const std::string W = round ? diametro : ancho;
        const std::string D = round ? diametro : fondo;
        const std::string inW = W + " - 2*" + pared;
        const std::string inD = D + " - 2*" + pared;
        const std::string beside = W + " + 10";
        // La tapa: aparte, al lado y apoyada en la cama; con bisagra, cerrada sobre la caja (a 0,2 mm para que no se peguen).
        const std::string lidX = hinge ? std::string() : beside;
        const std::string lidZ = hinge ? alto + " + 0.2" : std::string("0");
        const std::string lidXs = lidX.empty() ? std::string("0") : lidX;

        // 1. La caja y su hueco (abierta por arriba).
        recipe.features.push_back(piece("Caja: cuerpo", Operation::NewBody, W, D, "0", alto, "", esquina));
        recipe.features.push_back(piece("Caja: hueco", Operation::Cut, inW, inD, base, alto, "", ""));
        if (round && opt("interior") > 0.0) {
            // El hueco redondo con el borde de su fondo redondeado: al cortarlo, la unión pared-fondo queda reforzada.
            recipe.features.back()->expressions["baseRound"] = "min(" + interior + "; min((" + inW + ")/2 - 0.1; " + alto + " - " + base + " - 0.1))";
        }

        // Los postes de las cuatro esquinas, tangentes a la pared por dentro (con 0,5 mm metidos en ella para que se unan),
        // con su agujero para roscar; en la caja redonda van sobre la circunferencia, a 45°.
        const std::string px = round ? "(" + diametro + "/2 - " + pared + " - " + poste + "/2 + 0.5)*cos(45)"
                                     : ancho + "/2 - " + pared + " - " + poste + "/2 + 0.5";
        const std::string py = round ? px : fondo + "/2 - " + pared + " - " + poste + "/2 + 0.5";
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

        // Las divisiones (caja rectangular): paredes del grosor de la pared que reparten el interior a lo ancho; llegan 3 mm
        // por debajo del borde (para no estorbar al labio de la tapa) y se hunden 0,5 mm en las paredes y en la base.
        const int dividerCount = round ? 0 : int(opt("divisiones"));
        if (dividerCount > 0) {
            const std::string dividersP = p("divisiones");
            param(dividersP, double(dividerCount));
            for (int k = 1; k <= dividerCount; ++k) {
                const std::string x = "-(" + inW + ")/2 + (" + inW + ")*" + std::to_string(k) + "/(" + dividersP + " + 1)";
                recipe.features.push_back(addBox("Caja: división " + std::to_string(k), Operation::Join, pared, inD + " + 1", base + " - 0.5",
                                                 alto + " - " + base + " - 3 + 0.5", x, ""));
            }
        }
        // Las patas: cuatro discos bajo el fondo, hacia dentro de cada esquina (en la redonda, a 45°).
        if (opt("patas") >= 0.5) {
            const std::string legD = p("pata_diametro"), legH = p("pata_alto");
            param(legD, v("pata_diametro"));
            param(legH, v("pata_alto"));
            const std::string lx = round ? "(" + diametro + "/2 - " + legD + "/2 - " + pared + ")*cos(45)"
                                         : ancho + "/2 - " + legD + "/2 - " + pared;
            const std::string ly = round ? lx : fondo + "/2 - " + legD + "/2 - " + pared;
            for (int i = 0; i < 4; ++i) {
                const std::string sx = corners[i][0], sy = corners[i][1];
                recipe.features.push_back(addCylinder("Caja: pata " + std::to_string(i + 1), Operation::Join, legD, sx + "(" + lx + ")",
                                                       sy + "(" + ly + ")", "-" + legH, legH + " + 0.5"));
            }
        }
        // Las ranuras de ventilación en las paredes (caja rectangular): N ranuras de 2 mm una sobre otra, a media altura, que
        // atraviesan la pared; en las largas miden la mitad del ancho y en las cortas la mitad del fondo.
        const int wallSlots = round ? 0 : int(opt("ranuras_pared"));
        if (wallSlots > 0) {
            const std::string slotsP = p("ranuras_pared");
            param(slotsP, double(wallSlots));
            const int which = int(opt("paredes_ranuras"));  // 0 largas, 1 cortas, 2 las cuatro
            const std::string mid = base + " + (" + alto + " - " + base + ")/2";
            for (int k = 0; k < wallSlots; ++k) {
                const std::string z = mid + " + (" + std::to_string(k) + " - (" + slotsP + " - 1)/2)*4 - 1";
                if (which == 0 || which == 2) {
                    for (int side = 0; side < 2; ++side) {
                        const std::string sign = side == 0 ? "" : "-";
                        recipe.features.push_back(addBox("Caja: ranura larga " + std::to_string(k + 1) + (side == 0 ? "a" : "b"), Operation::Cut,
                                                         "(" + inW + ")*0.5", pared + " + 2", z, "2", "", "", sign + "(" + fondo + "/2 - " + pared + "/2)"));
                    }
                }
                if (which == 1 || which == 2) {
                    for (int side = 0; side < 2; ++side) {
                        const std::string sign = side == 0 ? "" : "-";
                        recipe.features.push_back(addBox("Caja: ranura corta " + std::to_string(k + 1) + (side == 0 ? "a" : "b"), Operation::Cut,
                                                         pared + " + 2", "(" + inD + ")*0.5", z, "2", sign + "(" + ancho + "/2 - " + pared + "/2)", ""));
                    }
                }
            }
        }

        // La bisagra de nudillos (caja rectangular), a lo largo del borde de atrás (+Y) y a la altura del borde de arriba: dos
        // nudillos de la caja y uno de la tapa entre ellos, con 0,4 mm de aire; el agujero del pasador los cruza.
        std::string hingeAxisZ;
        std::string hingeAxisY;
        std::string hingeHoleX0;
        std::string hingeTotal;
        if (hinge) {
            const std::string hd = p("bisagra_diametro"), total = p("bisagra_largo"), pin = p("bisagra_pasador");
            param(hd, v("bisagra_diametro"));
            param(total, v("bisagra_largo"));
            param(pin, v("bisagra_pasador"));
            if (v("bisagra_largo") > v("ancho") - 6.0) {
                throw std::runtime_error("La bisagra es demasiado larga para el ancho de la caja");
            }
            // El eje queda «diámetro/2» bajo el borde de arriba de la caja: así la parte de arriba de los nudillos de la caja llega
            // justo al borde y no se mete en la tapa, que está 0,2 mm más arriba; la tapa los alcanza por un puente.
            hingeAxisZ = alto + " - " + hd + "/2";
            hingeTotal = total;
            const std::string knuckle = "(" + total + " - 0.8)/3";           // el largo de cada nudillo
            const std::string left = "-(" + total + ")/2";                     // X donde empieza el primero
            hingeHoleX0 = left;
            // El eje queda por fuera de la pared (a «diámetro/2 − 1» de su cara exterior) para que los nudillos no entren en la
            // caja: solo se hunden 1 mm en la pared (y en la placa de la tapa) para unirse a ellas.
            const std::string y = fondo + "/2 + " + hd + "/2 - 1";
            hingeAxisY = y;
            // Los dos nudillos de la caja (se unen a la pared de atrás, por debajo del borde).
            recipe.features.push_back(addBarrel("Caja: nudillo 1", Operation::Join, hd, y, hingeAxisZ, left, knuckle));
            recipe.features.push_back(addBarrel("Caja: nudillo 2", Operation::Join, hd, y, hingeAxisZ, "(" + total + ")/2 - " + knuckle, knuckle));
            // El hueco en la pared para el nudillo de la tapa (algo más ancho que él, para que gire).
            const std::string between = "(" + total + " - 2*" + knuckle + ") - 0.02";  // el hueco que dejan los dos nudillos
            recipe.features.push_back(addBarrel("Caja: hueco del nudillo de la tapa", Operation::Cut, hd + " + 0.8", y, hingeAxisZ,
                                                left + " + " + knuckle + " + 0.01", between));
            // Y la pared se rebaja 2 mm en ese tramo, para que pase el puente que une el nudillo de la tapa con su placa.
            recipe.features.push_back(addBox("Caja: hueco del puente de la tapa", Operation::Cut, between, "4.5", alto + " - 2", "3.5", "",
                                             "", fondo + "/2 - 0.2"));
        }

        // 2. La tapa (al lado, apoyada en la cama; con bisagra, cerrada sobre la caja).
        recipe.features.push_back(piece("Tapa: placa", Operation::NewBody, W, D, lidZ, tapa, lidX, esquina));
        if (hinge) {
            const std::string hd = p("bisagra_diametro"), total = p("bisagra_largo");
            const std::string knuckle = "(" + total + " - 0.8)/3";
            // El puente: baja de la placa hasta el nudillo, en el tramo de la tapa.
            recipe.features.push_back(addBox("Tapa: puente de la bisagra", Operation::Join, "(" + total + " - 2*" + knuckle + ") - 0.8", "2",
                                             alto + " - 1", "1.21", "", "", fondo + "/2"));
            recipe.features.push_back(addBarrel("Tapa: nudillo", Operation::Join, hd, hingeAxisY, hingeAxisZ,
                                                "-(" + total + ")/2 + " + knuckle + " + 0.4", "(" + total + ") - 2*" + knuckle + " - 0.8"));
        }
        // Las ranuras de ventilación de la tapa: cortes alargados (2 mm de ancho) repartidos por el centro.
        const int slotCount = int(opt("ranuras"));
        if (slotCount > 0) {
            const std::string slotsP = p("ranuras");
            param(slotsP, double(slotCount));
            for (int k = 0; k < slotCount; ++k) {
                const std::string x = lidXs + " + (" + inW + ")*0.6*(" + std::to_string(k) + " + 0.5)/" + slotsP + " - (" + inW + ")*0.3";
                recipe.features.push_back(addBox("Tapa: ranura " + std::to_string(k + 1), Operation::Cut, "2", "(" + inD + ")*0.5", lidZ + " - 1",
                                                 tapa + " + 2", x, ""));
            }
        }
        if (screws) {
            for (int i = 0; i < 4; ++i) {
                const std::string sx = corners[i][0], sy = corners[i][1];
                recipe.features.push_back(addCylinder("Tapa: agujero " + std::to_string(i + 1), Operation::Cut, pasante,
                                                       lidXs + (sx.empty() ? " + " : " - ") + "(" + px + ")", sy + "(" + py + ")",
                                                       lidZ + " - 1", tapa + " + 2"));
            }
        } else {
            // El labio: un anillo que sale de la placa hacia la caja (hacia arriba si la tapa va aparte, hacia abajo si va
            // cerrada) y entra en ella con holgura.
            const std::string lipW = inW + " - 2*" + holgura, lipD = inD + " - 2*" + holgura;
            const std::string lipInW = lipW + " - 2*" + labioPared, lipInD = lipD + " - 2*" + labioPared;
            if (hinge) {
                recipe.features.push_back(piece("Tapa: labio", Operation::Join, lipW, lipD, lidZ + " - " + labio, labio + " + 0.01", lidX, ""));
                recipe.features.push_back(piece("Tapa: hueco del labio", Operation::Cut, lipInW, lipInD, lidZ + " - " + labio + " - 1",
                                                labio + " + 1", lidX, ""));
            } else {
                recipe.features.push_back(piece("Tapa: labio", Operation::Join, lipW, lipD, tapa + " - 0.01", labio + " + 0.01", lidX, ""));
                recipe.features.push_back(piece("Tapa: hueco del labio", Operation::Cut, lipInW, lipInD, tapa, labio + " + 1", lidX, ""));
            }
        }

        // 3. Los clips (caja rectangular): en cada lado corto (±X) un resalte en la caja y una lengüeta con gancho en la tapa que
        // lo abraza. Medidas (mm): resalte de 1 de saliente y 1,5 de alto; la lengüeta a 1,4 de la pared y 1,2 de gruesa; el
        // gancho entra 1,2 hacia la pared y sube 1,5. Con la tapa cerrada el gancho queda justo bajo el resalte.
        if (clips) {
            const std::string cw = p("clip_ancho"), cl = p("clip_largo");
            param(cw, v("clip_ancho"));
            param(cl, v("clip_largo"));
            for (int side = 0; side < 2; ++side) {
                const std::string sg = side == 0 ? "" : "-";
                const std::string tag = side == 0 ? "a" : "b";
                // El resalte, en la pared exterior, a «largo − 1,9» bajo el borde de arriba.
                recipe.features.push_back(addBox("Caja: resalte del clip " + tag, Operation::Join, "1.5", cw + " + 2", alto + " + 0.2 - " + cl + " + 1.9", "1.5",
                                                 sg + "(" + ancho + "/2 + 0.25)", ""));
                // La lengüeta de la tapa: sube (o baja, con la tapa cerrada) desde la placa por fuera de la caja.
                const std::string tabX = lidXs + (side == 0 ? " + " : " - ") + "(" + ancho + "/2 + 2)";
                const std::string bridgeX = lidXs + (side == 0 ? " + " : " - ") + "(" + ancho + "/2 + 0.75)";
                const std::string hookX = lidXs + (side == 0 ? " + " : " - ") + "(" + ancho + "/2 + 0.9)";
                std::string tabZ, tabH, hookZ, bridgeZ;
                if (hinge) {
                    bridgeZ = lidZ;
                    tabZ = lidZ + " - " + cl;
                    tabH = cl + " + " + tapa;
                    hookZ = lidZ + " - " + cl;
                } else {
                    bridgeZ = "0";
                    tabZ = "0";
                    tabH = cl + " + " + tapa;
                    hookZ = tapa + " + " + cl + " - 1.5";
                }
                recipe.features.push_back(addBox("Tapa: puente del clip " + tag, Operation::Join, "2.5", cw, bridgeZ, tapa, bridgeX, ""));
                recipe.features.push_back(addBox("Tapa: lengüeta del clip " + tag, Operation::Join, "1.2", cw, tabZ, tabH, tabX, ""));
                recipe.features.push_back(addBox("Tapa: gancho del clip " + tag, Operation::Join, "1.4", cw, hookZ, "1.5", hookX, ""));
            }
        }

        // El agujero del pasador de la bisagra cruza los tres nudillos (la tapa y la caja a la vez).
        if (hinge) {
            const std::string pin = p("bisagra_pasador");
            recipe.features.push_back(addBarrel("Bisagra: agujero del pasador", Operation::Cut, pin + " + 0.3", hingeAxisY, hingeAxisZ,
                                                hingeHoleX0 + " - 1", hingeTotal + " + 2"));
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
        text.multiline = true;
        text.hint = "Varias líneas: pulsa Intro para empezar otra";
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
        return grouped({choice("tipo", "Tipo", {"Letrero", "Llavero"}),
                text,
                font,
                length("altura_texto", "Tamaño del texto", 20.0, 3.0, 300.0, "Tamaño de la fuente; las letras mayúsculas miden ~70 %"),
                choice("alineacion", "Alineación", {"Centrado", "A la izquierda", "A la derecha"}),
                length("interlinea", "Espacio entre líneas", 2.0, 0.0, 100.0, "Se suma a la altura del texto (solo con varias líneas)"),
                mode,
                length("margen", "Margen de la placa", 5.0, 1.0, 100.0, "Entre el texto y el borde"),
                length("grosor", "Grosor de la placa", 3.0, 0.8, 50.0),
                length("relieve", "Relieve o profundidad", 1.2, 0.2, 50.0, "Cuánto sobresale (o se hunde) el texto"),
                length("esquina", "Radio de esquinas", 3.0, 0.0, 50.0),
                length("anilla", "Diámetro de la anilla", 12.0, 5.0, 60.0, "El círculo que rodea el agujero", "tipo", 1),
                length("agujero", "Agujero de la anilla", 5.0, 2.0, 40.0, {}, "tipo", 1)}, {{"texto", "Texto"}, {"modo", "Placa"}, {"anilla", "Anilla de llavero"}});
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
        // El relieve sale de la placa, entra en ella (grabado) o es la altura de las letras sueltas.
        ArrowSpec relief{0, 'z', false};
        relief.at = [](const GeneratorValues& v) {
            const int mode = int(v.at("modo"));
            return std::array<double, 3>{kCenter, kCenter, mode == 2 ? 0.0 : mode == 1 ? v.at("grosor") - v.at("relieve") : v.at("grosor")};
        };
        // El tamaño del texto, desde el borde de abajo de las letras (dentro del margen de la placa), y el espacio entre líneas
        // a continuación.
        const auto bottom = [](const GeneratorValues& v) { return int(v.at("modo")) == 2 ? 0.0 : v.at("margen"); };
        ArrowSpec textHeight{0, 'y', false};
        textHeight.at = [bottom](const GeneratorValues& v) { return std::array<double, 3>{kCenter, bottom(v), v.at("grosor")}; };
        ArrowSpec spacing{0, 'y', false};
        spacing.at = [bottom](const GeneratorValues& v) {
            return std::array<double, 3>{kCenter, bottom(v) + v.at("altura_texto"), v.at("grosor")};
        };
        return {{"grosor", {0, 'z', false}}, {"relieve", relief}, {"margen", margin}, {"esquina", corner},
                {"anilla", ring}, {"agujero", hole}, {"altura_texto", textHeight}, {"interlinea", spacing}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto v = [&](const char* key) { return values.at(key); };
        const bool keychain = v("tipo") >= 0.5;
        const int mode = int(v("modo"));  // 0 relieve, 1 grabado, 2 solo letras
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string textKey = values.texts.count("texto") ? values.texts.at("texto") : std::string();
        const std::string family = values.texts.count("fuente") ? values.texts.at("fuente") : std::string();

        // El contorno de las letras, línea por línea: la primera arriba y las demás debajo, a la distancia de una línea base a la
        // siguiente (altura del texto por 1,1 más el espacio extra), alineadas a la izquierda, al centro o a la derecha.
        // Los diseños anteriores no traen alineación ni espacio: se leen como centrado y 2 mm.
        const int align = values.count("alineacion") != 0 ? int(values.at("alineacion")) : 0;
        const double extraSpace = values.count("interlinea") != 0 ? values.at("interlinea") : 2.0;
        const double pitch = v("altura_texto") * 1.1 + extraSpace;
        std::vector<std::string> lines;
        {
            std::string current;
            for (const char c : textKey) {
                if (c == '\n') {
                    lines.push_back(current);
                    current.clear();
                } else if (c != '\r') {
                    current.push_back(c);
                }
            }
            lines.push_back(current);
        }
        struct TextLine {
            std::vector<sketch::Geometry> outline;
            double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
            double row = 0.0;
        };
        std::vector<TextLine> rows;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find_first_not_of(" \t") == std::string::npos) {
                continue;  // una línea en blanco solo deja su hueco
            }
            TextLine row;
            row.outline = textOutline(lines[i], v("altura_texto"), family);
            row.row = -double(i) * pitch;
            for (const sketch::Geometry& g : row.outline) {
                for (const sketch::Vec2& point : sketch::polyline(g)) {
                    row.minX = std::min(row.minX, point.x);
                    row.maxX = std::max(row.maxX, point.x);
                    row.minY = std::min(row.minY, point.y);
                    row.maxY = std::max(row.maxY, point.y);
                }
            }
            if (!row.outline.empty()) {
                rows.push_back(std::move(row));
            }
        }
        if (rows.empty()) {
            throw std::runtime_error("No se pudo crear el texto (¿está vacío o la fuente no existe?)");
        }
        double widest = 0.0;
        for (const TextLine& row : rows) {
            widest = std::max(widest, row.maxX - row.minX);
        }
        // Desplazamiento de cada línea (para alinearla) y caja de todo el texto.
        std::vector<sketch::Vec2> offsets;
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        for (const TextLine& row : rows) {
            const double width = row.maxX - row.minX;
            const double dx = align == 1 ? -row.minX : align == 2 ? widest - width - row.minX : (widest - width) / 2.0 - row.minX;
            offsets.push_back({dx, row.row});
            minX = std::min(minX, row.minX + dx);
            maxX = std::max(maxX, row.maxX + dx);
            minY = std::min(minY, row.minY + row.row);
            maxY = std::max(maxY, row.maxY + row.row);
        }
        const sketch::Vec2 center{-(minX + maxX) / 2.0, -(minY + maxY) / 2.0};
        sketch::Sketch lettering;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const sketch::Vec2 shift{offsets[i].x + center.x, offsets[i].y + center.y};
            for (const sketch::EntityId id : sketch::importDrawing(lettering, {rows[i].outline, {}}, shift)) {
                lettering.entity(id)->locked = true;
            }
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

GeneratorField herringboneField(const char* single = "Una hélice (la rueda 2 en sentido contrario)")
{
    GeneratorField f;
    f.kind = GeneratorField::Kind::Choice;
    f.key = "espina";
    f.label = "Dientes helicoidales";
    f.value = 0.0;
    f.choices = {single, "Espina de pescado (doble hélice)"};
    f.hint = "La espina de pescado lleva dos mitades con la hélice contraria: cancela el empuje lateral. Necesita un ángulo de hélice";
    return f;
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
    GeneratorField groove;
    groove.kind = GeneratorField::Kind::Length;
    groove.key = "surco";
    groove.label = "Surco central";
    groove.value = 0.0;
    groove.min = 0.0;
    groove.max = 20.0;
    groove.hint = "Con espina de pescado: un surco de este ancho en el medio, donde se juntan las dos mitades (0 = sin surco)";
    return {thin, chamfer, groove};
}

// Cómo se colocan las dos ruedas del par: engranadas (a la distancia entre ejes) o separadas, una al lado de la otra,
// para imprimirlas sin que se toquen.
// Las flechas de los ajustes que son una medida sobre la rueda que está en el origen: el surco de la espina de pescado (a
// media altura) y el chaflán de la base. «radius» es el radio primitivo de esa rueda.
void addTuningArrows(std::map<std::string, Generator::ArrowSpec>& arrows, std::function<double(const GeneratorValues&)> radius)
{
    arrows["surco"] = worldArrow('z', [radius](const GeneratorValues& v) {
        return std::array<double, 3>{radius(v), 0.0, v.at("ancho_rueda") / 2.0};
    }, {}, true);
    arrows["chaflan_base"] = worldArrow('z', [radius](const GeneratorValues& v) { return std::array<double, 3>{radius(v), 0.0, 0.0}; });
}

GeneratorField placementField()
{
    GeneratorField f;
    f.kind = GeneratorField::Kind::Choice;
    f.key = "colocacion";
    f.label = "Colocación de las ruedas";
    f.value = 0.0;
    f.choices = {"Engranadas (a la distancia entre ejes)", "Separadas, para imprimir"};
    f.hint = "Separadas: la rueda 2 queda al lado de la 1, a 5 mm de sus puntas, para imprimirlas sin que se toquen";
    return f;
}

// Reparte las preguntas de un engranaje en dos secciones plegables (Fase 15, U8): las medidas y los ajustes de impresión.
std::vector<GeneratorField> withGroups(std::vector<GeneratorField> fields)
{
    for (GeneratorField& f : fields) {
        const bool printing = f.key == "juego" || f.key == "adelgazar" || f.key == "chaflan_base" || f.key == "surco" || f.key == "colocacion";
        f.group = printing ? "Ajustes de impresión" : "Medidas del engranaje";
    }
    return fields;
}

// «groove»: el generador lleva la pregunta «Surco central» (solo cuenta con espina de pescado; los diseños anteriores no la traen).
void addTuningParameters(Recipe& recipe, const GeneratorValues& values, const std::string& suffix, bool groove = true)
{
    recipe.parameters.push_back({"adelgazar" + suffix, number(tuningValue(values, "adelgazar"))});
    recipe.parameters.push_back({"chaflan_base" + suffix, number(tuningValue(values, "chaflan_base"))});
    if (groove) {
        const bool herringbone = tuningValue(values, "espina") > 0.5;
        recipe.parameters.push_back({"surco" + suffix, number(herringbone ? tuningValue(values, "surco") : 0.0)});
    }
}

void applyTuning(GearFeature& gear, const std::string& suffix)
{
    gear.expressions["backlash"] = "adelgazar" + suffix;
    gear.expressions["baseChamfer"] = "chaflan_base" + suffix;
    gear.expressions["herringboneGroove"] = "surco" + suffix;
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
        return withGroups({make(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "El tamaño del diente: igual en las dos ruedas"),
                make(K::Count, "dientes_1", "Dientes de la rueda 1", 12.0, 6.0, 300.0),
                make(K::Count, "dientes_2", "Dientes de la rueda 2", 24.0, 6.0, 300.0),
                make(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 35.0, "Igual en las dos ruedas (20° es lo normal)"),
                make(K::Length, "ancho_rueda", "Ancho de las ruedas", 8.0, 1.0, 200.0, "A lo largo del eje"),
                make(K::Angle, "helice", "Ángulo de hélice", 0.0, 0.0, 45.0,
                     "0 = dientes rectos; si no, la rueda 2 lleva el sentido contrario"),
                herringboneField(),
                make(K::Length, "agujero_1", "Agujero de la rueda 1", 5.0, 0.0, 100.0, "0 = macizo"),
                make(K::Length, "agujero_2", "Agujero de la rueda 2", 5.0, 0.0, 100.0, "0 = macizo"),
                make(K::Length, "juego", "Juego entre dientes", 0.15, 0.0, 2.0,
                     "Aire para que giren sin atascarse al imprimir (se suma a la distancia entre ejes)"),
                placementField(),
                tuningFields()[0], tuningFields()[1], tuningFields()[2]});
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
        std::map<std::string, ArrowSpec> all{
            {"ancho_rueda", {0, 'z', false}}, {"agujero_1", centeredOn(0)}, {"agujero_2", centeredOn(1)},
            {"modulo", centeredOn(0, pitch("dientes_1"))}, {"dientes_1", centeredOn(0, pitch("dientes_1"))},
            {"dientes_2", centeredOn(1, pitch("dientes_2"))}};
        addTuningArrows(all, [](const GeneratorValues& v) { return v.at("modulo") * v.at("dientes_1") / 2.0; });
        return all;
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), z1 = p("dientes_1"), z2 = p("dientes_2"), presion = p("presion"),
                          ancho = p("ancho_rueda"), helice = p("helice"), bore1 = p("agujero_1"), bore2 = p("agujero_2"),
                          juego = p("juego"), giro = p("giro_rueda_2"), correccion = p("correccion");

        Recipe recipe;
        recipe.component = "Par de engranajes";
        recipe.bodyNames = {"Engranaje 1", "Engranaje 2"};
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
        // Con pocos dientes la rueda 1 lleva corrección de perfil y la 2 la opuesta: así los ejes siguen a la misma
        // distancia. Solo si la rueda 2 aguanta perder cabeza (25 dientes o más).
        {
            const double kPi = 3.14159265358979323846;
            const double shift = values.at("dientes_2") >= 25.0
                                     ? geometry::recommendedProfileShift(int(values.at("dientes_1")), values.at("presion") * kPi / 180.0)
                                     : 0.0;
            param(correccion, number(shift));
        }
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
                              {"bore", bore}, {"helixAngle", sign + helice}, {"profileShift", sign + correccion}};
            applyTuning(*f, suffix);
            f->spec.herringbone = tuningValue(values, "espina") > 0.5;  // los diseños anteriores no la traen: una hélice
            return f;
        };
        recipe.features.push_back(wheel("Engranaje: rueda 1", z1, bore1, ""));
        auto second = wheel("Engranaje: rueda 2", z2, bore2, "-");
        // Separadas para imprimir: a la suma de los radios de las puntas (m·(z/2 + 1) cada una) más 5 mm de aire.
        const bool apart = values.count("colocacion") != 0 && values.at("colocacion") > 0.5;
        second->expressions["x"] = apart ? modulo + "*(" + z1 + " + " + z2 + ")/2 + 2*" + modulo + " + 5"
                                         : modulo + "*(" + z1 + " + " + z2 + ")/2 + " + juego;
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
        return withGroups({gearField(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "El tamaño del diente: igual en la corona y el piñón"),
                gearField(K::Count, "dientes_corona", "Dientes de la corona", 36.0, 20.0, 300.0,
                          "Mejor al menos 8 más que el piñón (si no, los dientes se estorban)"),
                gearField(K::Count, "dientes_pinon", "Dientes del piñón", 12.0, 6.0, 300.0),
                gearField(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 35.0, "Igual en los dos (20° es lo normal)"),
                gearField(K::Length, "ancho_rueda", "Ancho", 8.0, 1.0, 200.0, "A lo largo del eje"),
                gearField(K::Angle, "helice", "Ángulo de hélice", 0.0, 0.0, 45.0,
                          "0 = dientes rectos; si no, la corona y el piñón llevan el mismo sentido"),
                herringboneField("Una hélice (corona y piñón con el mismo sentido)"),
                gearField(K::Length, "aro", "Grosor del aro de la corona", 6.0, 1.0, 100.0,
                          "Material por fuera de los huecos de los dientes"),
                gearField(K::Length, "agujero_pinon", "Agujero del piñón", 5.0, 0.0, 100.0, "0 = macizo"),
                gearField(K::Length, "juego", "Juego entre dientes", 0.15, 0.0, 2.0,
                          "Aire para que giren sin atascarse al imprimir (acerca los ejes)"),
                tuningFields()[0], tuningFields()[1], tuningFields()[2]});
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
        std::map<std::string, ArrowSpec> all{
            {"ancho_rueda", {0, 'z', false}}, {"agujero_pinon", centeredArrow(1)},
            {"modulo", centeredArrow(0, pitch("dientes_corona"))},
            {"dientes_corona", centeredArrow(0, pitch("dientes_corona"))},
            {"dientes_pinon", centeredArrow(1, pitch("dientes_pinon"))},
            {"aro", {0, 'x', true}}};  // desde el borde de fuera de la corona, hacia dentro
        addTuningArrows(all, [](const GeneratorValues& v) { return v.at("modulo") * v.at("dientes_corona") / 2.0; });
        return all;
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), zc = p("dientes_corona"), zp = p("dientes_pinon"), presion = p("presion"),
                          ancho = p("ancho_rueda"), aro = p("aro"), bore = p("agujero_pinon"), juego = p("juego");

        Recipe recipe;
        recipe.component = "Engranaje interno";
        recipe.bodyNames = {"Corona", "Piñón"};
        const std::string helice = p("helice");
        recipe.parameters.push_back({helice, number(values.count("helice") != 0 ? values.at("helice") : 0.0)});
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
        ring->expressions["helixAngle"] = helice;  // los dos con el mismo sentido: en un engranaje interno engranan así
        ring->spec.herringbone = tuningValue(values, "espina") > 0.5;
        recipe.features.push_back(std::move(ring));

        auto pinion = std::make_unique<GearFeature>();
        pinion->setName("Engranaje: piñón");
        pinion->base.origin = OriginPlane::XY;
        pinion->expressions = {{"module", modulo}, {"teeth", zp}, {"pressureAngle", presion}, {"thickness", ancho},
                               {"bore", bore},
                               {"x", modulo + "*(" + zc + " - " + zp + ")/2 - " + juego}};
        applyTuning(*pinion, suffix);
        pinion->expressions["helixAngle"] = helice;
        pinion->spec.herringbone = tuningValue(values, "espina") > 0.5;
        recipe.features.push_back(std::move(pinion));
        return recipe;
    }
};

// --- Tren planetario ---------------------------------------------------------------------------------------
// Un sol en el centro, N planetas a su alrededor y una corona (engranaje interno) que los rodea: el «spinner» que en
// Fusion 360 se arma a mano. Los dientes de cada planeta ya llevan su fase para que engrane a la vez con el sol y con
// la corona (con 3 planetas a 120° hace falta que (dientes del sol + dientes de la corona) / N sea entero). Va con su
// portasatélites (una placa) y sus ejes (el central y uno por planeta), con la corona fija, una unión giratoria por
// rueda y por portasatélites, y un enlace de movimiento: al girar el sol, el portasatélites (con sus ejes y planetas)
// gira zs/(zs+zr) veces lo que el sol, y cada planeta gira además sobre su eje (fórmula de Willis, corona quieta).

class PlanetaryGear final : public Generator {
public:
    std::string category() const override { return "Engranajes"; }
    std::string id() const override { return "tren-planetario"; }
    std::string title() const override { return "Tren planetario"; }
    std::string icon() const override { return "gear-solid"; }
    std::string description() const override
    {
        return "Un sol, varios planetas y una corona que engranan a la vez, con el portasatélites, los ejes, las uniones "
               "giratorias y el enlace de movimiento puestos: gira el sol y los planetas lo siguen.";
    }

    std::vector<GeneratorField> fields() const override
    {
        using K = GeneratorField::Kind;
        GeneratorField thin = tuningFields()[0];
        thin.value = 0.1;  // un tren con tres engranes a la vez necesita aire para girar al imprimirlo
        return withGroups({gearField(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "El tamaño del diente: igual en el sol, los planetas y la corona"),
                gearField(K::Count, "dientes_sol", "Dientes del sol", 12.0, 6.0, 300.0),
                gearField(K::Count, "dientes_planeta", "Dientes de cada planeta", 12.0, 6.0, 300.0,
                          "La corona sale con dientes del sol + 2 × los del planeta"),
                gearField(K::Count, "planetas", "Planetas", 3.0, 2.0, 8.0,
                          "Repartidos a partes iguales; (dientes del sol + dientes de la corona) ÷ planetas debe ser entero"),
                gearField(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 35.0, "Igual en todos (20° es lo normal)"),
                gearField(K::Length, "ancho_rueda", "Ancho", 8.0, 1.0, 200.0, "A lo largo del eje"),
                gearField(K::Angle, "helice", "Ángulo de hélice", 0.0, 0.0, 45.0,
                          "0 = dientes rectos; si no, el sol lleva un sentido y los planetas y la corona el contrario"),
                herringboneField("Una hélice (el sol en un sentido; los planetas y la corona en el otro)"),
                gearField(K::Length, "aro", "Grosor del aro de la corona", 6.0, 1.0, 100.0,
                          "Material por fuera de los huecos de los dientes"),
                gearField(K::Length, "agujero_sol", "Agujero del sol", 5.0, 1.0, 100.0, "Por él pasa el eje central"),
                gearField(K::Length, "agujero_planeta", "Agujero de cada planeta", 4.0, 1.0, 100.0, "Por él pasa su eje"),
                thin, tuningFields()[1], tuningFields()[2]});
    }

    std::string summary(const GeneratorValues& values) const override
    {
        const double zs = values.at("dientes_sol"), zp = values.at("dientes_planeta"), n = values.at("planetas");
        const double zr = zs + 2.0 * zp;
        const double check = (zs + zr) / n;
        char text[420];
        std::snprintf(text, sizeof text,
                      "Corona de %g dientes (fija) · por cada vuelta del sol, el portasatélites da %.3f vueltas y cada planeta gira %.3f veces sobre su eje%s",
                      zr, zs / (zs + zr), zr * zs / ((zs + zr) * zp),
                      std::abs(check - std::round(check)) > 1e-9
                          ? " · AVISO: (dientes del sol + de la corona) ÷ planetas no es entero: los planetas no pueden engranar todos a la vez"
                          : "");
        return text;
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        const auto pitch = [](const char* teeth) {
            return [teeth](const GeneratorValues& v) { return v.at("modulo") * v.at(teeth); };
        };
        std::map<std::string, ArrowSpec> all{
            {"ancho_rueda", {0, 'z', false}}, {"modulo", centeredArrow(0, pitch("dientes_sol"))},
            {"dientes_sol", centeredArrow(0, pitch("dientes_sol"))}, {"dientes_planeta", centeredArrow(1, pitch("dientes_planeta"))},
            {"agujero_sol", centeredArrow(0)}, {"agujero_planeta", centeredArrow(1)},
            // El aro, por fuera de los huecos de los dientes de la corona (que tiene sol + 2 × planeta dientes).
            {"aro", worldArrow('x', [](const GeneratorValues& v) {
                 const double teeth = v.at("dientes_sol") + 2.0 * v.at("dientes_planeta");
                 return std::array<double, 3>{v.at("modulo") * (teeth / 2.0 + 1.25), 0.0, v.at("ancho_rueda")};
             })}};
        addTuningArrows(all, [](const GeneratorValues& v) { return v.at("modulo") * v.at("dientes_sol") / 2.0; });
        return all;
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), zs = p("dientes_sol"), zp = p("dientes_planeta"), zr = p("dientes_corona"),
                          n = p("planetas"), presion = p("presion"), ancho = p("ancho_rueda"), helice = p("helice"),
                          aro = p("aro"), boreSun = p("agujero_sol"), borePlanet = p("agujero_planeta"),
                          giro = p("giro_planeta");
        const int count = int(values.at("planetas"));

        Recipe recipe;
        recipe.component = "Tren planetario";
        const auto param = [&](const std::string& name, const std::string& expression) {
            recipe.parameters.push_back({name, expression});
        };
        const auto fromValue = [&](const std::string& name, const char* key) { param(name, number(values.at(key))); };
        fromValue(modulo, "modulo");
        fromValue(zs, "dientes_sol");
        fromValue(zp, "dientes_planeta");
        fromValue(n, "planetas");
        fromValue(presion, "presion");
        fromValue(ancho, "ancho_rueda");
        param(helice, number(values.count("helice") != 0 ? values.at("helice") : 0.0));
        fromValue(aro, "aro");
        fromValue(boreSun, "agujero_sol");
        fromValue(borePlanet, "agujero_planeta");
        param(zr, zs + " + 2*" + zp);
        addTuningParameters(recipe, values, suffix);
        // La fase de los planetas: igual que en un par de engranajes (un diente del sol apuntando al planeta, un hueco de
        // éste apuntando al sol), reducida a un paso.
        const std::string halfStep = "180/" + zp;
        const std::string step = "360/" + zp;
        param(giro, "(180 - " + halfStep + ") - floor((180 - " + halfStep + ")/(" + step + "))*(" + step + ")");
        const std::string orbit = modulo + "*(" + zs + " + " + zp + ")/2";  // radio de la órbita de los planetas

        const auto gear = [&](const std::string& name, const std::string& teeth, const std::string& helix) {
            auto f = std::make_unique<GearFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->expressions = {{"module", modulo}, {"teeth", teeth}, {"pressureAngle", presion}, {"thickness", ancho},
                              {"helixAngle", helix}};
            applyTuning(*f, suffix);
            f->spec.herringbone = tuningValue(values, "espina") > 0.5;
            return f;
        };
        const auto cylinder = [&](const std::string& name, const std::string& diameter, const std::string& z,
                                  const std::string& height) {
            auto f = std::make_unique<CylinderFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->expressions = {{"diameter", diameter}, {"z", z}, {"height", height}};
            return f;
        };

        // Los cuerpos salen en este orden: sol, planetas, corona, portasatélites, ejes de los planetas y eje central.
        // (El índice de cada operación es el que usan las referencias «#k» de las uniones.)
        std::vector<std::string> names;
        const std::size_t sun = recipe.features.size();
        {
            auto f = gear("Engranaje: sol", zs, helice);
            f->expressions["bore"] = boreSun;
            recipe.features.push_back(std::move(f));
            names.push_back("Sol");
        }
        std::vector<std::size_t> planets;
        for (int k = 0; k < count; ++k) {
            const std::string angle = "360*" + std::to_string(k) + "/" + n;
            auto f = gear("Engranaje: planeta " + std::to_string(k + 1), zp, "-" + helice);
            f->expressions["bore"] = borePlanet;
            f->expressions["x"] = orbit + "*cos(" + angle + ")";
            f->expressions["y"] = orbit + "*sin(" + angle + ")";
            f->expressions["rotation"] = giro + " + " + angle + "*(" + zs + " + " + zp + ")/" + zp;
            planets.push_back(recipe.features.size());
            recipe.features.push_back(std::move(f));
            names.push_back("Planeta " + std::to_string(k + 1));
        }
        {
            auto f = gear("Engranaje: corona", zr, "-" + helice);
            f->spec.type = geometry::GearType::Internal;
            f->expressions.erase("pressureAngle");
            f->expressions["pressureAngle"] = presion;
            f->expressions["rim"] = aro;
            f->expressions["rotation"] = giro + "*" + zp + "/" + zr;
            recipe.features.push_back(std::move(f));
            names.push_back("Corona");
            recipe.fixedBodies.push_back(int(names.size()) - 1);  // lo único fijo: el resto gira respecto a ella
        }
        // El portasatélites: una placa debajo, del tamaño de la corona, y los ejes encima de ella.
        const std::size_t plate = recipe.features.size();
        recipe.features.push_back(cylinder("Tren: portasatélites", modulo + "*" + zr + " + 2*" + aro, "-4", "3"));
        names.push_back("Portasatélites");
        std::vector<std::size_t> pins;
        for (int k = 0; k < count; ++k) {
            const std::string angle = "360*" + std::to_string(k) + "/" + n;
            auto f = cylinder("Tren: eje del planeta " + std::to_string(k + 1), borePlanet + " - 0.3", "-1", ancho + " + 2");
            f->expressions["x"] = orbit + "*cos(" + angle + ")";
            f->expressions["y"] = orbit + "*sin(" + angle + ")";
            pins.push_back(recipe.features.size());
            recipe.features.push_back(std::move(f));
            names.push_back("Eje " + std::to_string(k + 1));
        }
        const std::size_t centralPin = recipe.features.size();
        recipe.features.push_back(cylinder("Tren: eje central", boreSun + " - 0.3", "-1", ancho + " + 2"));
        names.push_back("Eje central");
        recipe.bodyNames = names;

        // Una unión giratoria por rueda: su eje (el del agujero) sobre el del eje fijo.
        const auto axisOf = [](std::size_t feature) {
            return geometry::ElementRef{"@axis:#" + std::to_string(feature), 0};
        };
        const auto joint = [&](const std::string& name, std::size_t pin, std::size_t wheel) {
            auto f = std::make_unique<JointFeature>();
            f->setName(name);
            f->fixedFace = axisOf(pin);
            f->movingFace = axisOf(wheel);
            f->type = JointType::Revolute;
            return f;
        };
        std::vector<std::size_t> joints;
        joints.push_back(recipe.features.size());
        recipe.features.push_back(joint("Tren: giro del sol", centralPin, sun));
        for (int k = 0; k < count; ++k) {
            joints.push_back(recipe.features.size());
            recipe.features.push_back(joint("Tren: giro del planeta " + std::to_string(k + 1), pins[std::size_t(k)], planets[std::size_t(k)]));
        }
        // El portasatélites gira alrededor del eje central y se lleva sus ejes y los planetas («#k» = el cuerpo de la
        // operación k). Su eje es el de la placa, que está más abajo que el eje central: el desfase (el centro de la placa
        // menos el del eje, a lo largo del eje) se da como distancia para que la unión no la mueva de sitio.
        const std::size_t carrier = recipe.features.size();
        {
            auto f = joint("Tren: giro del portasatélites", centralPin, plate);
            f->expressions["offset"] = "-2.5 - " + ancho + "/2";
            for (const std::size_t pin : pins) {
                f->alsoMove.push_back("#" + std::to_string(pin));
            }
            for (const std::size_t planet : planets) {
                f->alsoMove.push_back("#" + std::to_string(planet));
            }
            recipe.features.push_back(std::move(f));
        }
        // El enlace (Willis, con la corona fija): el sol gira θ; el portasatélites gira θ·zs/(zs + zr); cada planeta gira
        // sobre su eje −θ·zr·zs/((zs + zr)·zp) (el giro de órbita se lo da el portasatélites, que va después).
        auto link = std::make_unique<MotionLinkFeature>();
        link->setName("Tren: enlace de movimiento");
        link->members.push_back({kRecipeFeatureBase + FeatureId(joints[0]), 1.0});
        const double zsv = values.at("dientes_sol"), zpv = values.at("dientes_planeta");
        const double zrv = zsv + 2.0 * zpv;
        for (int k = 0; k < count; ++k) {
            link->members.push_back({kRecipeFeatureBase + FeatureId(joints[std::size_t(k) + 1]), -zrv * zsv / ((zsv + zrv) * zpv)});
        }
        link->members.push_back({kRecipeFeatureBase + FeatureId(carrier), zsv / (zsv + zrv)});
        recipe.features.push_back(std::move(link));
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
        return withGroups({gearField(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "El tamaño del diente: igual en la rueda y la barra"),
                gearField(K::Count, "dientes_rueda", "Dientes de la rueda", 20.0, 6.0, 300.0),
                mode,
                teeth,
                exact,
                rounded,
                gearField(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 30.0, "Igual en los dos (20° es lo normal)"),
                gearField(K::Length, "ancho_rueda", "Ancho", 8.0, 1.0, 200.0, "A lo largo del eje"),
                gearField(K::Angle, "helice", "Ángulo de hélice", 0.0, 0.0, 45.0,
                          "0 = dientes rectos; si no, los dientes de la barra se inclinan lo mismo que los de la rueda"),
                herringboneField("Una hélice (la barra con la misma inclinación)"),
                gearField(K::Length, "base", "Grosor de la base de la barra", 5.0, 1.0, 100.0, "Bajo la raíz de los dientes"),
                gearField(K::Length, "agujero_rueda", "Agujero de la rueda", 5.0, 0.0, 100.0, "0 = macizo"),
                gearField(K::Length, "juego", "Juego entre dientes", 0.15, 0.0, 2.0,
                          "Aire para que engranen sin atascarse al imprimir (aleja la barra de la rueda)"),
                tuningFields()[0], tuningFields()[1], tuningFields()[2]});
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
        std::map<std::string, ArrowSpec> all{
            {"ancho_rueda", {0, 'z', false}}, {"agujero_rueda", centeredArrow(0)},
            {"modulo", centeredArrow(0, pitch)}, {"dientes_rueda", centeredArrow(0, pitch)},
            {"largo_exacto", {1, 'x', false}}, {"largo_aprox", {1, 'x', false}}, {"base", {1, 'y', false}}};
        addTuningArrows(all, [](const GeneratorValues& v) { return v.at("modulo") * v.at("dientes_rueda") / 2.0; });
        return all;
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), z = p("dientes_rueda"), n = p("dientes_cremallera"), presion = p("presion"),
                          ancho = p("ancho_rueda"), base = p("base"), bore = p("agujero_rueda"), juego = p("juego"),
                          giro = p("giro_rueda"), largo = p("largo");
        const RackSize rack = rackSize(values);

        Recipe recipe;
        recipe.component = "Cremallera y rueda";
        recipe.bodyNames = {"Rueda", "Cremallera"};
        const std::string helice = p("helice");
        recipe.parameters.push_back({helice, number(values.count("helice") != 0 ? values.at("helice") : 0.0)});
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
        wheel->expressions["helixAngle"] = helice;
        wheel->spec.herringbone = tuningValue(values, "espina") > 0.5;
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
        bar->expressions["helixAngle"] = helice;  // con el mismo ángulo y signo que la rueda, la barra sigue a sus dientes
        bar->spec.herringbone = tuningValue(values, "espina") > 0.5;
        recipe.features.push_back(std::move(bar));
        return recipe;
    }
};

// --- Tornillo sin fin y rueda (E4) ----------------------------------------------------------------
// Un tornillo de 1 a 4 entradas (perfil axial de cremallera) y la rueda helicoidal que engrana con él: el módulo es el
// axial del tornillo y el transversal de la rueda, y la rueda lleva como ángulo de hélice el ángulo de avance del
// tornillo, del mismo sentido. Los ejes se cruzan a 90°, a la distancia (Ø tornillo + Ø rueda)/2 (más el juego), y la
// rueda va girada para que un hueco de ella caiga sobre un filete del tornillo. La rueda es cilíndrica (sin garganta).

class WormAndWheel final : public Generator {
public:
    std::string category() const override { return "Engranajes"; }
    std::string id() const override { return "tornillo-sin-fin"; }
    std::string title() const override { return "Tornillo sin fin y rueda"; }
    std::string icon() const override { return "gear-solid"; }
    std::string description() const override
    {
        return "Un tornillo sin fin (de 1 a 4 entradas) y la rueda helicoidal que engrana con él, con los ejes a 90°, a la "
               "distancia correcta y con la rueda girada para que encajen. Reducciones grandes en poco espacio; con pocas "
               "entradas y un diámetro grande el tornillo no se deja mover por la rueda (autobloqueo).";
    }

    std::vector<GeneratorField> fields() const override
    {
        using K = GeneratorField::Kind;
        GeneratorField hand;
        hand.kind = K::Choice;
        hand.key = "mano";
        hand.label = "Sentido de la hélice";
        hand.value = 0.0;
        hand.choices = {"A derechas", "A izquierdas"};
        hand.hint = "La rueda lleva el mismo sentido";
        return withGroups({gearField(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "Axial en el tornillo y transversal en la rueda"),
                gearField(K::Count, "entradas", "Entradas del tornillo", 1.0, 1.0, 4.0, "Más entradas: más avance por vuelta y menos reducción"),
                gearField(K::Count, "dientes_rueda", "Dientes de la rueda", 30.0, 6.0, 300.0),
                gearField(K::Length, "diam_tornillo", "Diámetro primitivo del tornillo", 20.0, 5.0, 200.0,
                          "Suele valer de 8 a 12 módulos; más grueso, menos ángulo de avance"),
                gearField(K::Length, "largo_tornillo", "Largo del tornillo", 40.0, 8.0, 400.0, "Se ajusta a pasos axiales enteros"),
                hand,
                gearField(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 25.0, "Axial en el tornillo (20° es lo normal)"),
                gearField(K::Length, "ancho_rueda", "Ancho de la rueda", 8.0, 1.0, 200.0, "A lo largo de su eje"),
                gearField(K::Length, "agujero_rueda", "Agujero de la rueda", 5.0, 0.0, 100.0, "0 = macizo"),
                gearField(K::Length, "juego", "Juego entre dientes", 0.15, 0.0, 2.0,
                          "Aire para que giren sin atascarse al imprimir (aleja el tornillo de la rueda)"),
                tuningFields()[0]});
    }

    std::string summary(const GeneratorValues& values) const override
    {
        constexpr double kPi = 3.14159265358979323846;
        const double m = values.at("modulo"), n = values.at("entradas"), z = values.at("dientes_rueda"),
                     d = values.at("diam_tornillo");
        const double lead = std::atan(n * m / d) * 180.0 / kPi;
        char text[320];
        std::snprintf(text, sizeof text,
                      "Reducción %g:%g (el tornillo da %g vueltas por una de la rueda) · ángulo de avance %.1f°%s · distancia "
                      "entre ejes %s · Ø primitivo de la rueda %s",
                      z, n, z / n, lead, lead < 5.0 ? " (probablemente autobloqueante)" : "",
                      geometry::display::length((d + m * z) / 2.0 + values.at("juego")).c_str(),
                      geometry::display::length(m * z).c_str());
        return text;
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        const auto pitch = [](const GeneratorValues& v) { return v.at("modulo") * v.at("dientes_rueda"); };
        // El tornillo va tumbado a lo largo de X: su diámetro se mide en vertical, por el centro.
        ArrowSpec screw{1, 'z', false, true};
        screw.at = [](const GeneratorValues&) { return std::array<double, 3>{kCenter, kCenter, kCenter}; };
        return {{"ancho_rueda", {0, 'z', false}}, {"modulo", centeredArrow(0, pitch)}, {"dientes_rueda", centeredArrow(0, pitch)},
                {"agujero_rueda", centeredArrow(0)}, {"diam_tornillo", screw}, {"largo_tornillo", {1, 'x', false}}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), n = p("entradas"), z = p("dientes_rueda"), diam = p("diam_tornillo"),
                          largo = p("largo_tornillo"), mano = p("mano"), presion = p("presion"), ancho = p("ancho_rueda"),
                          bore = p("agujero_rueda"), juego = p("juego"), avance = p("avance"), helice = p("helice_rueda"),
                          giro = p("giro_rueda"), xrueda = p("x_rueda");

        Recipe recipe;
        recipe.component = "Tornillo sin fin y rueda";
        recipe.bodyNames = {"Rueda", "Tornillo sin fin"};
        const auto param = [&](const std::string& name, const std::string& expression) {
            recipe.parameters.push_back({name, expression});
        };
        param(modulo, number(values.at("modulo")));
        param(n, number(values.at("entradas")));
        param(z, number(values.at("dientes_rueda")));
        param(diam, number(values.at("diam_tornillo")));
        param(largo, number(values.at("largo_tornillo")));
        param(mano, number(values.count("mano") != 0 ? values.at("mano") : 0.0));
        param(presion, number(values.at("presion")));
        param(ancho, number(values.at("ancho_rueda")));
        param(bore, number(values.at("agujero_rueda")));
        param(juego, number(values.at("juego")));
        addTuningParameters(recipe, values, suffix, false);
        // Ángulo de avance del tornillo (grados) = ángulo de hélice de la rueda; con el sentido a izquierdas, negativo.
        param(avance, "atan(" + n + "*" + modulo + "/" + diam + ")");
        param(helice, "(1 - 2*" + mano + ")*" + avance);
        // La rueda se centra a un número entero de pasos axiales del extremo del tornillo, y se gira para que el hueco de
        // un diente (a mitad de su ancho, donde la hélice la ha girado la mitad) caiga sobre un filete.
        param(xrueda, modulo + "*pi*round(" + largo + "/(2*" + modulo + "*pi))");
        const std::string step = "360/" + z;
        const std::string start = "(-90 + 180/" + z + " - (" + ancho + "*tan(" + helice + ")/(" + modulo + "*" + z +
                                  "/2))*180/pi/2)";
        param(giro, start + " - floor(" + start + "/(" + step + "))*(" + step + ")");

        auto wheel = std::make_unique<GearFeature>();
        wheel->setName("Engranaje: rueda");
        wheel->base.origin = OriginPlane::XY;
        wheel->expressions = {{"module", modulo}, {"teeth", z}, {"pressureAngle", presion}, {"thickness", ancho},
                              {"bore", bore}, {"helixAngle", helice}, {"x", xrueda}, {"rotation", giro}};
        wheel->expressions["backlash"] = "adelgazar" + suffix;
        recipe.features.push_back(std::move(wheel));

        // El tornillo va por el plano YZ (su eje es el X del mundo), al otro lado de la rueda (Y negativa), a media altura.
        auto screw = std::make_unique<GearFeature>();
        screw->setName("Engranaje: tornillo sin fin");
        screw->base.origin = OriginPlane::YZ;
        screw->spec.type = geometry::GearType::Worm;
        screw->expressions = {{"module", modulo}, {"teeth", n}, {"wormDiameter", diam}, {"pressureAngle", presion},
                              {"thickness", largo}, {"leftHanded", mano},
                              {"x", "-((" + diam + " + " + modulo + "*" + z + ")/2 + " + juego + ")"}, {"y", ancho + "/2"}};
        recipe.features.push_back(std::move(screw));
        return recipe;
    }
};

// --- Engranajes cónicos (E5) -------------------------------------------------------------------
// Dos ruedas cónicas rectas con los ejes a 90°: el piñón (eje Z) y la corona (eje X), con el vértice común de los dos conos
// a un radio primitivo de cada uno. Mismo módulo (el del extremo grande) y ángulo de presión; los dientes son una
// aproximación: el perfil del extremo grande es el de una rueda recta de esos dientes con la cabeza y la raíz reducidas
// por el coseno del ángulo del cono, y el resto sale de reducirlo hacia el vértice (los flancos son rectas que pasan por él).

class BevelPair final : public Generator {
public:
    std::string category() const override { return "Engranajes"; }
    std::string id() const override { return "engranajes-conicos"; }
    std::string title() const override { return "Engranajes cónicos"; }
    std::string icon() const override { return "gear-solid"; }
    std::string description() const override
    {
        return "Dos ruedas cónicas rectas con los ejes a 90° (el piñón arriba y la corona de lado), con el vértice de los conos "
               "en común y una rueda girada para que los dientes encajen. Cambian la dirección del giro y la velocidad.";
    }

    std::vector<GeneratorField> fields() const override
    {
        using K = GeneratorField::Kind;
        return withGroups({gearField(K::Length, "modulo", "Módulo", 2.0, 0.3, 20.0, "El del extremo grande: igual en las dos ruedas"),
                gearField(K::Count, "dientes_1", "Dientes del piñón", 15.0, 6.0, 300.0, "Su eje es el vertical (Z)"),
                gearField(K::Count, "dientes_2", "Dientes de la corona", 30.0, 6.0, 300.0, "Su eje es el X"),
                gearField(K::Angle, "presion", "Ángulo de presión", 20.0, 5.0, 35.0, "Igual en las dos ruedas (20° es lo normal)"),
                gearField(K::Length, "ancho_cara", "Ancho de cara", 8.0, 1.0, 200.0,
                          "Medido sobre el cono; no debe pasar de un tercio de la distancia al vértice"),
                gearField(K::Length, "agujero_1", "Agujero del piñón", 5.0, 0.0, 100.0, "0 = macizo"),
                gearField(K::Length, "agujero_2", "Agujero de la corona", 5.0, 0.0, 100.0, "0 = macizo"),
                tuningFields()[0]});
    }

    std::string summary(const GeneratorValues& values) const override
    {
        constexpr double kPi = 3.14159265358979323846;
        const double m = values.at("modulo"), z1 = values.at("dientes_1"), z2 = values.at("dientes_2");
        const double cone = std::hypot(z1, z2) * m / 2.0;
        char text[320];
        std::snprintf(text, sizeof text,
                      "Relación %g:%g · conos de %.1f° y %.1f° · distancia al vértice %s (ancho de cara hasta %s) · Ø primitivos %s y %s",
                      z1, z2, std::atan2(z1, z2) * 180.0 / kPi, std::atan2(z2, z1) * 180.0 / kPi,
                      geometry::display::length(cone).c_str(), geometry::display::length(cone / 3.0).c_str(),
                      geometry::display::length(m * z1).c_str(), geometry::display::length(m * z2).c_str());
        return text;
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        // El piñón tiene el eje vertical; la corona, el eje X (su diámetro se mide a lo largo de Y).
        const auto pitch = [](const char* teeth) {
            return [teeth](const GeneratorValues& v) { return v.at("modulo") * v.at(teeth); };
        };
        const auto onCrown = [](std::function<double(const GeneratorValues&)> length = {}) {
            ArrowSpec spec{1, 'y', false, true};
            spec.at = [](const GeneratorValues&) { return std::array<double, 3>{kCenter, kCenter, kCenter}; };
            spec.length = std::move(length);
            return spec;
        };
        // El ancho de cara va por el cono: se dibuja lo que sube en vertical.
        ArrowSpec face{0, 'z', false};
        face.length = [](const GeneratorValues& v) { return v.at("ancho_cara") * std::cos(std::atan2(v.at("dientes_1"), v.at("dientes_2"))); };
        return {{"modulo", centeredArrow(0, pitch("dientes_1"))}, {"dientes_1", centeredArrow(0, pitch("dientes_1"))},
                {"dientes_2", onCrown(pitch("dientes_2"))}, {"agujero_1", centeredArrow(0)}, {"agujero_2", onCrown()},
                {"ancho_cara", face}};
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const std::string modulo = p("modulo"), z1 = p("dientes_1"), z2 = p("dientes_2"), presion = p("presion"),
                          cara = p("ancho_cara"), bore1 = p("agujero_1"), bore2 = p("agujero_2"), giro1 = p("giro_1"),
                          giro2 = p("giro_2");

        Recipe recipe;
        recipe.component = "Engranajes cónicos";
        recipe.bodyNames = {"Piñón", "Corona"};
        const auto param = [&](const std::string& name, const std::string& expression) {
            recipe.parameters.push_back({name, expression});
        };
        param(modulo, number(values.at("modulo")));
        param(z1, number(values.at("dientes_1")));
        param(z2, number(values.at("dientes_2")));
        param(presion, number(values.at("presion")));
        param(cara, number(values.at("ancho_cara")));
        param(bore1, number(values.at("agujero_1")));
        param(bore2, number(values.at("agujero_2")));
        addTuningParameters(recipe, values, suffix, false);
        // Las ruedas se tocan en el origen. El piñón (plano XY, eje a x = su radio primitivo) tiene un diente apuntando a ese
        // punto (180°); la corona (plano YZ, eje a z = su radio primitivo) tiene un hueco apuntando a él (270°).
        const std::string step1 = "360/" + z1, step2 = "360/" + z2;
        param(giro1, "180 - floor(180/(" + step1 + "))*(" + step1 + ")");
        const std::string start2 = "(270 - 180/" + z2 + ")";
        param(giro2, start2 + " - floor(" + start2 + "/(" + step2 + "))*(" + step2 + ")");

        const auto wheel = [&](const char* name, OriginPlane plane, const std::string& teeth, const std::string& mate,
                               const std::string& bore, const std::string& x, const std::string& y, const std::string& turn) {
            auto f = std::make_unique<GearFeature>();
            f->setName(name);
            f->base.origin = plane;
            f->spec.type = geometry::GearType::Bevel;
            f->expressions = {{"module", modulo}, {"teeth", teeth}, {"mateTeeth", mate}, {"pressureAngle", presion},
                              {"thickness", cara}, {"bore", bore}, {"x", x}, {"y", y}, {"rotation", turn}};
            f->expressions["backlash"] = "adelgazar" + suffix;
            return f;
        };
        recipe.features.push_back(wheel("Engranaje: piñón cónico", OriginPlane::XY, z1, z2, bore1, modulo + "*" + z1 + "/2", "0", giro1));
        recipe.features.push_back(wheel("Engranaje: corona cónica", OriginPlane::YZ, z2, z1, bore2, "0", modulo + "*" + z2 + "/2", giro2));
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

std::unique_ptr<Feature> instantiate(const Feature& feature, FeatureId recipeSketch, const std::vector<FeatureId>& ids)
{
    std::unique_ptr<Feature> copy = feature.clone();
    // «@axis:#k» → «@axis:f<id de la operación k>» (el eje del cuerpo que crea).
    const auto resolveAxis = [&](geometry::ElementRef& ref) {
        constexpr std::string_view prefix = "@axis:#";
        if (ref.name.rfind(prefix, 0) == 0) {
            const std::size_t k = std::stoul(ref.name.substr(prefix.size()));
            if (k < ids.size()) {
                ref.name = "@axis:f" + std::to_string(ids[k]);
            }
        }
    };
    if (auto* joint = dynamic_cast<JointFeature*>(copy.get())) {
        resolveAxis(joint->fixedFace);
        resolveAxis(joint->movingFace);
        // «#k» en los cuerpos que van con la pieza móvil → la clave del cuerpo que crea la operación k.
        for (std::string& key : joint->alsoMove) {
            if (!key.empty() && key.front() == '#') {
                const std::size_t k = std::stoul(key.substr(1));
                if (k < ids.size()) {
                    key = "f" + std::to_string(ids[k]);
                }
            }
        }
    }
    if (auto* link = dynamic_cast<MotionLinkFeature*>(copy.get())) {
        for (MotionLinkFeature::Member& member : link->members) {
            if (member.joint >= kRecipeFeatureBase && member.joint - kRecipeFeatureBase < ids.size()) {
                member.joint = ids[std::size_t(member.joint - kRecipeFeatureBase)];
            }
        }
    }
    if (auto* extrude = dynamic_cast<ExtrudeFeature*>(copy.get())) {
        for (ProfileRef& profile : extrude->profiles) {
            if (profile.sketch == kRecipeSketch) {
                profile.sketch = recipeSketch;
            }
        }
    }
    return copy;
}

// --- Bote con tapa --------------------------------------------------------------------------
// Un bote redondo con tapa de rosca o de bayoneta, o una caja rectangular con tapa deslizante. La tapa sale aparte,
// al lado, y se imprime con la cara de arriba sobre la cama. Una sola holgura de ajuste manda en todos los cierres.
class JarWithLid final : public Generator {
public:
    std::string category() const override { return "Cajas"; }
    std::string id() const override { return "bote-con-tapa"; }
    std::string title() const override { return "Bote con tapa"; }
    std::string icon() const override { return "box-solid"; }
    std::string description() const override
    {
        return "Bote redondo con tapa de rosca o de bayoneta, o caja rectangular con tapa deslizante. La holgura de "
               "ajuste es la misma para todos y se puede cambiar según tu impresora.";
    }

    std::vector<GeneratorField> fields() const override
    {
        using K = GeneratorField::Kind;
        const auto length = [](std::string key, std::string label, double value, double min, double max, std::string hint = {},
                               std::string showKey = {}, int showIndex = 0) {
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
        const auto choice = [](std::string key, std::string label, std::vector<std::string> choices, std::string hint,
                               std::string showKey = {}, int showIndex = 0) {
            GeneratorField f;
            f.key = std::move(key);
            f.label = std::move(label);
            f.kind = K::Choice;
            f.choices = std::move(choices);
            f.hint = std::move(hint);
            f.showKey = std::move(showKey);
            f.showIndex = showIndex;
            return f;
        };
        return grouped({choice("forma", "Forma", {"Redonda", "Rectangular"}, "La redonda cierra con rosca o bayoneta; la rectangular, con una tapa que se desliza"),
                        length("diametro", "Diámetro (exterior)", 60.0, 20.0, 500.0, {}, "forma", 0),
                        length("ancho", "Ancho (exterior)", 80.0, 20.0, 500.0, {}, "forma", 1),
                        length("fondo", "Fondo (exterior)", 50.0, 20.0, 500.0, "Hacia donde se desliza la tapa", "forma", 1),
                        length("alto", "Alto total", 50.0, 15.0, 500.0, "Con la rosca o el cuello incluidos"),
                        length("pared", "Pared", 2.4, 1.2, 20.0, "Un múltiplo del ancho de la boquilla"),
                        length("base", "Base", 2.0, 0.8, 20.0, "Grosor del fondo"),
                        length("interior", "Redondeo del fondo por dentro", 2.0, 0.0, 10.0,
                               "Redondea por dentro la unión de la pared con el fondo: la refuerza (0 = esquina viva)", "forma", 0),
                        length("esquina", "Radio de esquinas", 3.0, 0.0, 30.0, {}, "forma", 1),
                        length("tapa", "Grosor de la tapa", 2.4, 0.8, 20.0),
                        length("holgura", "Holgura de ajuste", 0.3, 0.0, 2.0,
                               "Aire entre las piezas que encajan, por cada lado: sube si queda apretado, baja si baila"),
                        choice("cierre", "Cierre", {"Rosca", "Bayoneta"}, "Rosca: se enrosca varias vueltas. Bayoneta: entra, gira hasta el tope y descansa en una grada; para abrir hay que levantarla", "forma", 0),
                        length("paso", "Paso de la rosca", 3.0, 1.0, 8.0, "Avance por vuelta: más grande imprime mejor", "cierre", 0),
                        length("rosca_alto", "Alto de la rosca", 10.0, 4.0, 40.0, "Lo que sube el cuello roscado", "cierre", 0),
                        length("pin_diametro", "Diámetro de los pines", 4.0, 2.0, 12.0, "Cuatro pines en el cuello", "cierre", 1),
                        length("pin_sale", "Lo que salen los pines", 2.5, 1.0, 8.0, {}, "cierre", 1),
                        length("pin_baja", "Altura de los pines", 6.0, 3.0, 40.0, "Desde el borde del bote hacia abajo", "cierre", 1),
                        length("cuello", "Alto del cuello", 12.0, 6.0, 40.0, "Lo que entra la tapa", "cierre", 1),
                        length("grada", "Altura de la grada", 1.5, 0.6, 4.0,
                               "Cuánto hay que levantar la tapa para sacar los pines de la grada donde descansan al cerrar", "cierre", 1),
                        length("ranura", "Hondo de la guía", 1.2, 0.6, 6.0, "Cuánto entra en la pared; deja al menos 0,8 mm", "forma", 1),
                        length("labio", "Labio sobre la tapa", 1.6, 0.8, 6.0, "El borde de pared que sujeta la tapa por arriba", "forma", 1)},
                       {{"forma", "Medidas"}, {"tapa", "Tapa y ajuste"}, {"cierre", "Cierre"}});
    }

    std::map<std::string, ArrowSpec> arrows() const override
    {
        using Point = std::array<double, 3>;
        const auto round = [](const GeneratorValues& v) { return v.at("forma") < 0.5; };
        const auto bayonet = [round](const GeneratorValues& v) { return round(v) && v.at("cierre") >= 0.5; };
        const auto halfX = [round](const GeneratorValues& v) { return (round(v) ? v.at("diametro") : v.at("ancho")) / 2.0; };
        const auto halfY = [round](const GeneratorValues& v) { return (round(v) ? v.at("diametro") : v.at("fondo")) / 2.0; };
        // Donde acaba el cuerpo (en el de rosca, debajo del cuello).
        const auto bodyTop = [round, bayonet](const GeneratorValues& v) {
            return round(v) && !bayonet(v) ? v.at("alto") - v.at("rosca_alto") : v.at("alto");
        };
        // El centro de la tapa y su radio por fuera.
        const auto lidX = [round](const GeneratorValues& v) {
            return round(v) ? v.at("diametro") + v.at("pared") + 12.0 : v.at("ancho") + 10.0;
        };
        const auto lidR = [bayonet](const GeneratorValues& v) {
            return v.at("diametro") / 2.0 + v.at("pared") + (bayonet(v) ? answer(v, "pin_sale") : 0.0);
        };
        const auto neckR = [](const GeneratorValues& v) { return v.at("diametro") / 2.0 - 0.3; };
        const auto pinZ = [](const GeneratorValues& v) { return v.at("alto") - v.at("pin_baja"); };
        // La guía de la tapa deslizante: donde empieza en altura.
        const auto slotZ = [](const GeneratorValues& v) { return v.at("alto") - v.at("labio") - v.at("tapa") - 2.0 * v.at("holgura"); };
        return {
            {"diametro", worldArrow('x', [bodyTop](const GeneratorValues& v) { return Point{0.0, 0.0, bodyTop(v)}; }, {}, true)},
            {"ancho", worldArrow('x', [halfX, halfY](const GeneratorValues& v) { return Point{-halfX(v), -halfY(v), v.at("alto")}; })},
            {"fondo", worldArrow('y', [halfX, halfY](const GeneratorValues& v) { return Point{-halfX(v), -halfY(v), v.at("alto")}; })},
            {"alto", worldArrow('z', [halfX, halfY, round](const GeneratorValues& v) { return Point{-halfX(v), round(v) ? 0.0 : -halfY(v), 0.0}; })},
            {"pared", worldArrow('x', [halfX, bodyTop](const GeneratorValues& v) { return Point{halfX(v), 0.0, bodyTop(v)}; }, backwards("pared"))},
            {"base", worldArrow('z', [halfY](const GeneratorValues& v) { return Point{0.0, -halfY(v), 0.0}; })},
            {"interior", worldArrow('z', [halfX](const GeneratorValues& v) { return Point{halfX(v) - v.at("pared"), 0.0, v.at("base")}; })},
            {"esquina", worldArrow('x', [halfX, halfY](const GeneratorValues& v) { return Point{-halfX(v), -halfY(v), v.at("alto")}; })},
            {"tapa", worldArrow('z', [lidX, lidR, round](const GeneratorValues& v) { return Point{lidX(v) + (round(v) ? lidR(v) : 0.0), 0.0, 0.0}; })},
            {"holgura", worldArrow('x', [round, bayonet, lidX, neckR, slotZ](const GeneratorValues& v) {
                 if (!round(v)) {
                     return Point{v.at("ancho") / 2.0 - v.at("pared") + v.at("ranura") - v.at("holgura"), 0.0, slotZ(v)};
                 }
                 return bayonet(v) ? Point{lidX(v) + v.at("diametro") / 2.0, 0.0, v.at("tapa") + v.at("cuello")}
                                   : Point{lidX(v) + neckR(v), 0.0, v.at("tapa") + v.at("rosca_alto") + 1.0};
             })},
            {"paso", worldArrow('z', [neckR, bodyTop](const GeneratorValues& v) { return Point{neckR(v), 0.0, bodyTop(v)}; })},
            {"rosca_alto", worldArrow('z', [neckR, bodyTop](const GeneratorValues& v) { return Point{-neckR(v), 0.0, bodyTop(v)}; })},
            {"pin_diametro", worldArrow('z', [pinZ](const GeneratorValues& v) { return Point{v.at("diametro") / 2.0 + v.at("pin_sale"), 0.0, pinZ(v)}; }, {}, true)},
            {"pin_sale", worldArrow('x', [pinZ](const GeneratorValues& v) { return Point{v.at("diametro") / 2.0, 0.0, pinZ(v)}; })},
            {"pin_baja", worldArrow('z', [](const GeneratorValues& v) { return Point{v.at("diametro") / 2.0, 0.0, v.at("alto")}; }, backwards("pin_baja"))},
            {"cuello", worldArrow('z', [lidX, lidR](const GeneratorValues& v) { return Point{lidX(v) + lidR(v), 0.0, v.at("tapa")}; })},
            {"grada", worldArrow('z', [lidX](const GeneratorValues& v) {
                 return Point{lidX(v) + v.at("diametro") / 2.0 + v.at("pin_sale") + v.at("holgura"), 0.0,
                              v.at("tapa") + v.at("pin_baja") - v.at("pin_diametro") / 2.0 - v.at("holgura")};
             })},
            {"ranura", worldArrow('x', [slotZ](const GeneratorValues& v) { return Point{v.at("ancho") / 2.0 - v.at("pared"), 0.0, slotZ(v)}; })},
            {"labio", worldArrow('z', [](const GeneratorValues& v) { return Point{v.at("ancho") / 2.0 - v.at("pared"), 0.0, v.at("alto")}; }, backwards("labio"))}};
    }

    // Las medidas con las que el bote no puede funcionar, dichas por su nombre (si no, saldría un sólido válido que no cierra,
    // o un error del kernel que no dice qué cambiar).
    static void checkSizes(const GeneratorValues& v, bool round, bool bayonet)
    {
        const auto fail = [](const char* text) { throw std::runtime_error(text); };
        const double wall = v.at("pared"), base = v.at("base"), gap = v.at("holgura"), height = v.at("alto");
        if (!round) {
            if (std::min(v.at("ancho"), v.at("fondo")) - 2.0 * wall < 5.0) {
                fail("La pared es demasiado gruesa para esas medidas: no queda hueco");
            }
            if (v.at("ranura") > wall - 0.4) {
                fail("El hondo de la guía casi atraviesa la pared: usa menos hondo de guía o más pared");
            }
            if (v.at("ranura") < gap + 0.3) {
                fail("El hondo de la guía es menor que la holgura: la tapa no se sujetaría; usa más hondo de guía o menos holgura");
            }
            if (height - base - v.at("labio") - v.at("tapa") - 2.0 * gap < 3.0) {
                fail("El bote es demasiado bajo para esa tapa y ese labio");
            }
            return;
        }
        const double diameter = v.at("diametro");
        if (diameter - 2.0 * wall < 5.0) {
            fail("La pared es demasiado gruesa para ese diámetro: no queda hueco");
        }
        if (!bayonet) {
            const double depth = 0.6134 * v.at("paso");
            if (diameter - 0.6 - 2.0 * (depth + wall) < 5.0) {
                fail("El cuello sale macizo: usa menos pared, menos paso de rosca o más diámetro");
            }
            if (height - v.at("rosca_alto") - base < 3.0) {
                fail("El bote es demasiado bajo para esa rosca: usa más alto o menos alto de rosca");
            }
            if (v.at("rosca_alto") < v.at("paso")) {
                fail("La rosca debe medir al menos un paso de alto");
            }
            return;
        }
        const double pin = v.at("pin_diametro"), out = v.at("pin_sale"), drop = v.at("pin_baja");
        const double groove = pin + 2.0 * gap;
        if (drop < groove / 2.0 + 0.3) {
            fail("Los pines quedan pegados al borde: usa más «Altura de los pines» o pines más finos");
        }
        if (height - base < drop + pin / 2.0 + 1.0) {
            fail("El bote es demasiado bajo para la altura de esos pines");
        }
        // Encima de la guía tiene que quedar material en la tapa: es lo que sujeta los pines.
        if (v.at("cuello") < drop + groove / 2.0 + gap + answer(v, "grada", 1.5) + 0.8) {
            fail("El cuello de la tapa es demasiado corto: los pines no quedarían sujetos; usa más «Alto del cuello» o menos «Altura de los pines»");
        }
        if (out < gap + 0.5) {
            fail("Los pines salen menos que la holgura: la tapa no se sujetaría; usa «Lo que salen los pines» mayor");
        }
        // El tope y la grada ocupan un tramo de la guía al lado de cada entrada: en un bote muy estrecho no caben.
        const double end = groove / 2.0 + 0.8 * pin + groove + 0.2 * pin;
        const double neck = diameter / 2.0 + gap, guide = diameter / 2.0 + out + gap;
        if (end * end + 0.81 * neck * neck > guide * guide) {
            fail("El bote es demasiado estrecho para esos pines: usa pines más finos o más diámetro");
        }
    }

    Recipe make(const GeneratorValues& values, const std::string& suffix) const override
    {
        const bool round = values.at("forma") < 0.5;
        const bool bayonet = round && values.at("cierre") >= 0.5;
        checkSizes(values, round, bayonet);
        const auto p = [&](const char* name) { return std::string(name) + suffix; };
        const auto v = [&](const char* key) { return values.at(key); };
        Recipe recipe;
        recipe.component = "Bote con tapa";
        recipe.bodyNames = {"Bote", "Tapa"};
        const auto param = [&](const std::string& name, double value) { recipe.parameters.push_back({name, number(value)}); };
        const std::string alto = p("alto"), pared = p("pared"), base = p("base"), tapa = p("tapa"), holgura = p("holgura"),
                          diametro = p("diametro"), ancho = p("ancho"), fondo = p("fondo"), esquina = p("esquina"),
                          interior = p("interior"), paso = p("paso"), roscaAlto = p("rosca_alto"), pinD = p("pin_diametro"),
                          pinSale = p("pin_sale"), pinBaja = p("pin_baja"), cuello = p("cuello"), grada = p("grada"), ranura = p("ranura"),
                          labio = p("labio");
        param(alto, v("alto"));
        param(pared, v("pared"));
        param(base, v("base"));
        param(tapa, v("tapa"));
        param(holgura, v("holgura"));
        const auto box = [&](const std::string& name, Operation op, const std::string& w, const std::string& d, const std::string& z,
                             const std::string& h, const std::string& x, const std::string& y, const std::string& radius) {
            auto f = std::make_unique<BoxFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->centered = true;
            f->operation = op;
            f->expressions = {{"width", w}, {"depth", d}, {"z", z}, {"height", h}};
            if (!x.empty()) {
                f->expressions["x"] = x;
            }
            if (!y.empty()) {
                f->expressions["y"] = y;
            }
            if (!radius.empty()) {
                f->expressions["radius"] = radius;
            }
            return f;
        };
        const auto cylinder = [&](const std::string& name, Operation op, const std::string& diameter, const std::string& z,
                                  const std::string& h, const std::string& x, OriginPlane plane = OriginPlane::XY,
                                  const std::string& y = {}) {
            auto f = std::make_unique<CylinderFeature>();
            f->setName(name);
            f->base.origin = plane;
            f->operation = op;
            f->expressions = {{"diameter", diameter}, {"z", z}, {"height", h}};
            if (!x.empty()) {
                f->expressions["x"] = x;
            }
            if (!y.empty()) {
                f->expressions["y"] = y;
            }
            return f;
        };
        const auto thread = [&](const std::string& name, Operation op, const std::string& diameter, const std::string& z,
                                const std::string& length, const std::string& x) {
            auto f = std::make_unique<ThreadFeature>();
            f->setName(name);
            f->base.origin = OriginPlane::XY;
            f->operation = op;
            f->expressions = {{"diameter", diameter}, {"pitch", paso}, {"length", length}, {"z", z}, {"x", x}};
            if (op == Operation::Cut) {
                f->expressions["clearance"] = holgura;
            } else {
                f->expressions["entryChamfer"] = "0.6*" + paso;
            }
            return f;
        };

        if (round) {
            param(diametro, v("diametro"));
            const std::string inD = diametro + " - 2*" + pared;
            const std::string beside = diametro + " + " + pared + " + 12";
            if (!bayonet) {
                // Rosca: primero el vaso liso con su hueco (cilindros, al instante) y solo después el cuello roscado, así las
                // operaciones con la rosca (las caras) son dos. El hueco acaba en un hombro a 45° hasta la boca, que es más
                // estrecha que el cuerpo: sin él, el cuello colgaría de un anillo de medio milímetro. El cuello es un poco más
                // delgado que el cuerpo (crestas y pared en la misma superficie dan booleanas lentas y frágiles).
                param(paso, v("paso"));
                param(roscaAlto, v("rosca_alto"));
                const std::string depth = "0.6134*" + paso;
                const std::string bodyH = alto + " - " + roscaAlto;
                const std::string neckD = diametro + " - 0.6";
                const std::string mouth = neckD + " - 2*(" + depth + " + " + pared + ")";
                const std::string hollowTop = bodyH + " - 0.4*" + pared;
                const std::string hollowH = "(" + hollowTop + " - " + base + ")";
                recipe.features.push_back(cylinder("Bote: cuerpo", Operation::NewBody, diametro, "0", bodyH, ""));
                auto hollow = cylinder("Bote: hueco", Operation::Cut, inD, base, hollowH, "");
                hollow->expressions["topChamfer"] = "min((" + inD + " - (" + mouth + "))/2; 0.45*" + hollowH + ")";
                if (v("interior") > 0.0) {
                    param(interior, v("interior"));
                    hollow->expressions["baseRound"] = "min(" + interior + "; min((" + inD + ")/2 - 0.1; 0.45*" + hollowH + "))";
                }
                recipe.features.push_back(std::move(hollow));
                recipe.features.push_back(thread("Bote: rosca", Operation::Join, neckD, alto, "-(" + roscaAlto + " + 0.2*" + pared + ")", "0"));
                recipe.features.push_back(cylinder("Bote: boca", Operation::Cut, mouth, hollowTop + " - 0.2",
                                                   roscaAlto + " + 0.4*" + pared + " + 1.2", ""));
                // La tapa: un vaso con la rosca por dentro (la misma rosca, más la holgura).
                recipe.features.push_back(cylinder("Tapa: cuerpo", Operation::NewBody, diametro + " + 2*" + pared, "0",
                                                   tapa + " + " + roscaAlto + " + 1", beside));
                recipe.features.push_back(thread("Tapa: rosca", Operation::Cut, neckD, tapa, roscaAlto + " + 2", beside));
            } else {
                // Bayoneta: cuatro pines en el cuello y, en la tapa, una guía redonda con cuatro entradas.
                param(pinD, v("pin_diametro"));
                param(pinSale, v("pin_sale"));
                param(pinBaja, v("pin_baja"));
                param(cuello, v("cuello"));
                param(grada, v("grada"));
                const std::string span = diametro + " + 2*" + pinSale;
                const std::string pinZ = alto + " - " + pinBaja;
                recipe.features.push_back(cylinder("Bote: cuerpo", Operation::NewBody, diametro, "0", alto, ""));
                // Dos barras que cruzan el bote por la altura de los pines (en X y en Y); el hueco las deja en cuatro pines.
                // En los planos YZ y XZ la «y» del plano es la altura; centrados sobre el plano (simétricos), de lado a lado.
                for (const auto& [name, plane] : {std::pair<const char*, OriginPlane>{"Bote: pines en X", OriginPlane::YZ},
                                                  std::pair<const char*, OriginPlane>{"Bote: pines en Y", OriginPlane::XZ}}) {
                    auto pins = cylinder(name, Operation::Join, pinD, "0", span, "0", plane, "(" + pinZ + ")");
                    pins->symmetric = true;
                    recipe.features.push_back(std::move(pins));
                }
                auto hollow = cylinder("Bote: hueco", Operation::Cut, inD, base, alto + " - " + base + " + 1", "");
                if (v("interior") > 0.0) {
                    param(interior, v("interior"));
                    hollow->expressions["baseRound"] = "min(" + interior + "; min((" + inD + ")/2 - 0.1; " + alto + " - " + base + " - 0.1))";
                }
                recipe.features.push_back(std::move(hollow));
                // La tapa: un vaso con la guía de los pines. Cada pin entra por una de las cuatro entradas, se levanta la
                // tapa para girar por la guía hasta el tope (no puede seguir hasta la entrada siguiente) y al soltarla los
                // pines bajan a la grada (un escalón más hondo que la guía) donde descansan. Para abrir hay que volver a
                // levantar la tapa por encima del escalón antes de girar de vuelta.
                const std::string capD = span + " + 2*" + pared;
                const std::string capH = tapa + " + " + cuello;
                const std::string groove = pinD + " + 2*" + holgura;
                const std::string lift = "(" + holgura + " + " + grada + ")";  // lo que sube la guía sobre las entradas y la grada
                const std::string grooveZ = tapa + " + " + pinBaja + " - (" + groove + ")/2";
                const std::string neckR = "((" + diametro + " + 2*" + holgura + ")/2)";
                const std::string guideR = "((" + span + " + 2*" + holgura + ")/2)";
                const std::string outerR = "min(" + guideR + " + 0.3; (" + capD + ")/2 - 0.1)";
                const std::string g = "(" + groove + ")";
                const std::string stopW = "(0.8*" + pinD + ")";
                // El tope llega un poco más allá de la guía (para unirse a la pared), sin que su esquina asome por fuera de la tapa.
                const std::string stopEnd = "(" + g + "/2 + " + stopW + ")";
                const std::string stopR = "min(" + outerR + "; sqrt(((" + capD + ")/2)*((" + capD + ")/2) - " + stopEnd + "*" + stopEnd + ") - 0.05)";
                const std::string midR = "((" + neckR + " + " + stopR + ")/2)";
                const std::string lenR = "(" + stopR + " - " + neckR + ")";
                // El hueco de cierre empieza más adentro: el pin, ya girado, entra en él inclinado respecto del radio.
                const std::string pocketR0 = "(0.9*" + neckR + ")";
                const std::string pocketMid = "((" + pocketR0 + " + " + outerR + ")/2)";
                const std::string pocketLen = "(" + outerR + " - " + pocketR0 + ")";
                recipe.features.push_back(cylinder("Tapa: cuerpo", Operation::NewBody, capD, "0", capH, beside));
                recipe.features.push_back(cylinder("Tapa: hueco", Operation::Cut, diametro + " + 2*" + holgura, tapa, cuello + " + 1", beside));
                // La guía va un peldaño más arriba que las entradas y que el hueco de cierre: los pines solo la recorren
                // levantando la tapa un poco.
                const std::string pathZ = grooveZ + " + " + lift;
                recipe.features.push_back(cylinder("Tapa: guía de los pines", Operation::Cut, span + " + 2*" + holgura, pathZ, groove, beside));
                // Los topes: un muro junto a cada entrada, del lado hacia donde llega el pin al girar.
                // (cx, cy) y (w, d) se giran 90° para cada una de las cuatro entradas.
                struct Piece {
                    const char* name;
                    Operation op;
                    std::string cx, w;  // a lo largo del giro
                    std::string cy, d;  // a lo largo del radio
                    std::string z, h;
                };
                const std::vector<Piece> pieces = {
                    {"tope", Operation::Join, "(" + g + "/2 + " + stopW + "/2)", stopW, midR, lenR, pathZ, groove},
                    {"grada de cierre", Operation::Cut, "(" + g + "/2 + " + stopW + " + (" + g + " + 0.2*" + pinD + ")/2)",
                     "(" + g + " + 0.2*" + pinD + ")", pocketMid, pocketLen, grooveZ, groove + " + " + lift}};
                std::vector<std::unique_ptr<Feature>> later;
                for (const Piece& piece : pieces) {
                    for (int k = 0; k < 4; ++k) {
                        std::string cx = piece.cx, cy = piece.cy, w = piece.w, d = piece.d;
                        for (int turn = 0; turn < k; ++turn) {
                            const std::string nx = "-(" + cy + ")", ny = cx;
                            cx = nx;
                            cy = ny;
                            std::swap(w, d);
                        }
                        const std::string name = std::string("Tapa: ") + piece.name + " " + std::to_string(k + 1);
                        auto part = box(name, piece.op, w, d, piece.z, piece.h, beside + " + (" + cx + ")", cy, "");
                        if (piece.op == Operation::Join) {
                            recipe.features.push_back(std::move(part));
                        } else {
                            later.push_back(std::move(part));
                        }
                    }
                }
                // Las gradas y las entradas bajan hasta el suelo hondo, y se cortan después de poner los topes.
                for (auto& part : later) {
                    recipe.features.push_back(std::move(part));
                }
                // Las gradas son rectas y sus esquinas llegan más afuera que la guía, que es redonda (con algunos pines, hasta
                // atravesar la pared): la pared vuelve a ponerse entera por fuera, así la grada acaba justo en el fondo del carril.
                auto wall = cylinder("Tapa: pared de la guía", Operation::Join, capD, grooveZ, groove + " + " + lift, beside);
                wall->expressions["innerDiameter"] = span + " + 2*" + holgura;
                recipe.features.push_back(std::move(wall));
                recipe.features.push_back(box("Tapa: entradas en X", Operation::Cut, span + " + 2*" + pared + " + 2", groove, grooveZ,
                                              capH + " - (" + grooveZ + ") + 1", beside, "", ""));
                recipe.features.push_back(box("Tapa: entradas en Y", Operation::Cut, groove, span + " + 2*" + pared + " + 2", grooveZ,
                                              capH + " - (" + grooveZ + ") + 1", beside, "", ""));
            }
        } else {
            // Tapa deslizante: guías en las paredes largas y una ranura en la pared de delante para meter la tapa.
            param(ancho, v("ancho"));
            param(fondo, v("fondo"));
            param(esquina, v("esquina"));
            param(ranura, v("ranura"));
            param(labio, v("labio"));
            const std::string inW = ancho + " - 2*" + pared, inD = fondo + " - 2*" + pared;
            const std::string slotH = tapa + " + 2*" + holgura;                     // alto de la guía
            const std::string slotZ = alto + " - " + labio + " - (" + slotH + ")";  // dónde empieza
            const std::string beside = ancho + " + 10";
            recipe.features.push_back(box("Bote: cuerpo", Operation::NewBody, ancho, fondo, "0", alto, "", "", esquina));
            recipe.features.push_back(box("Bote: hueco", Operation::Cut, inW, inD, base, alto, "", "", ""));
            // La guía: una ranura a lo ancho (más ancha que el hueco), del frente hasta la pared de atrás, que queda cerrada.
            recipe.features.push_back(box("Bote: guías de la tapa", Operation::Cut, inW + " + 2*" + ranura,
                                          fondo + " - " + pared + " + 1", slotZ, slotH, "", "-(1 + " + pared + ")/2", ""));
            // La tapa: una placa que entra por delante, apoyada en las guías (más estrecha por la holgura).
            const std::string lidW = inW + " + 2*(" + ranura + " - " + holgura + ")";
            const std::string lidD = fondo + " - " + pared + " - " + holgura;
            recipe.features.push_back(box("Tapa: placa", Operation::NewBody, lidW, lidD, "0", tapa, beside,
                                          "-(" + pared + " + " + holgura + ")/2", ""));
        }
        return recipe;
    }
};

const std::vector<const Generator*>& generators()
{
    static const Lightbox lightbox;
    static const BoxWithLid boxWithLid;
    static const JarWithLid jarWithLid;
    static const Sign sign;
    static const GearPair gearPair;
    static const InternalGear internalGear;
    static const RackAndPinion rackAndPinion;
    static const WormAndWheel wormAndWheel;
    static const BevelPair bevelPair;
    static const PlanetaryGear planetaryGear;
    static const std::vector<const Generator*> all = {&lightbox, &boxWithLid, &jarWithLid, &sign, &gearPair, &internalGear,
                                                      &rackAndPinion, &wormAndWheel, &bevelPair, &planetaryGear};
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
    return freeSuffix(doc.parameters(), generator, values);
}

std::string freeSuffix(const std::vector<Parameter>& used, const Generator& generator, const GeneratorValues& values)
{
    for (int n = 1; n < 1000; ++n) {
        const std::string suffix = n == 1 ? std::string() : "_" + std::to_string(n);
        const Recipe recipe = generator.make(values, suffix);
        const bool clash = std::ranges::any_of(recipe.parameters, [&](const Parameter& p) {
            return std::ranges::any_of(used, [&](const Parameter& old) { return old.name == p.name; });
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

std::string numberedComponent(const std::map<std::string, BodyInfo>& info, const std::string& base)
{
    if (base.empty()) {
        return {};
    }
    for (int n = 1; n < 10000; ++n) {
        const std::string name = base + " " + std::to_string(n);
        const bool used = std::ranges::any_of(info, [&](const auto& entry) { return entry.second.component == name; });
        if (!used) {
            return name;
        }
    }
    return base;
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
    std::vector<FeatureId> recipeIds;
    for (const auto& feature : recipe.features) {
        const FeatureId id = doc.insertFeature(instantiate(*feature, recipeSketch, recipeIds), doc.marker());
        recipeIds.push_back(id);
        if (feature->kind() == FeatureKind::Sketch) {
            recipeSketch = id;
        }
    }
    std::map<std::string, BodyInfo> info = doc.bodyInfo();
    const std::string component = numberedComponent(info, recipe.component);
    std::size_t next = 0;
    for (const Body& body : doc.result().bodies) {
        if (std::ranges::find(before, body.key) != before.end()) {
            continue;
        }
        info[body.key] = {next < recipe.bodyNames.size() ? recipe.bodyNames[next] : std::string(), component, std::string(),
                          std::ranges::find(recipe.fixedBodies, int(next)) != recipe.fixedBodies.end()};
        ++next;
    }
    doc.setBodyInfo(info);
}

} // namespace nuf::document
