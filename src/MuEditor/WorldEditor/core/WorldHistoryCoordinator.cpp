#include "WorldHistoryCoordinator.h"
#include <utility>

namespace Silver::WorldEditor
{
void WorldHistoryCoordinator::Reset()
{
    entries_.clear();
    cursor_ = 0;
}

void WorldHistoryCoordinator::Record(WorldHistoryDomain domain, std::string label)
{
    RecordBatch({domain}, std::move(label));
}

void WorldHistoryCoordinator::RecordBatch(std::vector<WorldHistoryDomain> domains, std::string label)
{
    if (domains.empty()) return;
    if (cursor_ < entries_.size())
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());
    if (label.empty()) label = domains.size() == 1 ? DomainName(domains.front()) : "World edit";
    entries_.push_back({std::move(domains), std::move(label)});
    if (entries_.size() > MaximumEntries)
        entries_.erase(entries_.begin());
    cursor_ = entries_.size();
}

const WorldHistoryEntry* WorldHistoryCoordinator::UndoEntry() const
{
    return CanUndo() ? &entries_[cursor_ - 1] : nullptr;
}

const WorldHistoryEntry* WorldHistoryCoordinator::RedoEntry() const
{
    return CanRedo() ? &entries_[cursor_] : nullptr;
}

bool WorldHistoryCoordinator::CommitUndo()
{
    if (!CanUndo()) return false;
    --cursor_;
    return true;
}

bool WorldHistoryCoordinator::CommitRedo()
{
    if (!CanRedo()) return false;
    ++cursor_;
    return true;
}

const char* WorldHistoryCoordinator::DomainName(WorldHistoryDomain domain)
{
    switch (domain)
    {
    case WorldHistoryDomain::Objects: return "Objects";
    case WorldHistoryDomain::Terrain: return "Terrain";
    case WorldHistoryDomain::Effects: return "Effects";
    case WorldHistoryDomain::Layers: return "Layers";
    case WorldHistoryDomain::Regions: return "Regions";
    case WorldHistoryDomain::Triggers: return "Triggers";
    case WorldHistoryDomain::MonsterSpawns: return "MonsterSpawns";
    case WorldHistoryDomain::Gates: return "Gates";
    }
    return "Unknown";
}
}
