#pragma once
#include "../core/WorldGateDocument.h"
#include <array>
namespace Silver::WorldEditor
{
class WorldGateUI
{
public:
    void Draw(WorldGateDocument& document, GateId selected);
    void DrawChanges(const WorldGateDocument& document);
private:
    void Read(const WorldGate& gate, std::uint64_t revision);
    WorldGate Value(const WorldGate& original) const;
    GateId selected_=NoGate, target_=NoGate;
    std::uint64_t revision_=0;
    std::array<int,4> entry_{}, exit_{};
    int level_=0, map_=0, direction_=0;
    std::string status_;
    std::uint64_t reviewRevision_=0;
    std::vector<std::string> changes_;
};
}
