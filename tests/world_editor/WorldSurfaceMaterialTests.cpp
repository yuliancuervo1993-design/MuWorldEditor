#include "Render/Renderer/WorldSurfaceMaterial.h"
#include "Render/Renderer/WorldSurfaceMaps.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace mu;

namespace
{
void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
    std::cout << "PASS " << message << "\n";
}

bool Near(float a, float b)
{
    return std::abs(a - b) < 0.0001f;
}
}

int main()
{
    try
    {
        Check(sizeof(WorldSurfaceUniforms) == 128, "world surface uniform ABI stays 128 bytes");

        const auto legacy = WorldSurfaceProfile(WorldSurfaceKind::Legacy);
        const auto ground = WorldSurfaceProfile(WorldSurfaceKind::Ground);
        const auto stone = WorldSurfaceProfile(WorldSurfaceKind::Stone);
        const auto wood = WorldSurfaceProfile(WorldSurfaceKind::Wood);
        const auto metal = WorldSurfaceProfile(WorldSurfaceKind::Metal);
        const auto foliage = WorldSurfaceProfile(WorldSurfaceKind::Foliage);
        const auto water = WorldSurfaceProfile(WorldSurfaceKind::Water);
        const auto snow = WorldSurfaceProfile(WorldSurfaceKind::Snow);

        Check(Near(legacy.material[0], 0.0f), "legacy profile kind");
        Check(Near(ground.material[0], 1.0f), "ground profile kind");
        Check(Near(stone.material[0], 2.0f), "stone profile kind");
        Check(Near(wood.material[0], 3.0f), "wood profile kind");
        Check(Near(metal.material[0], 4.0f), "metal profile kind");
        Check(Near(foliage.material[0], 5.0f), "foliage profile kind");
        Check(foliage.materialExtra[1] > ground.materialExtra[1], "foliage keeps wrapped diffuse for oblique cards");
        Check(foliage.materialExtra[2] < ground.materialExtra[2], "foliage keeps bounded cavity darkening");
        Check(Near(water.material[0], 6.0f), "water profile kind");
        Check(Near(snow.material[0], 7.0f), "snow profile kind");
        Check(metal.material[2] > 0.8f, "metal keeps metallic response");
        Check(water.material[1] < ground.material[1], "water remains smoother than ground");
        Check(snow.skyColor[2] > ground.skyColor[2], "snow profile keeps colder sky");

        WorldSurfaceEnvironment disabled{};
        auto environmentTarget = ground;
        const auto originalSun = environmentTarget.sunColor;
        ApplyWorldSurfaceEnvironment(environmentTarget, disabled);
        Check(environmentTarget.sunColor == originalSun, "disabled environment leaves profile unchanged");

        WorldSurfaceEnvironment enabled{};
        enabled.enabled = true;
        enabled.sunColor = {0.7f, 0.8f, 0.9f, 1.1f};
        enabled.skyColor = {0.2f, 0.3f, 0.6f, 0.9f};
        ApplyWorldSurfaceEnvironment(environmentTarget, enabled);
        Check(environmentTarget.sunColor == enabled.sunColor, "enabled environment overrides sun");
        Check(environmentTarget.skyColor == enabled.skyColor, "enabled environment overrides sky");

        WorldSurfaceMaps maps{};
        Check(!maps.Complete(), "empty map set is incomplete");
        maps.normalTextureId = 2;
        maps.metallicTextureId = 3;
        maps.roughnessTextureId = 4;
        Check(maps.Complete(), "normal metallic roughness make authored map set complete");
        Check(maps.albedoTextureId == 0, "albedo override stays optional");
        maps.layout = WorldSurfaceMapLayout::LorenciaWater;
        Check(static_cast<int>(maps.layout) == 3, "water layout ABI");

        std::cout << "ALL WORLD SURFACE MATERIAL TESTS PASSED\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}
