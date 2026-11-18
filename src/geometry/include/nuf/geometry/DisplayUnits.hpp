#pragma once

// Unidad con la que el núcleo escribe las medidas en los textos que ve el usuario (Medir, avisos de impresión,
// resúmenes de los generadores). El modelo y los cálculos siguen siempre en milímetros: esto solo cambia el texto.
// La interfaz la pone (la misma unidad que el selector de la barra de estado); por defecto, milímetros, así que
// los textos no cambian si nadie la toca. Se puede leer desde cualquier hilo.

#include <atomic>
#include <cstdio>
#include <string>

namespace nuf::geometry::display {

enum class Unit { Mm, Cm, M, In };

namespace detail {

inline std::atomic<int>& unitIndex()
{
    static std::atomic<int> index{0};
    return index;
}

} // namespace detail

inline void setUnit(Unit unit)
{
    detail::unitIndex().store(int(unit));
}

inline Unit unit()
{
    return Unit(detail::unitIndex().load());
}

// Milímetros por unidad.
inline double mmPerUnit(Unit u = unit())
{
    switch (u) {
    case Unit::Mm: return 1.0;
    case Unit::Cm: return 10.0;
    case Unit::M: return 1000.0;
    case Unit::In: return 25.4;
    }
    return 1.0;
}

inline const char* label(Unit u = unit())
{
    switch (u) {
    case Unit::Mm: return "mm";
    case Unit::Cm: return "cm";
    case Unit::M: return "m";
    case Unit::In: return "in";
    }
    return "mm";
}

// Decimales de más en otras unidades para llegar a la misma precisión (igual que en la interfaz).
inline int extraDecimals(Unit u = unit())
{
    switch (u) {
    case Unit::Mm: return 0;
    case Unit::Cm: return 1;
    case Unit::In: return 2;
    case Unit::M: return 3;
    }
    return 0;
}

inline std::string fixed(double value, int decimals)
{
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    return text;
}

// El número ya convertido, sin unidad: «12.70».
inline std::string number(double mm, int decimals = 2)
{
    return fixed(mm / mmPerUnit(), decimals + extraDecimals());
}

// «12.70 mm».
inline std::string length(double mm, int decimals = 2)
{
    return number(mm, decimals) + " " + label();
}

// Áreas en la unidad al cuadrado («mm²») y volúmenes al cubo («mm³»).
inline std::string area(double mm2, int decimals = 2)
{
    const double factor = mmPerUnit();
    return fixed(mm2 / (factor * factor), decimals + extraDecimals()) + " " + label() + "²";
}

inline std::string volume(double mm3, int decimals = 2)
{
    const double factor = mmPerUnit();
    return fixed(mm3 / (factor * factor * factor), decimals + extraDecimals()) + " " + label() + "³";
}

} // namespace nuf::geometry::display
