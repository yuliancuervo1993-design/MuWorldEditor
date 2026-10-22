#pragma once
#include "WorldRegionDocument.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
using TriggerId = std::uint64_t;
constexpr TriggerId NoTrigger = 0;

struct WorldTrigger
{
    TriggerId Id = NoTrigger;
    std::string Name;
    std::string Event;
    RegionId Region = NoRegion;
    std::vector<std::string> Conditions;
    std::vector<std::string> Actions;
    bool Enabled = true;
    bool operator==(const WorldTrigger&) const = default;
};

class WorldTriggerDocument
{
public:
    static constexpr const char* Schema = "silver-world-editor-triggers-v1";
    static constexpr const char* FileName = "triggers.json";
    void Reset();
    TriggerId Add(std::string name, std::string event, RegionId region);
    bool Remove(TriggerId id);
    bool AddCondition(TriggerId id, std::string condition);
    bool AddAction(TriggerId id, std::string action);
    bool Set(TriggerId id, const WorldTrigger& value, std::string label = "Edit Trigger");
    bool RemoveCondition(TriggerId id, std::size_t index);
    bool RemoveAction(TriggerId id, std::size_t index);
    bool MoveCondition(TriggerId id, std::size_t index, int direction);
    bool MoveAction(TriggerId id, std::size_t index, int direction);
    const WorldTrigger* Find(TriggerId id) const;
    const std::vector<WorldTrigger>& Entries() const { return entries_; }
    std::vector<std::uint8_t> Encode(int mapId) const;
    void Decode(std::span<const std::uint8_t> bytes, int expectedMap);
    bool Dirty() const { return state_ != savedState_; }
    void MarkSaved() { savedState_ = state_; savedEntries_ = entries_; }
    const std::vector<WorldTrigger>& SavedEntries() const { return savedEntries_; }
    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < history_.size(); }
    bool Undo();
    bool Redo();
    std::string UndoLabel() const { return CanUndo() ? history_[cursor_ - 1].Label : std::string{}; }
    std::uint64_t HistorySerial() const { return historySerial_; }
private:
    struct Snapshot { std::vector<WorldTrigger> Entries; TriggerId NextId = 1; };
    struct HistoryEntry { Snapshot Before; Snapshot After; std::string Label; std::uint64_t BeforeState = 0, AfterState = 0; };
    static std::string Checked(std::string value, const char* field);
    Snapshot Capture() const { return {entries_, nextId_}; }
    void Restore(const Snapshot& snapshot);
    void PushHistory(Snapshot before, std::string label);
    std::vector<WorldTrigger> entries_;
    std::vector<WorldTrigger> savedEntries_;
    TriggerId nextId_ = 1;
    std::vector<HistoryEntry> history_;
    std::size_t cursor_ = 0;
    std::uint64_t revision_ = 0, historySerial_ = 0, state_ = 0, nextState_ = 0, savedState_ = 0;
};
}
