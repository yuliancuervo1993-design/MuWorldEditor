#pragma once
#include "WorldObjectRecord.h"
#include <span>
#include <vector>

namespace Silver::WorldEditor
{
class WorldCadLayout
{
public:
    enum class Shape { Line, Polyline, Rectangle, Circle };
    using Point = std::array<float,2>;
    static constexpr std::size_t MaximumInstances = 256;
    static constexpr std::size_t MaximumPoints = 32;
    Shape Kind = Shape::Line;
    std::vector<Point> Points{{1000,1000},{2000,1000}};
    float Spacing = 100;
    float Radius = 500;
    bool AlignToPath = false;
    float Length() const;
    std::vector<WorldObjectRecord> Generate(const WorldObjectRecord& prototype) const;
    std::vector<std::uint8_t> Encode(int mapId) const;
    static WorldCadLayout Decode(std::span<const std::uint8_t> bytes, int mapId);
private:
    void Validate() const;
    std::vector<Point> Route() const;
};
}
