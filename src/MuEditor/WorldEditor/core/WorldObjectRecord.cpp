#include "WorldObjectRecord.h"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace Silver::WorldEditor
{
namespace
{
bool SameFloat(float left, float right)
{
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}
}
bool WorldObjectRecord::operator==(const WorldObjectRecord& other) const
{
    if (Type != other.Type || BaselineOnly != other.BaselineOnly || !SameFloat(Scale, other.Scale))
        return false;
    for (std::size_t axis = 0; axis < Position.size(); ++axis)
        if (!SameFloat(Position[axis], other.Position[axis]) || !SameFloat(Angle[axis], other.Angle[axis]))
            return false;
    return true;
}

void ValidateRecord(const WorldObjectRecord& record)
{
    if (record.Type < 0) throw std::invalid_argument("Tipo de objeto negativo.");
    for (float value : record.Position)
        if (!std::isfinite(value)) throw std::invalid_argument("Posicion no finita.");
    for (float value : record.Angle)
        if (!std::isfinite(value)) throw std::invalid_argument("Rotacion no finita.");
    if (!std::isfinite(record.Scale) || record.Scale < MinimumScale || record.Scale > MaximumScale)
        throw std::invalid_argument("Escala fuera de 0.01 a 100.");
}

int SpatialBlock(const WorldObjectRecord& record)
{
    ValidateRecord(record);
    constexpr float BlockSize = MapEdge / 16.0f;
    const auto& position = record.Position;
    if (position[0] < 0 || position[1] < 0 || position[0] >= MapEdge || position[1] >= MapEdge)
        throw std::invalid_argument("El objeto sale del mapa de 256 x 256.");
    return static_cast<int>(position[0] / BlockSize) * 16 + static_cast<int>(position[1] / BlockSize);
}

// Match MuMain CreateObject's truncation only when binding an EXISTING baseline.
// New transforms still use SpatialBlock's strict nonnegative editing bounds.
int LoadedSpatialBlock(const WorldObjectRecord& record)
{
    ValidateRecord(record);
    constexpr int BlocksPerAxis = 16;
    constexpr float BlockSize = MapEdge / static_cast<float>(BlocksPerAxis);
    const auto& position = record.Position;
    if (position[0] <= -BlockSize || position[1] <= -BlockSize || position[0] >= MapEdge || position[1] >= MapEdge)
        throw std::invalid_argument("El objeto original no pertenece a un bloque que MuMain pueda cargar.");
    return static_cast<int>(position[0] / BlockSize) * BlocksPerAxis + static_cast<int>(position[1] / BlockSize);
}
}
