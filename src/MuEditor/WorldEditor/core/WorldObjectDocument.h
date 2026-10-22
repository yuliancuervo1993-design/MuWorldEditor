#pragma once
#include "WorldObjectCodec.h"
#include <optional>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
struct WorldObjectEntry
{
    ObjectId Id = NoObject;
    std::optional<WorldObjectRecord> Record;
};

struct WorldObjectUpdate
{
    ObjectId Id = NoObject;
    WorldObjectRecord Record;
};

class WorldObjectDocument
{
public:
    void Load(WorldObjectFile file);
    const std::vector<WorldObjectEntry>& Entries() const { return entries_; }
    const WorldObjectRecord* Find(ObjectId id) const;
    WorldObjectFile Export() const;
    ObjectId Add(const WorldObjectRecord& record);
    std::vector<ObjectId> AddMany(const std::vector<WorldObjectRecord>& records, std::string label);
    void Begin(ObjectId id, std::string label);
    void Preview(const WorldObjectRecord& record);
    bool Commit();
    void Cancel();
    bool Set(ObjectId id, const WorldObjectRecord& record, std::string label);
    bool SetMany(const std::vector<WorldObjectUpdate>& updates, std::string label);
    bool Remove(ObjectId id);
    bool RemoveMany(const std::vector<ObjectId>& ids, std::string label);
    bool Undo();
    bool Redo();
    bool CanUndo() const { return cursor_ != 0 && !pending_; }
    bool CanRedo() const { return cursor_ < history_.size() && !pending_; }
    std::string UndoLabel() const { return CanUndo() ? history_[cursor_ - 1].Label : std::string{}; }
    std::string RedoLabel() const { return CanRedo() ? history_[cursor_].Label : std::string{}; }
    bool HasPreview() const { return pending_.has_value(); }
    bool Dirty() const;
    void MarkSaved();
    std::uint64_t Revision() const { return revision_; }
    std::uint64_t HistorySerial() const { return historySerial_; }
private:
    struct ChangeItem
    {
        ObjectId Id = NoObject;
        std::optional<WorldObjectRecord> Before;
        std::optional<WorldObjectRecord> After;
    };
    struct Change
    {
        std::vector<ChangeItem> Items;
        std::string Label;
        std::uint64_t BeforeState = 0;
        std::uint64_t AfterState = 0;
    };
    WorldObjectEntry& Entry(ObjectId id);
    void Push(Change change);
    WorldObjectFile header_;
    std::vector<WorldObjectEntry> entries_;
    std::vector<Change> history_;
    std::optional<Change> pending_;
    std::size_t cursor_ = 0;
    std::uint64_t revision_ = 0, historySerial_ = 0, state_ = 0, nextState_ = 0, savedState_ = 0;
};
}
