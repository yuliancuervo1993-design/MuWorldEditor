#include "WorldPlacementSession.h"
#include <cmath>
#include <iostream>
using namespace Silver::WorldEditor;
int main()
{
    constexpr int Samples = 20000;
    WorldObjectDocument document; document.Load(WorldObjectFile{});
    WorldPlacementSession placement;
    int failures = 0;
    for (int i = 0; i <= Samples; ++i)
    {
        WorldObjectRecord prototype;
        prototype.Scale = static_cast<float>(MinimumScale * std::pow(10000.0, static_cast<double>(i) / Samples));
        if (prototype.Scale < MinimumScale || prototype.Scale > MaximumScale) continue;
        placement.Select(prototype, document, 1);
        const auto bounds = placement.ScalePercentRange();
        try { placement.SetScalePercent(bounds.first); placement.SetScalePercent(bounds.second); }
        catch (const std::exception&) { ++failures; }
    }
    std::cout << "SCALE_ENDPOINT_SWEEP intervals=" << Samples << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
