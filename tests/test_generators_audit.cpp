// Revisión de todos los generadores con todas sus opciones: ninguna operación puede quedar con error NI con advertencia
// (un «No corta ningún cuerpo» es una pieza mal construida aunque el programa siga), y los cuerpos que salen tienen que
// ser sólidos válidos. Las pruebas ocultas «[.audit]» y «[.audit3]» además miden tiempos y prueban medidas distintas.

#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>
#include <nuf/document/Generators.hpp>
#include <nuf/geometry/Modeling.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

using namespace nuf;

namespace {

struct Case {
    const document::Generator* generator = nullptr;
    std::string label;
    document::GeneratorValues values;
};

// Un «texto» de mentira: cada letra es un cuadrado con un hueco (no hay fuentes en las pruebas).
std::vector<sketch::Geometry> squareLetters(const std::string& text, double height, const std::string&)
{
    std::vector<sketch::Geometry> out;
    const double w = height * 0.7;
    const auto rectangle = [&](double x0, double y0, double x1, double y1) {
        out.push_back(sketch::Line{{x0, y0}, {x1, y0}});
        out.push_back(sketch::Line{{x1, y0}, {x1, y1}});
        out.push_back(sketch::Line{{x1, y1}, {x0, y1}});
        out.push_back(sketch::Line{{x0, y1}, {x0, y0}});
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const double x = double(i) * 1.2 * w;
        rectangle(x, 0.0, x + w, height);
        rectangle(x + 0.3 * w, 0.3 * w, x + 0.7 * w, height - 0.3 * w);
    }
    return out;
}

// ¿Se muestra la pregunta con esas respuestas? (en cadena: la que la muestra también tiene que verse)
bool shown(const std::vector<document::GeneratorField>& fields, const document::GeneratorField& field,
           const document::GeneratorValues& values)
{
    if (field.showKey.empty()) {
        return true;
    }
    for (const document::GeneratorField& other : fields) {
        if (other.key == field.showKey) {
            return int(values.at(field.showKey)) == field.showIndex && shown(fields, other, values);
        }
    }
    return true;
}

// Los valores de fábrica y, por cada pregunta de opciones, cada opción; las opciones que abren otras preguntas («forma»,
// «cierre»…) se combinan con todas las de las que abren.
std::vector<Case> choiceCases(const document::Generator& generator)
{
    const std::vector<document::GeneratorField> fields = generator.fields();
    std::vector<Case> bases{{&generator, generator.id(), generator.defaults()}};
    std::vector<Case> cases = bases;
    for (const document::GeneratorField& field : fields) {
        if (field.kind != document::GeneratorField::Kind::Choice) {
            continue;
        }
        const bool opens = std::ranges::any_of(fields, [&](const document::GeneratorField& f) { return f.showKey == field.key; });
        std::vector<Case> grown;
        for (const Case& base : bases) {
            if (!shown(fields, field, base.values)) {
                continue;
            }
            for (std::size_t i = 0; i < field.choices.size(); ++i) {
                if (base.values.at(field.key) == double(i)) {
                    continue;
                }
                Case next = base;
                next.values[field.key] = double(i);
                // La espina de pescado necesita hélice (la interfaz la pone sola al elegirla).
                if (field.key == "espina" && i == 1) {
                    next.values["helice"] = 20.0;
                }
                next.label += " · " + field.key + "=" + field.choices[i];
                cases.push_back(next);
                grown.push_back(std::move(next));
            }
        }
        if (opens && bases.size() < 12) {
            bases.insert(bases.end(), grown.begin(), grown.end());
        }
    }
    return cases;
}

// Cada medida, más pequeña y más grande que la de fábrica (dentro de sus límites). Cada pregunta se prueba en el primer
// caso de opciones en que se ve, y en todos los de las preguntas «forma» y «cierre».
std::vector<Case> sizeCases(const document::Generator& generator)
{
    const std::vector<document::GeneratorField> fields = generator.fields();
    std::vector<Case> cases;
    std::vector<std::string> done;
    for (const Case& base : choiceCases(generator)) {
        std::string family;
        for (const char* key : {"forma", "cierre", "tipo", "modo"}) {
            if (const auto it = base.values.find(key); it != base.values.end()) {
                family += std::string(key) + std::to_string(int(it->second));
            }
        }
        for (const document::GeneratorField& field : fields) {
            using K = document::GeneratorField::Kind;
            if ((field.kind != K::Length && field.kind != K::Count && field.kind != K::Angle) || !shown(fields, field, base.values)) {
                continue;
            }
            if (std::ranges::find(done, family + field.key) != done.end()) {
                continue;
            }
            done.push_back(family + field.key);
            for (const double factor : {0.6, 1.6}) {
                double value = std::clamp(field.value * factor, field.min, field.max);
                if (field.kind == K::Count) {
                    value = std::round(value);
                }
                if (value == field.value) {
                    continue;
                }
                Case next = base;
                next.values[field.key] = value;
                char text[32];
                std::snprintf(text, sizeof text, "%.2f", value);
                next.label += " · " + field.key + "=" + text;
                cases.push_back(std::move(next));
            }
        }
    }
    return cases;
}

// Los problemas de un caso (vacío: bien). Un mensaje claro del propio generador («la bisagra es demasiado larga…») no es
// un problema: es lo que debe hacer con medidas imposibles; se devuelve aparte.
std::vector<std::string> problemsOf(const Case& c, document::Document& doc, std::string* refused = nullptr)
{
    std::vector<std::string> problems;
    try {
        document::applyRecipe(doc, c.generator->make(c.values, ""));
    } catch (const std::exception& e) {
        if (refused != nullptr) {
            *refused = e.what();
        } else {
            problems.push_back(std::string("no se pudo: ") + e.what());
        }
        return problems;
    }
    for (std::size_t i = 0; i < doc.featureCount(); ++i) {
        const auto& status = doc.status(doc.feature(i).id());
        if (status.health != document::Health::Ok) {
            problems.push_back(doc.feature(i).name() + ": " + status.message);
        }
    }
    for (const document::Body& body : doc.result().bodies) {
        if (!body.shape.isValid()) {
            problems.push_back(body.name + ": sólido no válido");
        }
        if (c.generator->id() != "letrero" && body.shape.solidCount() != 1) {
            problems.push_back(body.name + ": " + std::to_string(body.shape.solidCount()) + " trozos sueltos");
        }
        if (body.shape.volume() <= 0.0) {
            problems.push_back(body.name + ": volumen " + std::to_string(body.shape.volume()));
        }
    }
    if (doc.result().bodies.empty()) {
        problems.push_back("no sale ningún cuerpo");
    }
    return problems;
}

double msSince(std::chrono::steady_clock::time_point from)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
}

} // namespace

