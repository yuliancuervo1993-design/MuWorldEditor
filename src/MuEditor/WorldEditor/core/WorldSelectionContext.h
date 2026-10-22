#pragma once
#include "WorldObjectDocument.h"
#include <cstddef>
#include <vector>

namespace Silver::WorldEditor
{
class WorldSelectionContext
{
public:
    void Clear();
    void SelectOnly(ObjectId id);
    void Add(ObjectId id);
    void Toggle(ObjectId id);
    bool Remove(ObjectId id);
    void Prune(const WorldObjectDocument& document);

    bool Contains(ObjectId id) const;
    bool Empty() const { return ids_.empty(); }
    std::size_t Count() const { return ids_.size(); }
    ObjectId Active() const { return active_; }
    const std::vector<ObjectId>& Items() const { return ids_; }

private:
    std::vector<ObjectId> ids_;
    ObjectId active_ = NoObject;
};
}
