#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>

namespace Silver::WorldEditor
{
struct WorldCameraViews
{
    struct Pose { std::array<float, 3> Position; float Yaw, Pitch; };
    static constexpr float Pi = 3.14159265358979323846f;
    static std::optional<Pose> FromTarget(int view, const std::array<float, 3>& target, float distance)
    {
        if (view < 0 || view > 3 || !std::isfinite(distance) || distance <= 0
            || !std::all_of(target.begin(), target.end(), [](float v) { return std::isfinite(v); })) return {};
        // MU: pitch zero looks down; -90 looks horizontally along +Y at yaw zero.
        constexpr std::array<float, 4> yaw{45.0f, 0.0f, 0.0f, 90.0f};
        constexpr std::array<float, 4> pitch{-45.0f, 0.0f, -90.0f, -90.0f};
        const auto index = static_cast<std::size_t>(view);
        const float y = yaw[index] * Pi / 180.0f, p = pitch[index] * Pi / 180.0f;
        const std::array<float, 3> forward{-std::sin(y) * std::sin(p), -std::cos(y) * std::sin(p), -std::cos(p)};
        Pose pose{target, yaw[index], pitch[index]};
        for (std::size_t axis = 0; axis < target.size(); ++axis) pose.Position[axis] -= forward[axis] * distance;
        return pose;
    }
};
}