TEST_CASE("Todos los generadores, con cada opción, salen sin errores ni advertencias y con cuerpos válidos", "[generator]")
{
    document::setTextOutliner(squareLetters);
    for (const document::Generator* generator : document::generators()) {
        for (const Case& c : choiceCases(*generator)) {
            document::Document doc;
            std::string all;
            for (const std::string& problem : problemsOf(c, doc)) {
                all += problem + "; ";
            }
            INFO(c.label);
            CHECK(all == "");
        }
    }
    document::setTextOutliner({});
}

TEST_CASE("Una herramienta metida entera dentro del cuerpo también lo corta o se une a él", "[geometry][document]")
{
    const auto cylinder = [](double diameter, double z, double height, document::Operation operation) {
        auto f = std::make_unique<document::CylinderFeature>();
        f->base.origin = document::OriginPlane::XY;
        f->diameter = diameter;
        f->z = z;
        f->height = height;
        f->operation = operation;
        return f;
    };
    document::Document doc;
    doc.addFeature(cylinder(40.0, 0.0, 30.0, document::Operation::NewBody));
    const auto id = doc.addFeature(cylinder(20.0, 5.0, 20.0, document::Operation::Cut));
    INFO(doc.status(id).message);
    CHECK(doc.status(id).health == document::Health::Ok);
    REQUIRE(doc.result().bodies.size() == 1);
    const double pi = std::numbers::pi;
    CHECK(doc.result().bodies.front().shape.volume() == Catch::Approx(pi * (400.0 * 30.0 - 100.0 * 20.0)).epsilon(1e-6));
    // Unir algo que ya está dentro no crea un cuerpo aparte.
    document::Document joined;
    joined.addFeature(cylinder(40.0, 0.0, 30.0, document::Operation::NewBody));
    joined.addFeature(cylinder(10.0, 5.0, 5.0, document::Operation::Join));
    CHECK(joined.result().bodies.size() == 1);
    // Y lo que está fuera (aunque las cajas se crucen) sigue sin tocarse.
    document::Document apart;
    apart.addFeature(cylinder(40.0, 0.0, 30.0, document::Operation::NewBody));
    auto corner = cylinder(6.0, 0.0, 30.0, document::Operation::Cut);
    corner->x = 18.0;
    corner->y = 18.0;
    const auto miss = apart.addFeature(std::move(corner));
    CHECK(apart.status(miss).health == document::Health::Warning);
}

