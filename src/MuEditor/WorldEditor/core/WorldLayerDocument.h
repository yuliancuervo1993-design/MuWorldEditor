#pragma once
#include "WorldObjectDocument.h"
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Silver::WorldEditor
{
using WorldLayerId = std::uint32_t;
constexpr WorldLayerId NoWorldLayer = 0;
constexpr WorldLayerId DefaultWorldLayer = 1;

struct WorldLayer
{
    WorldLayerId Id = NoWorldLayer;
    std::string Name;
    bool Visible = true;
    bool Locked = false;
    bool operator==(const WorldLayer&) const = default;
};

class WorldLayerDocument
{
public:
    WorldLayerDocument();

    void Reset();
    const std::vector<WorldLayer>& Layers() const { return layers_; }
    const WorldLayer* Find(WorldLayerId id) const;

    WorldLayerId Add(std::string name);
    bool Rename(WorldLayerId id, std::string name);
    bool SetVisible(WorldLayerId id, bool visible);
    bool SetLocked(WorldLayerId id, bool locked);
    bool Remove(WorldLayerId id);

    bool Assign(ObjectId object, WorldLayerId layer);
    bool AssignMany(std::span<const ObjectId> objects, WorldLayerId layer);
    WorldLayerId LayerFor(ObjectId object) const;
    bool IsVisible(ObjectId object) const;
    bool IsLocked(ObjectId object) const;
    void Prune(const WorldObjectDocument& document);

    std::uint64_t Revision() const { return revision_; }
    std::uint64_t HistorySerial() const { return historySerial_; }
    bool Dirty() const;
    void MarkSaved();
    bool CanUndo() const { return historyCursor_ != 0; }
    bool CanRedo() const { return historyCursor_ < history_.size(); }
    std::string UndoLabel() const { return CanUndo() ? history_[historyCursor_ - 1].Label : std::string{}; }
    std::string RedoLabel() const { return CanRedo() ? history_[historyCursor_].Label : std::string{}; }
    bool Undo();
    bool Redo();
    void AcceptLoadedState();

private:
    struct Snapshot
    {
        std::vector<WorldLayer> Layers;
        std::unordered_map<ObjectId, WorldLayerId> Assignments;
        WorldLayerId NextId = DefaultWorldLayer + 1;
    };
    struct HistoryEntry
    {
        Snapshot Before;
        Snapshot After;
        std::string Label;
    };
    Snapshot Capture() const;
    void Restore(const Snapshot& snapshot);
    void PushHistory(Snapshot before, std::string label);

    static std::string NormalizeName(std::string name);
    void RequireUniqueName(const std::string& name, WorldLayerId except) const;
    WorldLayer& Require(WorldLayerId id);

    std::vector<WorldLayer> layers_;
    std::unordered_map<ObjectId, WorldLayerId> assignments_;
    WorldLayerId nextId_ = DefaultWorldLayer + 1;
    std::uint64_t revision_ = 0;
    std::uint64_t historySerial_ = 0;
    Snapshot saved_;
    std::vector<HistoryEntry> history_;
    std::size_t historyCursor_ = 0;
};
}
