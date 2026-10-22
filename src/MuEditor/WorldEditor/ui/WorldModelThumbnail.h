#pragma once
#include <array>
namespace Silver::WorldEditor
{
struct WorldModelThumbnail
{
    using Point = std::array<float, 2>;
    using Triangle = std::array<Point, 3>;
};
}
