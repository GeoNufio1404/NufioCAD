// NufioCAD: sustituye a Base/Console.h de FreeCAD. PlaneGCS solo escribe
// mensajes de depuración (log, desactivados) y avisos (warning, a stderr).
#pragma once

#include <format>
#include <iostream>
#include <utility>

namespace Base
{

class ConsoleSingleton
{
public:
    template<typename... Args>
    void log(std::format_string<Args...>, Args&&...)
    {}

    template<typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        std::cerr << "PlaneGCS: " << std::format(fmt, std::forward<Args>(args)...);
    }
};

inline ConsoleSingleton& Console()
{
    static ConsoleSingleton console;
    return console;
}

}  // namespace Base
