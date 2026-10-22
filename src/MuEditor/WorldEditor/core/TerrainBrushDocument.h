#pragma once
#include "TerrainAttributeCodec.h"
#include <optional>
#include <span>

namespace Silver::WorldEditor
{
enum class TerrainBrushTool { Raise, Lower, Flatten, Smooth, AddFlag, RemoveFlag };
struct TerrainBrushSettings
{
    TerrainBrushTool Tool = TerrainBrushTool::Raise;
    int Radius = 2;
    int Strength = 4;
    std::uint8_t TargetHeight = 0;
    std::uint16_t Flag = 1;
};
class TerrainBrushDocument
{
public:
    void Load(std::vector<std::uint16_t> attributes, std::vector<std::uint8_t> heights,
        std::vector<int> protectedAttributeCells = {});
    const auto& Attributes() const { return attributes_; }
    const auto& Heights() const { return heights_; }
    void Begin(TerrainBrushSettings settings);
    void Paint(int x, int y);
    bool Commit();
    void Cancel();
    bool Undo();
    bool Redo();
    bool Drawing() const { return active_.has_value(); }
    bool CanUndo() const { return !Drawing() && cursor_ > 0; }
    bool CanRedo() const { return !Drawing() && cursor_ < history_.size(); }
    bool Dirty() const;
    void MarkSaved();
    std::uint64_t Revision() const { return revision_; }
private:
    struct CellChange { int Index; std::uint16_t BeforeAttribute, AfterAttribute; std::uint8_t BeforeHeight, AfterHeight; };
    struct Stroke { std::vector<CellChange> Cells; std::uint64_t Before = 0, After = 0; };
    void Dab(int x, int y);
    void Apply(const Stroke& stroke, bool after);
    std::uint8_t HeightFor(int x, int y) const;
    std::vector<std::uint16_t> attributes_;
    std::vector<std::uint8_t> heights_, startHeights_, visited_, protected_;
    std::vector<Stroke> history_;
    std::optional<Stroke> active_;
    TerrainBrushSettings settings_;
    int previousX_ = -1, previousY_ = -1;
    std::size_t cursor_ = 0;
    std::uint64_t revision_ = 0, state_ = 0, savedState_ = 0, nextState_ = 0;
};
}
