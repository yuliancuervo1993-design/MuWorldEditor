#pragma once
#include "WorldPlacementSession.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace PlacementTests
{
using namespace Silver::WorldEditor;
inline int passed = 0, failed = 0;
inline void Check(bool value) { if (!value) throw std::runtime_error("contract not met"); }
inline bool Near(float a, float b) { return std::abs(a - b) < 0.001f; }
inline void Case(const std::string& name, const std::function<void()>& test)
{
    try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& error)
    { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
}
template<class Error, class Action> void Reject(Action action)
{
    try { action(); } catch (const Error&) { return; }
    throw std::runtime_error("expected rejection");
}
inline constexpr WorldPlacementSession::SceneId Scene = 11;
using Status = WorldPlacementSession::Status;
struct Snapshot
{
    std::vector<std::uint8_t> Bytes;
    std::size_t Entries;
    std::uint64_t Revision;
    bool Dirty, Undo, Redo, Preview;
    bool operator==(const Snapshot&) const = default;
};
inline Snapshot Capture(const WorldObjectDocument& doc)
{
    return {WorldObjectCodec::Encode(doc.Export()), doc.Entries().size(), doc.Revision(),
        doc.Dirty(), doc.CanUndo(), doc.CanRedo(), doc.HasPreview()};
}
class Surface final : public WorldPlacementSurface
{
public:
    bool Ready = true, Ground = true, Geometry = true, Slope = false, Throw = false;
    float Height = 100.0f;
    std::optional<float> Minimum;
    mutable float SampledX = -1, SampledY = -1;
    mutable WorldObjectRecord SampledTransform;
    bool ModelReady(std::int16_t type) const override { return Ready && type == 2; }
    bool HeightAt(float x, float y, float& height) const override
    {
        if (Throw) throw std::runtime_error("surface query failed");
        SampledX = x; SampledY = y;
        height = Height + (Slope ? 2.0f * x + 3.0f * y : 0.0f);
        return Ground;
    }
    bool MinimumModelZ(const WorldObjectRecord& transform, float& minimum) const override
    {
        SampledTransform = transform;
        minimum = Minimum.value_or(-2.0f * transform.Scale);
        return Geometry;
    }
};
struct Rig
{
    WorldObjectRecord Original{2, {100, 200, 30}, {7, 13, 345}, 2};
    WorldObjectDocument Document;
    Surface Ground;
    WorldPlacementSession Placement;
    Rig()
    {
        WorldObjectFile file; file.Records.push_back(Original); Document.Load(file);
        Placement.Select(Original, Document, Scene);
    }
    void Target(float x = 126, float y = 176)
    { Placement.UpdateTarget(x, y, Document, Scene, Ground); }
    ObjectId Button(bool down) { return Placement.PrimaryButton(down, Document, Scene, Ground); }
    void Prime() { Target(); Button(false); }
};
}
