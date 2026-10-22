#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
using RegionId = std::uint64_t;
constexpr RegionId NoRegion = 0;

enum class WorldRegionShape { Rectangle, Circle };

struct WorldRegion
{
    RegionId Id = NoRegion;
    std::string Name;
    WorldRegionShape Shape = WorldRegionShape::Rectangle;
    float X1 = 0.0f;
    float Y1 = 0.0f;
    float X2 = 0.0f;
    float Y2 = 0.0f;
    float Radius = 0.0f;
    std::string Tag;
    bool operator==(const WorldRegion&) const = default;
};

class WorldRegionDocument
{
public:
    static constexpr const char* Schema = "silver-world-editor-regions-v1";
    static constexpr const char* FileName = "regions.json";

    void Reset();
    RegionId AddRectangle(std::string name, float x1, float y1, float x2, float y2, std::string tag = {});
    RegionId AddCircle(std::string name, float x, float y, float radius, std::string tag = {});
    bool Set(RegionId id, const WorldRegion& value, std::string label = "Edit Region");
    bool Remove(RegionId id);
    const WorldRegion* Find(RegionId id) const;
    const std::vector<WorldRegion>& Entries() const { return entries_; }

    void BeginEdit(RegionId id, std::string label);
    bool Preview(const WorldRegion& value);
    bool CommitEdit();
    void CancelEdit();
    bool Editing() const { return pending_.has_value(); }

    bool Undo();
    bool Redo();
    bool CanUndo() const { return !pending_ && cursor_ > 0; }
    bool CanRedo() const { return !pending_ && cursor_ < history_.size(); }
    std::string UndoLabel() const { return CanUndo() ? history_[cursor_ - 1].Label : std::string{}; }
    std::string RedoLabel() const { return CanRedo() ? history_[cursor_].Label : std::string{}; }
    std::uint64_t HistorySerial() const { return historySerial_; }
    std::uint64_t Revision() const { return revision_; }

    std::vector<std::uint8_t> Encode(int mapId) const;
    void Decode(std::span<const std::uint8_t> bytes, int expectedMap);
    bool Dirty() const { return state_ != savedState_ || pending_.has_value(); }
    void MarkSaved() { savedState_ = state_; savedEntries_ = entries_; }
    const std::vector<WorldRegion>& SavedEntries() const { return savedEntries_; }

private:
    struct Change
    {
        RegionId Id = NoRegion;
        std::optional<WorldRegion> Before;
        std::optional<WorldRegion> After;
        std::string Label;
        std::uint64_t BeforeState = 0;
        std::uint64_t AfterState = 0;
    };
    static void Validate(const WorldRegion& region);
    void Push(Change change);
    void Apply(const Change& change, bool forward);

    std::vector<WorldRegion> entries_;
    std::vector<WorldRegion> savedEntries_;
    RegionId nextId_ = 1;
    std::vector<Change> history_;
    std::optional<Change> pending_;
    std::size_t cursor_ = 0;
    std::uint64_t revision_ = 0;
    std::uint64_t historySerial_ = 0;
    std::uint64_t state_ = 0;
    std::uint64_t nextState_ = 0;
    std::uint64_t savedState_ = 0;
};
}
