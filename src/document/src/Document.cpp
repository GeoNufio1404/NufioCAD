#include <nuf/document/Document.hpp>

#include <nuf/geometry/Cancel.hpp>
#include <nuf/geometry/TaskPool.hpp>
#include <nuf/geometry/Shape.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace nuf::document {

namespace {

std::string kindLabel(FeatureKind kind)
{
    switch (kind) {
    case FeatureKind::Sketch:
        return "Sketch";
    case FeatureKind::Extrude:
        return "Extrusión";
    case FeatureKind::Revolve:
        return "Revolución";
    case FeatureKind::Import:
        return "Importación";
    case FeatureKind::Fillet:
        return "Redondeo";
    case FeatureKind::Chamfer:
        return "Chaflán";
    case FeatureKind::Shell:
        return "Vaciado";
    case FeatureKind::Hole:
        return "Agujero";
    case FeatureKind::Mirror:
        return "Espejo";
    case FeatureKind::Split:
        return "División";
    case FeatureKind::Pattern:
        return "Patrón";
    case FeatureKind::ConstructionPlane:
        return "Plano";
    case FeatureKind::ConstructionAxis:
        return "Eje";
    case FeatureKind::Box:
        return "Caja";
    case FeatureKind::Cylinder:
        return "Cilindro";
    case FeatureKind::Gear:
        return "Engranaje";
    case FeatureKind::Thread:
        return "Rosca";
    case FeatureKind::Sweep:
        return "Barrido";
    case FeatureKind::Loft:
        return "Loft";
    case FeatureKind::Move:
        return "Mover";
    case FeatureKind::Combine:
        return "Combinar";
    case FeatureKind::Joint:
        return "Unión";
    case FeatureKind::Align:
        return "Alinear";
    case FeatureKind::MotionLink:
        return "Enlace de movimiento";
    }
    return "Operación";
}

// Una copia de la operación con sus expresiones ya evaluadas.
std::unique_ptr<Feature> bindExpressions(const Feature& feature, const ParameterLookup& lookup)
{
    std::unique_ptr<Feature> bound = feature.clone();
    for (const Feature::NumericField& field : bound->numericFields()) {
        const auto it = feature.expressions.find(field.key);
        if (it == feature.expressions.end()) {
            continue;
        }
        try {
            // set() ya convierte de las unidades del usuario (grados) a las internas (radianes).
            field.set(evaluateExpression(it->second, lookup));
        } catch (const ExpressionError& e) {
            throw FeatureError(field.label + ": " + e.what());
        }
    }
    return bound;
}

// Calcula la operación sobre «before».
std::pair<ModelState, FeatureStatus> computeOn(const ModelState& before, const Feature& original,
                                               const ParameterLookup& lookup)
{
    ModelState state = before;
    try {
        std::unique_ptr<Feature> bound;
        if (!original.expressions.empty()) {
            bound = bindExpressions(original, lookup);
        }
        const Feature& feature = bound ? *bound : original;
        const auto warning = feature.compute(state);
        return {std::move(state), warning ? FeatureStatus{Health::Warning, *warning} : FeatureStatus{}};
    } catch (const FeatureError& e) {
        return {before, {Health::Error, e.what()}};
    } catch (const geometry::GeometryError& e) {
        return {before, {Health::Error, e.what()}};
    } catch (const std::exception& e) {
        return {before, {Health::Error, std::string("Algo salió mal al calcular esta operación; prueba con otros valores o deshaz el último cambio (detalle técnico: ") + e.what() + ")"}};
    }
}

} // namespace

// --- ModelState y referencias --------------------------------------------

std::vector<geometry::Shape> ModelState::shapes() const
{
    std::vector<geometry::Shape> result;
    for (const Body& body : bodies) {
        result.push_back(body.shape);
    }
    return result;
}

std::optional<ModelState::FaceHit> ModelState::findFace(const geometry::ElementRef& ref) const
{
    if (ref.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (const auto face = bodies[i].shape.findFace(ref)) {
            return FaceHit{i, *face};
        }
    }
    return std::nullopt;
}

std::optional<ModelState::EdgeHit> ModelState::findEdge(const geometry::ElementRef& ref) const
{
    if (ref.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (const auto edge = bodies[i].shape.findEdge(ref)) {
            return EdgeHit{i, *edge};
        }
    }
    return std::nullopt;
}

