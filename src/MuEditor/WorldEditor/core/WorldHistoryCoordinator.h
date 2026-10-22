#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
enum class WorldHistoryDomain { Objects, Terrain, Effects, Layers, Regions, Triggers, MonsterSpawns, Gates };

struct WorldHistoryEntry
{
    std::vector<WorldHistoryDomain> Domains;
    std::string Label;
};

class WorldHistoryCoordinator
{
public:
    void Reset();
    void Record(WorldHistoryDomain domain, std::string label);
    void RecordBatch(std::vector<WorldHistoryDomain> domains, std::string label);
    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < entries_.size(); }
    const WorldHistoryEntry* UndoEntry() const;
    const WorldHistoryEntry* RedoEntry() const;
    bool CommitUndo();
    bool CommitRedo();
    std::size_t Cursor() const { return cursor_; }
    const std::vector<WorldHistoryEntry>& Entries() const { return entries_; }
    static const char* DomainName(WorldHistoryDomain domain);
private:
    static constexpr std::size_t MaximumEntries = 256;
    std::vector<WorldHistoryEntry> entries_;
    std::size_t cursor_ = 0;
};
}
