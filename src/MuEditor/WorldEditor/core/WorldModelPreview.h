#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
// CPU-only inspection. Never creates engine objects or changes a model slot.
class WorldModelPreview
{
public:
    using Point = std::array<float, 3>;
    using Triangle = std::array<Point, 3>;
    static constexpr std::size_t MaximumFileBytes = 64 * 1024 * 1024;
    static constexpr std::size_t MaximumPreviewTriangles = 200;
    static WorldModelPreview Decode(std::span<const std::uint8_t> bytes);
    std::string Name;
    int Version = 0, MeshCount = 0, BoneCount = 0, ActionCount = 0;
    std::size_t VertexCount = 0, TriangleCount = 0;
    std::vector<std::string> Textures;
    std::vector<Triangle> Geometry;
};
}
