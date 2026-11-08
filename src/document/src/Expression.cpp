#include <nuf/document/Expression.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>

namespace nuf::document {

namespace {

constexpr double kDegree = std::numbers::pi / 180.0;

struct Function {
    const char* name;
    int arguments;  // -1: uno o más
};

constexpr Function kFunctions[] = {
    {"sin", 1}, {"cos", 1}, {"tan", 1}, {"asin", 1}, {"acos", 1}, {"atan", 1}, {"sqrt", 1}, {"abs", 1},
    {"round", 1}, {"floor", 1}, {"ceil", 1}, {"min", -1}, {"max", -1}, {"pow", 2},
};

constexpr const char* kConstants[] = {"pi", "e"};
constexpr const char* kUnits[] = {"mm", "cm", "m", "in", "deg", "rad"};

bool isFunction(std::string_view name)
{
    return std::ranges::any_of(kFunctions, [&](const Function& f) { return name == f.name; });
}
bool isConstant(std::string_view name)
{
    return std::ranges::any_of(kConstants, [&](const char* c) { return name == c; });
}
bool isUnit(std::string_view name)
{
    return std::ranges::any_of(kUnits, [&](const char* u) { return name == u; });
}

double unitFactor(std::string_view unit)
{
    if (unit == "cm") {
        return 10.0;
    }
    if (unit == "m") {
        return 1000.0;
    }
    if (unit == "in") {
        return 25.4;
    }
    if (unit == "rad") {
        return 1.0 / kDegree;
    }
    return 1.0;  // mm, deg
}

// Evalúa o solo recorre (con lookup nulo, anota los nombres que encuentra).
class Parser {
public:
    Parser(std::string_view text, const ParameterLookup* lookup, std::vector<std::string>* names)
        : s_(text)
        , lookup_(lookup)
        , names_(names)
    {
    }

    double parse()
    {
        skip();
        if (pos_ >= s_.size()) {
            throw ExpressionError("La expresión está vacía");
        }
        const double value = expression();
        skip();
        if (pos_ < s_.size()) {
            throw ExpressionError(std::string("No se entiende «") + s_[pos_] + "» en la expresión");
        }
        return value;
    }

private:
    void skip()
    {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
    }
    bool take(char c)
    {
        skip();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    double expression()
    {
        double value = term();
        for (;;) {
            if (take('+')) {
                value += term();
            } else if (take('-')) {
                value -= term();
            } else {
                return value;
            }
        }
    }

    double term()
    {
        double value = unary();
        for (;;) {
            if (take('*')) {
                value *= unary();
            } else if (take('/')) {
                const double divisor = unary();
                if (lookup_ != nullptr && divisor == 0.0) {
                    throw ExpressionError("División entre cero");
                }
                value = divisor == 0.0 ? 0.0 : value / divisor;
            } else {
                return value;
            }
        }
    }

    double unary()
    {
        if (take('-')) {
            return -unary();
        }
        if (take('+')) {
            return unary();
        }
        return power();
    }

    double power()
    {
        const double base = primary();
        if (take('^')) {
            const double exponent = unary();
            const double result = std::pow(base, exponent);
            if (lookup_ != nullptr && !std::isfinite(result)) {
                throw ExpressionError("El resultado de la potencia no es un número");
            }
            return std::isfinite(result) ? result : 0.0;
        }
        return base;
    }

    double number()
    {
        const std::size_t start = pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' || s_[pos_] == ',')) {
            ++pos_;
        }
        std::string digits(s_.substr(start, pos_ - start));
        std::ranges::replace(digits, ',', '.');
        double value = 0.0;
        try {
            std::size_t used = 0;
            value = std::stod(digits, &used);
            if (used != digits.size()) {
                throw ExpressionError("Número mal escrito: " + digits);
            }
        } catch (const std::invalid_argument&) {
            throw ExpressionError("Número mal escrito: " + digits);
        }
        // Unidad opcional pegada o tras un espacio.
        const std::size_t save = pos_;
        skip();
        const std::string unit = word();
        if (!unit.empty() && isUnit(unit)) {
            return value * unitFactor(unit);
        }
        if (s_.substr(pos_).starts_with("°")) {
            pos_ += std::string_view("°").size();
            return value;
        }
        pos_ = save;
        return value;
    }

    std::string word()
    {
        const std::size_t start = pos_;
        if (pos_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) {
            ++pos_;
            while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) {
                ++pos_;
            }
        }
        return std::string(s_.substr(start, pos_ - start));
    }