TEST_CASE("El bote de rosca sale hueco, con el cuello bien unido al cuerpo", "[generator][jar]")
{
    const document::Generator* generator = document::findGenerator("bote-con-tapa");
    REQUIRE(generator != nullptr);
    for (const double wall : {1.2, 2.4, 4.0}) {
        document::GeneratorValues values = generator->defaults();
        values["pared"] = wall;
        document::Document doc;
        document::applyRecipe(doc, generator->make(values, ""));
        REQUIRE(doc.result().bodies.size() == 2);
        const geometry::Shape& jar = doc.result().bodies[0].shape;
        INFO("pared " << wall);
        CHECK(jar.isValid());
        CHECK(jar.solidCount() == 1);
        // Vacío: el agua que cabe es casi todo el cilindro interior (el de 60 × 40 con esa pared, menos hombro y redondeo).
        const double pi = std::numbers::pi;
        const double inner = pi * std::pow(30.0 - wall, 2) * (40.0 - 2.0);
        const double solid = pi * 900.0 * 50.0;
        CHECK(jar.volume() < solid - 0.85 * inner);
        // El hombro: a la altura en que acaba el cuerpo, un corte horizontal fino tiene un anillo de al menos media pared.
        const sketch::Plane slice = sketch::Plane::fromPointAndNormal({0.0, 0.0, 39.9}, {0.0, 0.0, 1.0});
        const geometry::Shape disc = geometry::cylinder("d", slice, 100.0, 0.05);
        const double area = geometry::boolean(geometry::BooleanOp::Common, jar, disc).volume() / 0.05;
        CHECK(area > pi * (std::pow(30.0, 2) - std::pow(30.0 - wall, 2)));
    }
}

TEST_CASE("Auditoría: tiempos y salud de todos los generadores", "[.audit]")
{
    document::setTextOutliner(squareLetters);
    using Clock = std::chrono::steady_clock;
    for (const document::Generator* generator : document::generators()) {
        for (const Case& c : choiceCases(*generator)) {
            document::Document doc;
            const auto start = Clock::now();
            const std::vector<std::string> problems = problemsOf(c, doc);
            std::printf("%8.0f ms  %2zu ops  %s\n", msSince(start), doc.featureCount(), c.label.c_str());
            for (const std::string& problem : problems) {
                std::printf("            MAL  %s\n", problem.c_str());
            }
            for (std::size_t i = 0; i < doc.featureCount(); ++i) {
                const auto one = Clock::now();
                (void)document::preview(doc, doc.feature(i), i);
                if (const double t = msSince(one); t > 150.0) {
                    std::printf("            %7.0f ms  %s\n", t, doc.feature(i).name().c_str());
                }
            }
            std::fflush(stdout);
        }
    }
    document::setTextOutliner({});
}

TEST_CASE("Auditoría: medidas más pequeñas y más grandes en todos los generadores", "[.audit3]")
{
    document::setTextOutliner(squareLetters);
    for (const document::Generator* generator : document::generators()) {
        std::size_t count = 0;
        std::size_t refusedCount = 0;
        for (const Case& c : sizeCases(*generator)) {
            document::Document doc;
            std::string refused;
            const std::vector<std::string> problems = problemsOf(c, doc, &refused);
            ++count;
            if (!refused.empty()) {
                ++refusedCount;
                std::printf("AVISA  %s -> %s\n", c.label.c_str(), refused.c_str());
            }
            for (const std::string& problem : problems) {
                std::printf("MAL    %s -> %s\n", c.label.c_str(), problem.c_str());
            }
            std::fflush(stdout);
        }
        std::printf("== %s: %zu casos, %zu avisos claros\n", generator->id().c_str(), count, refusedCount);
    }
    document::setTextOutliner({});
}

