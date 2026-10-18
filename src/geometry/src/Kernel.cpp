#include <nuf/geometry/Kernel.hpp>

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Version.hxx>

#include <stdexcept>

namespace nuf::geometry {

std::string kernelVersion()
{
    return OCC_VERSION_COMPLETE;
}

double probeBoxVolume(double dx, double dy, double dz)
{
    if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) {
        throw std::invalid_argument("Las dimensiones de la caja deben ser positivas");
    }

    BRepPrimAPI_MakeBox box(dx, dy, dz);
    GProp_GProps props;
    BRepGProp::VolumeProperties(box.Shape(), props);
    return props.Mass();
}

} // namespace nuf::geometry