    double primary()
    {
        skip();
        if (pos_ >= s_.size()) {
            throw ExpressionError("La expresión termina antes de tiempo");
        }
        const char c = s_[pos_];
        if (c == '(') {
            ++pos_;
            const double value = expression();
            if (!take(')')) {
                throw ExpressionError("Falta cerrar un paréntesis");
            }
            return value;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ',') {
            return number();
        }
        const std::string name = word();
        if (name.empty()) {
            throw ExpressionError(std::string("No se entiende «") + c + "» en la expresión");
        }
        skip();
        if (pos_ < s_.size() && s_[pos_] == '(') {
            return call(name);
        }
        if (name == "pi") {
            return std::numbers::pi;
        }
        if (name == "e") {
            return std::numbers::e;
        }
        if (isFunction(name) || isUnit(name)) {
            throw ExpressionError("«" + name + "» no es un parámetro");
        }
        if (names_ != nullptr && std::ranges::find(*names_, name) == names_->end()) {
            names_->push_back(name);
        }
        if (lookup_ == nullptr) {
            return 0.0;
        }
        const auto value = (*lookup_)(name);
        if (!value) {
            throw ExpressionError("No existe el parámetro «" + name + "»");
        }
        return *value;
    }

    double call(const std::string& name)
    {
        const auto fn = std::ranges::find_if(kFunctions, [&](const Function& f) { return name == f.name; });
        if (fn == std::end(kFunctions)) {
            throw ExpressionError("No existe la función «" + name + "»");
        }
        ++pos_;  // «(»
        std::vector<double> args;
        if (!take(')')) {
            do {
                args.push_back(expression());
            } while (take(';'));
            if (!take(')')) {
                throw ExpressionError("Falta cerrar el paréntesis de «" + name + "»");
            }
        }
        if ((fn->arguments >= 0 && int(args.size()) != fn->arguments) || (fn->arguments < 0 && args.empty())) {
            throw ExpressionError("«" + name + "» no lleva esa cantidad de argumentos");
        }
        const double a = args.front();
        double result = 0.0;
        if (name == "sin") {
            result = std::sin(a * kDegree);
        } else if (name == "cos") {
            result = std::cos(a * kDegree);
        } else if (name == "tan") {
            result = std::tan(a * kDegree);
        } else if (name == "asin") {
            result = std::asin(a) / kDegree;
        } else if (name == "acos") {
            result = std::acos(a) / kDegree;
        } else if (name == "atan") {
            result = std::atan(a) / kDegree;
        } else if (name == "sqrt") {
            result = std::sqrt(a);
        } else if (name == "abs") {
            result = std::abs(a);
        } else if (name == "round") {
            result = std::round(a);
        } else if (name == "floor") {
            result = std::floor(a);
        } else if (name == "ceil") {
            result = std::ceil(a);
        } else if (name == "pow") {
            result = std::pow(a, args[1]);
        } else if (name == "min") {
            result = *std::ranges::min_element(args);
        } else if (name == "max") {
            result = *std::ranges::max_element(args);
        }
        if (!std::isfinite(result)) {
            if (lookup_ != nullptr) {
                throw ExpressionError("«" + name + "» no da un número con ese valor");
            }
            return 0.0;
        }
        return result;
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    const ParameterLookup* lookup_;
    std::vector<std::string>* names_;
};

} // namespace

double evaluateExpression(std::string_view text, const ParameterLookup& lookup)
{
    return Parser(text, &lookup, nullptr).parse();
}

std::vector<std::string> expressionNames(std::string_view text)
{
    std::vector<std::string> names;
    Parser(text, nullptr, &names).parse();
    return names;
}

std::string renameInExpression(std::string_view text, const std::map<std::string, std::string>& renames)
{
    std::string result;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isalpha(c) || c == '_') {
            std::size_t end = i + 1;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
                ++end;
            }
            const std::string word(text.substr(i, end - i));
            const auto it = renames.find(word);
            result += it != renames.end() ? it->second : word;
            i = end;
        } else {
            result += text[i++];
        }
    }
    return result;
}

bool isValidParameterName(std::string_view name)
{
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) {
        return false;
    }
    if (!std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) {
        return false;
    }
    return !isFunction(name) && !isConstant(name) && !isUnit(name);
}

} // namespace nuf::document
