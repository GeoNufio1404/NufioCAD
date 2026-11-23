#pragma once

// Exportar cuerpos a archivos que entienden otros programas: STL y OBJ (mallas
// para el slicer) y STEP (sólido exacto). Mallar no altera las formas que se
// pasan: se trabaja sobre una copia.

#include <nuf/geometry/Shape.hpp>

#include <cmath>
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

// Revisión de la malla que saldría con esa calidad (G4): si está cerrada, si algún borde lo comparten más de dos triángulos o
// dos triángulos lo recorren en el mismo sentido (malla mal orientada), cuántos triángulos son degenerados (de área casi
// nula) y cuánto se aparta su volumen del del sólido. Los vértices se sueldan a 1e-5 mm, como al escribir un 3MF.
struct MeshReport {
    std::size_t triangles = 0;
    std::size_t openEdges = 0;          // bordes de un solo triángulo: la malla tiene un agujero
    std::size_t nonManifoldEdges = 0;   // bordes de tres o más triángulos
    std::size_t flippedEdges = 0;       // bordes recorridos dos veces en el mismo sentido (triángulos al revés)
    std::size_t degenerateTriangles = 0;
    double meshVolume = 0.0;            // mm³ (con signo)
    double solidVolume = 0.0;           // mm³ del sólido exacto
    // Fracción en que el volumen de la malla se aparta del del sólido (0,01 = 1 %).
    double volumeDeviation() const { return solidVolume > 0.0 ? std::abs(meshVolume - solidVolume) / solidVolume : 0.0; }
};
MeshReport analyzeMesh(const Shape& shape, const MeshQuality& quality);

// Voladizos (O5): el área de las caras que miran hacia abajo más de lo que permite «maxAngle» (grados desde la vertical:
// con 45°, las que se inclinan menos de 45° respecto a la horizontal) sin apoyarse en la cama (Z mínima del cuerpo), y el
// área total de la malla.
struct OverhangReport {
    double overhangArea = 0.0;  // mm²
    double totalArea = 0.0;     // mm²
};
OverhangReport analyzeOverhang(const Shape& shape, const MeshQuality& quality, double maxAngleDegrees);

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

// Una malla (STL, binario o de texto, u OBJ) convertida a sólido: cada triángulo pasa a ser una cara plana, se cosen entre sí
// y los vértices iguales se sueldan. Solo vale si la malla es hermética (cerrada); las unidades no vienen en el archivo, se
// toman como milímetros. Con más de 30000 triángulos se rechaza (cada uno sería una cara). Lanza GeometryError si no se puede
// leer, no es hermética o es demasiado grande. Se parece a readStep: el resultado se guarda y se abre igual.
StepImport readMesh(const std::filesystem::path& path);

// Los cuerpos de un BREP guardado, uno por sólido, con sus caras nombradas
// «<tag>:?<n>» (o «<tag>:c<k>:?<n>» si son varios sólidos, para que no se
// repitan). Mismos nombres cada vez que se llama con los mismos datos.
std::vector<Shape> shapesFromBrep(const std::string& brep, const std::string& tag);

} // namespace nuf::geometry
