#include "WorldLayerSidecar.h"
#include <iostream>
#include <stdexcept>
#include <string>

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

template<class Error, class Action>
void Reject(Action action, const char* name)
{
    try { action(); }
    catch (const Error&) { Check(true, name); return; }
    throw std::runtime_error(name);
}

WorldObjectRecord Record(std::int16_t type, float x)
{
    return {type, {x, 200.0f, 30.0f}, {10.0f, 20.0f, 30.0f}, 1.25f};
}

WorldObjectFile Baseline()
{
    WorldObjectFile file;
    file.Map = 1;
    file.Records.push_back(Record(10, 100.0f));
    file.Records.push_back(Record(20, 200.0f));
    file.Records.push_back(Record(20, 200.0f));
    file.Records.push_back(Record(30, 400.0f));
    return file;
}

void RoundTripContract()
{
    auto baseline = Baseline();
    WorldLayerDocument layers;
    const auto decor = layers.Add("Decoracion");
    layers.SetVisible(decor, false);
    layers.SetLocked(decor, true);
    layers.Assign(1, decor);
    layers.Assign(2, decor);
    layers.Assign(3, decor);

    const auto encoded = WorldLayerSidecar::Encode(baseline, layers, 0, 1);
    Check(!encoded.Bytes.empty(), "sidecar encodes bytes");
    Check(encoded.PersistentAssignments == 1, "unique baseline assignment persists");
    Check(encoded.AmbiguousAssignmentsSkipped == 2, "ambiguous duplicate assignments are skipped");

    const auto restored = WorldLayerSidecar::Decode(encoded.Bytes, baseline, 0, 1);
    Check(restored.RestoredAssignments == 1, "sidecar restores unique assignment");
    Check(restored.AmbiguousAssignmentsSkipped == 0, "encoder omits ambiguous keys rather than guessing");
    const auto custom = restored.Layers.Layers().at(1).Id;
    Check(restored.Layers.LayerFor(1) == custom, "unique object returns to custom layer");
    Check(restored.Layers.LayerFor(2) == DefaultWorldLayer
        && restored.Layers.LayerFor(3) == DefaultWorldLayer,
        "ambiguous duplicate objects safely remain in Base");
    Check(!restored.Layers.Find(custom)->Visible && restored.Layers.Find(custom)->Locked,
        "layer visibility and lock round-trip");
}

void IdentityContract()
{
    auto first = Record(55, 123.0f);
    auto same = first;
    auto moved = first;
    moved.Position[0] += 1.0f;
    Check(WorldLayerSidecar::ObjectKey(first) == WorldLayerSidecar::ObjectKey(same),
        "identical baseline records have identical persistent key");
    Check(WorldLayerSidecar::ObjectKey(first) != WorldLayerSidecar::ObjectKey(moved),
        "position change changes persistent key");
}

void ValidationContract()
{
    auto baseline = Baseline();
    WorldLayerDocument layers;
    const auto encoded = WorldLayerSidecar::Encode(baseline, layers, 0, 1);
    Reject<std::invalid_argument>([&] {
        WorldLayerSidecar::Decode(encoded.Bytes, baseline, 1, 1);
    }, "sidecar rejects another map");
    Reject<std::invalid_argument>([&] {
        const std::vector<std::uint8_t> invalid{'{','}'};
        WorldLayerSidecar::Decode(invalid, baseline, 0, 1);
    }, "sidecar rejects incompatible schema");
}
}

int main()
{
    try
    {
        RoundTripContract();
        IdentityContract();
        ValidationContract();
        std::cout << "WORLD_LAYER_SIDECAR_OK passed=" << passed << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
