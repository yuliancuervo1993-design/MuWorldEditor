#pragma once
namespace Silver::WorldEditor
{
// Shared by the actual edit and its overlay. Radius zero is exactly one cell.
struct TerrainBrushFootprint
{
    static constexpr int Side = 256;
    static constexpr int MaximumRadius = 16;
    static constexpr bool Contains(int x, int y, int centerX, int centerY, int radius)
    {
        if (radius < 0 || radius > MaximumRadius || x < 0 || y < 0 || x >= Side || y >= Side
            || centerX < 0 || centerY < 0 || centerX >= Side || centerY >= Side) return false;
        const int dx = x - centerX, dy = y - centerY;
        return dx * dx + dy * dy <= radius * radius;
    }
};
}
