#include "WorldPrefabCodec.h"
#include <iostream>
#include <stdexcept>

using namespace Silver::WorldEditor;

namespace
{
int passed = 0;
void Check(bool value, const char* name)
{
    if (!value) throw std::runtime_error(name);
    ++passed;
    std::cout << "PASS " << name << '\n';
}

WorldObjectRecord Record(std::int16_t type, float x, float y)
{
    return {type, {x, y, 30.0f}, {0.0f, 0.0f, 45.0f}, 1.25f};
}

void PrefabContract()
{
    const std::vector<WorldObjectRecord> source{Record(10, 100.0f, 200.0f), Record(11, 150.0f, 260.0f)};
    const auto prefab = WorldPrefabCodec::FromSelection("Plaza Lorencia", source, {100.0f, 200.0f, 30.0f});
    Check(prefab.Records.size() == 2, "prefab captures complete selection");
    Check(prefab.Records[0].Position[0] == 0.0f && prefab.Records[0].Position[1] == 0.0f,
        "prefab stores position relative to pivot");
    Check(prefab.Records[1].Position[0] == 50.0f && prefab.Records[1].Position[1] == 60.0f,
        "prefab preserves relative layout");

    const auto bytes = WorldPrefabCodec::Encode(prefab);
    const auto decoded = WorldPrefabCodec::Decode(bytes);
    Check(decoded.Name == "Plaza Lorencia" && decoded.Records.size() == 2, "prefab JSON round-trip");

    const auto placed = WorldPrefabCodec::Instantiate(decoded, {1000.0f, 2000.0f, 50.0f});
    Check(placed[0].Position[0] == 1000.0f && placed[0].Position[1] == 2000.0f,
        "prefab instance anchors first relative record at destination");
    Check(placed[1].Position[0] == 1050.0f && placed[1].Position[1] == 2060.0f,
        "prefab instance preserves group offsets");
    Check(placed[1].Angle == source[1].Angle && placed[1].Scale == source[1].Scale,
        "prefab preserves rotation and scale");
}
}

int main()
{
    try
    {
        PrefabContract();
        std::cout << "WORLD_PREFAB_OK passed=" << passed << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
