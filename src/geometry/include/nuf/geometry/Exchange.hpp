#pragma once

// Exportar cuerpos a archivos que entienden otros programas: STL y OBJ (mallas
// para el slicer) y STEP (sólido exacto). Mallar no altera las formas que se
// pasan: se trabaja sobre una copia.

#include <nuf/geometry/Shape.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nuf::geometry {

// Cuánto se aparta la malla de la superficie real. Valores en mm y radianes.
struct MeshQuality {
    double linearDeflection = 0.1;
    double angularDeflection = 0.5;

    static MeshQuality low() { return {0.5, 0.8}; }
    static MeshQuality medium() { return {0.1, 0.5}; }
    static MeshQuality high() { return {0.02, 0.2}; }
};

// Triángulos que tendría la malla de esas formas con esa calidad.
std::size_t countTriangles(const std::vector<Shape>& shapes, const MeshQuality& quality);

// Volumen que encierra la malla (mm³, con signo): sirve para comprobar que la
// malla es fiel al sólido y está cerrada.
double meshVolume(const std::vector<Shape>& shapes, const MeshQuality& quality);

// Todas las formas juntas en un solo archivo. Lanzan GeometryError si no hay
// nada que exportar o no se puede escribir.
void writeStl(const std::filesystem::path& path, const std::vector<Shape>& shapes, const MeshQuality& quality);
void writeObj(const std::filesystem::path& path, const std::vector<Shape>& shapes, const MeshQuality& quality);
void writeStep(const std::filesystem::path& path, const std::vector<Shape>& shapes);

// 3MF (el formato que leen Bambu Studio, Orca Slicer y PrusaSlicer): un objeto por cuerpo, con su nombre y un material de
// color («Filamento 1»…) para repartir cada cuerpo en un filamento. «rgb» = 0xRRGGBB; con 0 se toma uno de la paleta
// según el orden del cuerpo. Los vértices se sueldan (la malla es indexada) y el archivo es un ZIP sin comprimir.
struct ThreeMfPart {
    Shape shape;
    std::string name;
    std::uint32_t rgb = 0;
};
void writeThreeMf(const std::filesystem::path& path, const std::vector<ThreeMfPart>& parts, const MeshQuality& quality);

// Un STEP leído y reparado, listo para guardarse: solo los sólidos, en BREP
// binario (así abrir el diseño no vuelve a analizar el STEP).
struct StepImport {
    std::string brep;
    int solids = 0;
    std::vector<std::string> warnings;  // reparos o cosas que se dejaron fuera
};

// Lanza GeometryError si no se puede leer o no hay ningún sólido.
StepImport readStep(const std::filesystem::path& path);

// Los cuerpos de un BREP guardado, uno por sólido, con sus caras nombradas
// «<tag>:?<n>» (o «<tag>:c<k>:?<n>» si son varios sólidos, para que no se
// repitan). Mismos nombres cada vez que se llama con los mismos datos.
std::vector<Shape> shapesFromBrep(const std::string& brep, const std::string& tag);

} // namespace nuf::geometry
