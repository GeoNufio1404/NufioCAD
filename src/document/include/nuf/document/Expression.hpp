#pragma once

// Expresiones numéricas de los parámetros: «grosor * 2 + 1.5», «sin(30) * largo»,
// «2 in»… Números con punto o coma decimal, + - * / ^, paréntesis, parámetros
// por su nombre, funciones (argumentos separados por «;») y unidades tras un
// número (mm, cm, m, in; deg, rad). Las longitudes salen en mm y los ángulos en
// grados; las funciones trigonométricas usan grados.

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace nuf::document {

class ExpressionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Valor de un parámetro por su nombre (nullopt si no existe).
using ParameterLookup = std::function<std::optional<double>(std::string_view)>;

// Lanza ExpressionError (con un mensaje para el usuario) si no se puede evaluar.
double evaluateExpression(std::string_view text, const ParameterLookup& lookup);

// Los nombres de parámetros que usa la expresión (sin repetir; no cuenta
// funciones, constantes ni unidades). Lanza ExpressionError si está mal escrita.
std::vector<std::string> expressionNames(std::string_view text);

// Nombre válido para un parámetro: letra o «_» seguido de letras, dígitos o
// «_», y que no sea una función, una constante ni una unidad.
bool isValidParameterName(std::string_view name);

} // namespace nuf::document