TEST_CASE("Cada medida de cada generador tiene su flecha, sobre la pieza", "[generator]")
{
    document::setTextOutliner(squareLetters);
    // Sin flecha a propósito: holguras de décimas entre dientes (no se verían), y los ángulos y cantidades.
    const std::vector<std::string> exempt = {"juego", "adelgazar"};
    for (const document::Generator* generator : document::generators()) {
        const auto arrows = generator->arrows();
        for (const document::GeneratorField& f : generator->fields()) {
            INFO(generator->id() << " · " << f.key);
            if (f.kind == document::GeneratorField::Kind::Length && std::ranges::find(exempt, f.key) == exempt.end()) {
                CHECK(arrows.count(f.key) == 1);
            }
        }
        // Las que dan un punto del diseño: en cada caso de opciones en que se ve la pregunta, la flecha empieza o acaba
        // cerca de algún cuerpo (a menos de 3 mm de su caja).
        for (const Case& c : choiceCases(*generator)) {
            document::Document doc;
            if (!problemsOf(c, doc).empty()) {
                continue;  // lo cuenta la otra prueba
            }
            for (const document::GeneratorField& f : generator->fields()) {
                const auto it = arrows.find(f.key);
                if (it == arrows.end() || it->second.body != document::Generator::kWorld || !shown(generator->fields(), f, c.values)) {
                    continue;
                }
                const std::array<double, 3> p = it->second.at(c.values);
                double best = 1e300;
                for (const document::Body& body : doc.result().bodies) {
                    const geometry::Box b = body.shape.bounds();
                    const double dx = std::max({b.min.x - p[0], 0.0, p[0] - b.max.x});
                    const double dy = std::max({b.min.y - p[1], 0.0, p[1] - b.max.y});
                    const double dz = std::max({b.min.z - p[2], 0.0, p[2] - b.max.z});
                    best = std::min(best, std::sqrt(dx * dx + dy * dy + dz * dz));
                }
                INFO(c.label << " · " << f.key << " en (" << p[0] << ", " << p[1] << ", " << p[2] << ")");
                CHECK(best < 3.0);
            }
        }
    }
    document::setTextOutliner({});
}

TEST_CASE("El bote avisa con palabras de las medidas con las que no puede funcionar", "[generator][jar]")
{
    const document::Generator* generator = document::findGenerator("bote-con-tapa");
    REQUIRE(generator != nullptr);
    const auto refuses = [&](std::vector<std::pair<const char*, double>> changes) {
        document::GeneratorValues values = generator->defaults();
        for (const auto& [key, value] : changes) {
            values[key] = value;
        }
        try {
            (void)generator->make(values, "");
        } catch (const std::exception& e) {
            return std::string(e.what()).size() > 20;
        }
        return false;
    };
    CHECK_FALSE(refuses({}));
    CHECK_FALSE(refuses({{"cierre", 1.0}}));
    CHECK_FALSE(refuses({{"forma", 1.0}}));
    CHECK(refuses({{"pared", 20.0}, {"diametro", 40.0}}));
    CHECK(refuses({{"alto", 15.0}, {"rosca_alto", 14.0}}));
    CHECK(refuses({{"cierre", 1.0}, {"cuello", 6.0}}));
    CHECK(refuses({{"cierre", 1.0}, {"diametro", 20.0}}));
    CHECK(refuses({{"cierre", 1.0}, {"pin_baja", 3.0}, {"pin_diametro", 8.0}}));
    CHECK(refuses({{"forma", 1.0}, {"ranura", 2.4}}));
    // El sufijo libre también lanza (arma la receta): por eso la interfaz lo busca en el hilo de trabajo.
}

TEST_CASE("Auditoría: medidas sin flecha", "[.audit4]")
{
    for (const document::Generator* generator : document::generators()) {
        const auto arrows = generator->arrows();
        std::printf("== %s\n", generator->id().c_str());
        for (const document::GeneratorField& f : generator->fields()) {
            using K = document::GeneratorField::Kind;
            if (f.kind != K::Choice && f.kind != K::Text && f.kind != K::Font && arrows.count(f.key) == 0) {
                std::printf("   %-18s %d  %-40s %s\n", f.key.c_str(), int(f.kind), f.label.c_str(), f.showKey.c_str());
            }
        }
    }
}