std::vector<FeatureId> featuresIn(const geometry::ElementRef& ref)
{
    // «f<dígitos>:» al principio o tras «|» o «:».
    std::vector<FeatureId> ids;
    const std::string& s = ref.name;
    for (std::size_t p = 0; p < s.size(); ++p) {
        if (s[p] != 'f' || (p > 0 && s[p - 1] != '|' && s[p - 1] != ':')) {
            continue;
        }
        std::size_t q = p + 1;
        while (q < s.size() && std::isdigit(static_cast<unsigned char>(s[q]))) {
            ++q;
        }
        if (q > p + 1 && (q == s.size() || s[q] == ':')) {
            const FeatureId id = std::stoull(s.substr(p + 1, q - p - 1));
            if (std::ranges::find(ids, id) == ids.end()) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

// --- Document --------------------------------------------------------------

// Lo que lleva un cálculo en segundo plano: una copia de lo que necesita y, al acabar, lo que salió.
struct Document::Job {
    std::uint64_t generation = 0;
    std::size_t from = 0;  // la primera operación que recalcula
    geometry::TaskPool::TaskPtr task;  // la tarea en el grupo de hilos (se cancela y se espera a través de ella)
    std::vector<ModelState> states;
    std::vector<FeatureStatus> statuses;
};

Document::Document() = default;

Document::~Document()
{
    stopBackground();
}

void Document::stopBackground()
{
    // Las tareas trabajan sobre copias, pero no se pueden dejar sueltas (avisan a este documento): se les pide parar,
    // las que aún esperaban en la cola se descartan y se espera a las que corrían (las construcciones largas se
    // interrumpen a media operación, así que cerrar no espera a que termine una rosca).
    post_ = nullptr;
    for (auto& job : retired_) {
        job->task->cancel();
    }
    if (job_) {
        job_->task->cancel();
        retired_.push_back(std::move(job_));
    }
    for (auto& job : retired_) {
        job->task->wait();
    }
    retired_.clear();
}

void Document::setName(std::string name)
{
    if (name == name_) {
        return;
    }
    name_ = std::move(name);
    markChanged();
}

void Document::markChanged()
{
    ++revision_;
    changed.notify();
}

const Feature* Document::findFeature(FeatureId id) const
{
    const auto index = indexOf(id);
    return index ? features_[*index].get() : nullptr;
}

std::optional<std::size_t> Document::indexOf(FeatureId id) const
{
    for (std::size_t i = 0; i < features_.size(); ++i) {
        if (features_[i]->id() == id) {
            return i;
        }
    }
    return std::nullopt;
}

FeatureId Document::insertFeature(std::unique_ptr<Feature> feature, std::size_t index)
{
    if (!feature) {
        throw std::invalid_argument("Operación nula");
    }
    index = std::min(index, features_.size());
    if (feature->id() == 0) {
        feature->setId(nextId_++);
    } else {
        if (findFeature(feature->id())) {
            throw std::invalid_argument("Ya existe una operación con ese id");
        }
        nextId_ = std::max(nextId_, feature->id() + 1);
    }
    if (feature->name().empty()) {
        feature->setName(defaultName(feature->kind()));
    }
    const FeatureId id = feature->id();
    features_.insert(features_.begin() + std::ptrdiff_t(index), std::move(feature));
    // En segundo plano, hasta que se calcule se sigue viendo lo anterior (no un modelo vacío).
    states_.insert(states_.begin() + std::ptrdiff_t(index),
                   post_ && index > 0 ? states_[index - 1] : ModelState{});
    statuses_.insert(statuses_.begin() + std::ptrdiff_t(index), FeatureStatus{});
    if (index <= marker_) {
        ++marker_;
    }
    recomputeFrom(index);
    return id;
}

FeatureId Document::addFeature(std::unique_ptr<Feature> feature)
{
    return insertFeature(std::move(feature), marker_);
}

std::unique_ptr<Feature> Document::removeFeature(FeatureId id)
{
    const auto index = indexOf(id);
    if (!index) {
        return nullptr;
    }
    std::unique_ptr<Feature> removed = std::move(features_[*index]);
    features_.erase(features_.begin() + std::ptrdiff_t(*index));
    states_.erase(states_.begin() + std::ptrdiff_t(*index));
    statuses_.erase(statuses_.begin() + std::ptrdiff_t(*index));
    if (*index < marker_) {
        --marker_;
    }
    recomputeFrom(*index);
    return removed;
}

std::unique_ptr<Feature> Document::replaceFeature(FeatureId id, std::unique_ptr<Feature> feature)
{
    const auto index = indexOf(id);
    if (!index || !feature) {
        return nullptr;
    }
    feature->setId(id);
    if (feature->name().empty()) {
        feature->setName(features_[*index]->name());
    }
    std::unique_ptr<Feature> old = std::exchange(features_[*index], std::move(feature));
    recomputeFrom(*index);
    return old;
}

void Document::setSuppressed(FeatureId id, bool suppressed)
{
    const auto index = indexOf(id);
    if (!index || features_[*index]->suppressed() == suppressed) {
        return;
    }
    features_[*index]->setSuppressed(suppressed);
    recomputeFrom(*index);
}

bool Document::canMoveFeature(FeatureId id, std::size_t index) const
{
    const auto from = indexOf(id);
    if (!from) {
        return false;
    }
    std::vector<const Feature*> order;
    for (const auto& f : features_) {
        if (f->id() != id) {
            order.push_back(f.get());
        }
    }
    order.insert(order.begin() + std::ptrdiff_t(std::min(index, order.size())), features_[*from].get());
    std::set<FeatureId> seen;
    for (const Feature* f : order) {
        for (FeatureId dep : f->dependencies()) {
            if (findFeature(dep) && !seen.contains(dep)) {
                return false;
            }
        }
        seen.insert(f->id());
    }
    return true;
}

void Document::moveFeature(FeatureId id, std::size_t index)
{
    const auto from = indexOf(id);
    if (!from) {
        return;
    }
    std::unique_ptr<Feature> moving = std::move(features_[*from]);
    features_.erase(features_.begin() + std::ptrdiff_t(*from));
    index = std::min(index, features_.size());
    features_.insert(features_.begin() + std::ptrdiff_t(index), std::move(moving));
    recomputeFrom(std::min(*from, index));
}

std::vector<FeatureId> Document::dependents(FeatureId id) const
{
    std::set<FeatureId> affected{id};
    std::vector<FeatureId> result;
    for (const auto& f : features_) {
        for (FeatureId dep : f->dependencies()) {
            if (affected.contains(dep) && f->id() != id) {
                affected.insert(f->id());
                result.push_back(f->id());
                break;
            }
        }
    }
    return result;
}

void Document::setMarker(std::size_t marker)
{
    marker = std::min(marker, features_.size());
    if (marker == marker_) {
        return;
    }
    const std::size_t from = std::min(marker, marker_);
    marker_ = marker;
    recomputeFrom(from);
}

std::string Document::defaultName(FeatureKind kind) const
{
    const std::string label = kindLabel(kind);
    int highest = 0;
    for (const auto& f : features_) {
        const std::string& name = f->name();
        if (f->kind() != kind || !name.starts_with(label)) {
            continue;
        }
        const std::string digits = name.substr(label.size());
        if (!digits.empty() && std::ranges::all_of(digits, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            highest = std::max(highest, std::stoi(digits));
        }
    }
    return label + std::to_string(highest + 1);
}

void Document::setBodyInfo(std::map<std::string, BodyInfo> info)
{
    // Una entrada vacía no dice nada.
    std::erase_if(info, [](const auto& entry) {
        return entry.second.name.empty() && entry.second.component.empty() && entry.second.color.empty() && !entry.second.fixed;
    });
    if (info == bodyInfo_) {
        return;
    }
    std::set<std::string> fixed;
    for (const auto& [key, entry] : info) {
        if (entry.fixed) {
            fixed.insert(key);
        }
    }
    const bool fixedChanged = fixed != empty_.fixedBodies;
    empty_.fixedBodies = std::move(fixed);
    // Los cuerpos que ya no llevan nombre propio vuelven al suyo: se rehace el estado.
    const bool lostName = std::ranges::any_of(bodyInfo_, [&](const auto& entry) {
        const auto next = info.find(entry.first);
        return (!entry.second.name.empty() && (next == info.end() || next->second.name.empty()))
            || (!entry.second.color.empty() && (next == info.end() || next->second.color.empty()));
    });
    bodyInfo_ = std::move(info);
    // Fijar o soltar un cuerpo cambia dónde quedan las alineaciones y las uniones: se rehace desde el principio.
    const bool placements = fixedChanged && std::ranges::any_of(features_, [](const auto& f) {
        return f->kind() == FeatureKind::Align || f->kind() == FeatureKind::Joint || f->kind() == FeatureKind::MotionLink;
    });
    if (lostName || placements) {
        recomputeFrom(0);
    } else {
        applyBodyInfo();
        markChanged();
    }
}

std::string Document::componentOf(const std::string& bodyKey) const
{
    const auto it = bodyInfo_.find(bodyKey);
    return it == bodyInfo_.end() ? std::string() : it->second.component;
}

bool Document::isFixed(const std::string& bodyKey) const
{
    const auto it = bodyInfo_.find(bodyKey);
    return it != bodyInfo_.end() && it->second.fixed;
}

void Document::applyBodyInfo()
{
    if (bodyInfo_.empty()) {
        return;
    }
    for (ModelState& state : states_) {
        for (Body& body : state.bodies) {
            const auto it = bodyInfo_.find(body.key);
            if (it != bodyInfo_.end() && !it->second.name.empty()) {
                body.name = it->second.name;
            }
            if (it != bodyInfo_.end() && !it->second.color.empty()) {
                body.color = it->second.color;
            }
        }
    }
}

void Document::setParameters(std::vector<Parameter> parameters)
{
    if (parameters == parameters_) {
        return;
    }
    parameters_ = std::move(parameters);
    evaluateParameters();
    // Desde la primera operación que usa expresiones.
    for (std::size_t i = 0; i < features_.size(); ++i) {
        if (!features_[i]->expressions.empty()) {
            recomputeFrom(i);
            return;
        }
    }
    markChanged();
}

Document::ExpressionSnapshot Document::expressionSnapshot() const
{
    ExpressionSnapshot snapshot;
    for (const auto& feature : features_) {
        if (!feature->expressions.empty()) {
            snapshot[feature->id()] = feature->expressions;
        }
    }
    return snapshot;
}

void Document::setParameters(std::vector<Parameter> parameters, const std::map<std::string, std::string>& renames)
{
    if (renames.empty()) {
        setParameters(std::move(parameters));
        return;
    }
    ExpressionSnapshot snapshot = expressionSnapshot();
    for (auto& [id, fields] : snapshot) {
        for (auto& [field, text] : fields) {
            text = renameInExpression(text, renames);
        }
    }
    restoreParameters(std::move(parameters), snapshot);
}

void Document::restoreParameters(std::vector<Parameter> parameters, const ExpressionSnapshot& snapshot)
{
    bool differs = parameters != parameters_;
    std::optional<std::size_t> first;
    for (std::size_t i = 0; i < features_.size(); ++i) {
        const auto it = snapshot.find(features_[i]->id());
        static const std::map<std::string, std::string> kNone;
        const auto& wanted = it == snapshot.end() ? kNone : it->second;
        if (features_[i]->expressions != wanted) {
            features_[i]->expressions = wanted;
            differs = true;
            if (!first) {
                first = i;
            }
        }
    }
    if (!differs) {
        return;
    }
    parameters_ = std::move(parameters);
    evaluateParameters();
    // Desde la primera operación cambiada o que use expresiones.
    std::optional<std::size_t> start = first;
    for (std::size_t i = 0; i < features_.size() && (!start || i < *start); ++i) {
        if (!features_[i]->expressions.empty()) {
            start = i;
        }
    }
    if (start) {
        recomputeFrom(*start);
    } else {
        markChanged();
    }
}

ParameterValue Document::parameterValue(const std::string& name) const
{
    const auto it = values_.find(name);
    return it == values_.end() ? ParameterValue{std::nullopt, "No existe el parámetro"} : it->second;
}

ParameterLookup Document::lookup() const
{
    return [this](std::string_view name) -> std::optional<double> {
        const auto it = values_.find(std::string(name));
        if (it == values_.end()) {
            return std::nullopt;
        }
        if (!it->second.value) {
            throw ExpressionError("El parámetro «" + std::string(name) + "» tiene un error: " + it->second.error);
        }
        return it->second.value;
    };
}

ParameterLookup Document::snapshotLookup() const
{
    const auto values = std::make_shared<const std::map<std::string, ParameterValue>>(values_);
    return [values](std::string_view name) -> std::optional<double> {
        const auto it = values->find(std::string(name));
        if (it == values->end()) {
            return std::nullopt;
        }
        if (!it->second.value) {
            throw ExpressionError("El parámetro «" + std::string(name) + "» tiene un error: " + it->second.error);
        }
        return it->second.value;
    };
}

void Document::evaluateParameters()
{
    values_.clear();
    enum class Mark { Visiting, Done };
    std::map<std::string, Mark> marks;
    std::function<double(const std::string&)> eval = [&](const std::string& name) -> double {
        const auto done = values_.find(name);
        if (done != values_.end() && done->second.value) {
            return *done->second.value;
        }
        const auto mark = marks.find(name);
        if (mark != marks.end() && mark->second == Mark::Visiting) {
            throw ExpressionError("Referencia circular con «" + name + "»");
        }
        const auto param = std::ranges::find_if(parameters_, [&](const Parameter& p) { return p.name == name; });
        if (param == parameters_.end()) {
            throw ExpressionError("No existe el parámetro «" + name + "»");
        }
        marks[name] = Mark::Visiting;
        double value = 0.0;
        try {
            value = evaluateExpression(param->expression, [&](std::string_view n) -> std::optional<double> {
                const std::string other(n);
                if (std::ranges::none_of(parameters_, [&](const Parameter& p) { return p.name == other; })) {
                    return std::nullopt;
                }
                return eval(other);
            });
        } catch (...) {
            marks.erase(name);
            throw;
        }
        marks[name] = Mark::Done;
        values_[name] = {value, {}};
        return value;
    };
    for (const Parameter& p : parameters_) {
        try {
            eval(p.name);
        } catch (const ExpressionError& e) {
            values_[p.name] = {std::nullopt, e.what()};
        }
    }
}

void Document::restore(std::string name, std::vector<std::unique_ptr<Feature>> features, std::size_t marker,
                       FeatureId nextId, std::vector<Parameter> parameters)
{
    FeatureId highest = 0;
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (!features[i] || features[i]->id() == 0) {
            throw std::invalid_argument("Operación sin id");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (features[j]->id() == features[i]->id()) {
                throw std::invalid_argument("Hay dos operaciones con el mismo id");
            }
        }
        highest = std::max(highest, features[i]->id());
    }
    name_ = std::move(name);
    features_ = std::move(features);
    states_.assign(features_.size(), ModelState{});
    statuses_.assign(features_.size(), FeatureStatus{});
    marker_ = std::min(marker, features_.size());
    nextId_ = std::max(nextId, highest + 1);
    parameters_ = std::move(parameters);
    evaluateParameters();
    recomputeFrom(0);
}

const ModelState& Document::result() const
{
    return marker_ == 0 ? empty_ : states_[marker_ - 1];
}

const ModelState& Document::stateBefore(std::size_t index) const
{
    index = std::min(index, states_.size());
    return index == 0 ? empty_ : states_[index - 1];
}

const FeatureStatus& Document::status(FeatureId id) const
{
    static const FeatureStatus kUnknown{Health::Error, "No existe la operación"};
    const auto index = indexOf(id);
    return index ? statuses_[*index] : kUnknown;
}

void Document::enableBackgroundCompute(Poster post)
{
    waitForCompute();
    post_ = std::move(post);
}

void Document::waitForCompute()
{
    if (job_) {
        finishBackground(job_->generation);
    }
}

void Document::reapRetired()
{
    std::erase_if(retired_, [](const std::shared_ptr<Job>& job) { return job->task->finished(); });
}

// El cálculo en curso ya no vale: se le pide parar (el hilo se recoge cuando termine).
void Document::retireJob()
{
    if (job_) {
        job_->task->cancel();
        retired_.push_back(std::move(job_));
    }
    reapRetired();
}

void Document::startBackground(std::size_t index)
{
    const bool wasComputing = job_ != nullptr;
    dirty_ = dirty_ ? std::min(*dirty_, index) : index;
    retireJob();
    const std::size_t from = *dirty_;
    if (from >= features_.size()) {
        // Nada que calcular (se quitó lo último): lo mostrado ya es lo correcto.
        dirty_.reset();
        applyBodyInfo();
        markChanged();
        if (wasComputing) {
            computingChanged.notify(false);
        }
        return;
    }

    // Todo lo que necesita el hilo es una copia: el documento puede seguir cambiando.
    auto job = std::make_shared<Job>();
    job->generation = ++generation_;
    job->from = from;
    std::vector<std::unique_ptr<Feature>> copies;
    for (std::size_t i = from; i < features_.size(); ++i) {
        copies.push_back(features_[i]->clone());
    }
    ModelState before = from == 0 ? empty_ : states_[from - 1];
    const std::size_t marker = marker_;
    const std::uint64_t generation = job->generation;
    Document* self = this;
    const Poster post = post_;
    const std::weak_ptr<bool> alive = alive_;
    job_ = job;
    Job* raw = job.get();
    job->task = geometry::TaskPool::instance().submit(
        // std::function exige un objeto copiable: las operaciones (únicas) viajan en un shared_ptr.
        [raw, self, post, alive, generation, from, marker, lookup = snapshotLookup(),
         owned = std::make_shared<std::vector<std::unique_ptr<Feature>>>(std::move(copies)),
         before = std::move(before)]() mutable {
        const std::vector<std::unique_ptr<Feature>>& features = *owned;
        try {
            for (std::size_t k = 0; k < features.size() && !geometry::cancelRequested(); ++k) {
                const Feature& feature = *features[k];
                if (from + k >= marker) {
                    raw->states.push_back(before);
                    raw->statuses.push_back({Health::RolledBack, {}});
                } else if (feature.suppressed()) {
                    raw->states.push_back(before);
                    raw->statuses.push_back({Health::Suppressed, {}});
                } else {
                    auto [state, status] = computeOn(before, feature, lookup);
                    raw->states.push_back(std::move(state));
                    raw->statuses.push_back(std::move(status));
                }
                before = raw->states.back();
            }
        } catch (...) {
            // Lo que salga mal aquí deja el cálculo incompleto: no se instala (ver finishBackground).
        }
        if (!geometry::cancelRequested()) {
            post([self, alive, generation] {
                if (alive.lock()) {
                    self->finishBackground(generation);
                }
            });
        }
    });

    // La interfaz se refresca ya con lo anterior (el historial cambió aunque el modelo aún no).
    applyBodyInfo();
    markChanged();
    if (!wasComputing) {
        computingChanged.notify(true);
    }
}

// En el hilo del documento: el cálculo terminó; si sigue siendo el vigente, su resultado se instala.
void Document::finishBackground(std::uint64_t generation)
{
    if (!job_ || job_->generation != generation) {
        return;  // lo cancelaron o lo reemplazó otro más nuevo
    }
    const std::shared_ptr<Job> job = std::move(job_);
    job->task->wait();  // ya terminó (avisó al acabar): solo falta que suelte lo suyo
    reapRetired();
    if (job->from + job->states.size() == features_.size()) {
        for (std::size_t k = 0; k < job->states.size(); ++k) {
            states_[job->from + k] = std::move(job->states[k]);
            statuses_[job->from + k] = std::move(job->statuses[k]);
        }
        dirty_.reset();
    }
    applyBodyInfo();
    markChanged();
    computingChanged.notify(false);
}

void Document::recomputeFrom(std::size_t index)
{
    if (post_) {
        startBackground(index);
        return;
    }
    for (std::size_t i = index; i < features_.size(); ++i) {
        const ModelState& before = i == 0 ? empty_ : states_[i - 1];
        const Feature& feature = *features_[i];
        if (i >= marker_) {
            states_[i] = before;
            statuses_[i] = {Health::RolledBack, {}};
        } else if (feature.suppressed()) {
            states_[i] = before;
            statuses_[i] = {Health::Suppressed, {}};
        } else {
            auto [state, status] = computeOn(before, feature, lookup());
            states_[i] = std::move(state);
            statuses_[i] = std::move(status);
        }
    }
    applyBodyInfo();
    markChanged();
}

Preview preview(const Document& document, const Feature& feature, std::size_t index)
{
    return preview(document.stateBefore(index), feature, document.lookup());
}

Preview preview(const ModelState& before, const Feature& feature, const ParameterLookup& lookup)
{
    auto [state, status] = computeOn(before, feature, lookup);
    return {std::move(state), std::move(status)};
}

} // namespace nuf::document
