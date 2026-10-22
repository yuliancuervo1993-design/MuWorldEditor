#include "WorldSelectionContext.h"
#include <algorithm>

namespace Silver::WorldEditor
{
void WorldSelectionContext::Clear()
{
    ids_.clear();
    active_ = NoObject;
}
void WorldSelectionContext::SelectOnly(ObjectId id)
{
    if (id == NoObject) { Clear(); return; }
    ids_.assign(1, id);
    active_ = id;
}
void WorldSelectionContext::Add(ObjectId id)
{
    if (id == NoObject) return;
    if (!Contains(id)) ids_.push_back(id);
    active_ = id;
}
void WorldSelectionContext::Toggle(ObjectId id)
{
    if (id == NoObject) return;
    if (!Remove(id)) Add(id);
}
bool WorldSelectionContext::Remove(ObjectId id)
{
    const auto found = std::find(ids_.begin(), ids_.end(), id);
    if (found == ids_.end()) return false;
    ids_.erase(found);
    if (active_ == id) active_ = ids_.empty() ? NoObject : ids_.back();
    return true;
}
void WorldSelectionContext::Prune(const WorldObjectDocument& document)
{
    ids_.erase(std::remove_if(ids_.begin(), ids_.end(), [&document](ObjectId id)
    {
        const auto* record = document.Find(id);
        return record == nullptr || record->BaselineOnly;
    }), ids_.end());
    if (ids_.empty()) active_ = NoObject;
    else if (!Contains(active_)) active_ = ids_.back();
}
bool WorldSelectionContext::Contains(ObjectId id) const
{
    return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
}
}
