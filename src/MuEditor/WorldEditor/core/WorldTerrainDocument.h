#pragma once
#include "TerrainAttributeCodec.h"
#include "TerrainHeightFile.h"
#include "TerrainMappingCodec.h"
#include <optional>
namespace Silver::WorldEditor
{
enum class TerrainBrushMode
{
    Raise, Lower, Flatten, Smooth, AddFlags, RemoveFlags,
    PaintLayer1, PaintLayer2, BlendMaterial, EraseBlend, Noise, Terrace, Erode, FillLayer1, Ramp
};
struct TerrainBrush
{
    TerrainBrushMode Mode = TerrainBrushMode::Raise;
    int Radius = 3;
    int Strength = 4;
    std::uint8_t FlattenHeight = 0;
    std::uint16_t Flags = 1;
    int Material = 0;
    int BlendAlpha = 128;
    float Falloff = 0.0f;
    void Validate() const;
};
class WorldTerrainDocument
{
public:
    void Load(TerrainAttributeFile attributes, std::vector<std::uint8_t> heights, int protectedCell = -1);
    void Load(TerrainAttributeFile attributes, std::vector<std::uint8_t> heights,
        const TerrainMappingFile& mapping, int protectedCell = -1);
    void Begin(TerrainBrush brush);
    bool Stamp(int x, int y);
    bool Ramp(int x1, int y1, int x2, int y2, int halfWidth);
    bool Commit();
    void Cancel();
    void BreakStrokePath() { previous_.reset(); }
    bool Undo();
    bool Redo();
    bool Active() const { return active_; }
    bool CanUndo() const { return !active_ && cursor_ > 0; }
    bool CanRedo() const { return !active_ && cursor_ < history_.size(); }
    bool Dirty() const { return state_ != savedState_ || (active_ && !pending_.empty()); }
    void MarkSaved();
    const TerrainAttributeFile& Attributes() const { return attributes_; }
    const std::vector<std::uint8_t>& Heights() const { return heights_; }
    const std::optional<TerrainMappingFile>& Mapping() const { return mapping_; }
    float Height(int x, int y) const;
    std::uint16_t Flags(int x, int y) const;
    std::uint64_t Revision() const { return revision_; }
    std::uint64_t HistorySerial() const { return historySerial_; }
    std::size_t ChangedInStroke() const { return pending_.size(); }
private:
    void Load(TerrainAttributeFile attributes, std::vector<std::uint8_t> heights,
        const TerrainMappingFile* mapping, int protectedCell);
    struct Delta
    {
        int Index;
        std::uint16_t BeforeFlags, AfterFlags;
        std::uint8_t BeforeHeight, AfterHeight;
        std::uint8_t BeforeLayer1, AfterLayer1;
        std::uint8_t BeforeLayer2, AfterLayer2;
        std::uint8_t BeforeAlpha, AfterAlpha;
    };
    struct Stroke { std::vector<Delta> Deltas; std::uint64_t Before, After; };
    bool Circle(int x, int y);
    bool Cell(int index);
    void Apply(const Stroke& stroke, bool forward);
    TerrainAttributeFile attributes_;
    std::vector<std::uint8_t> heights_, startHeights_, visited_;
    std::optional<TerrainMappingFile> mapping_;
    std::vector<Delta> pending_;
    std::vector<Stroke> history_;
    TerrainBrush brush_;
    std::optional<std::pair<int, int>> previous_;
    bool active_ = false;
    int protectedCell_ = -1;
    int effectiveStrength_ = 1;
    std::size_t cursor_ = 0, storedChanges_ = 0;
    std::uint64_t state_ = 0, nextState_ = 0, savedState_ = 0, revision_ = 0, historySerial_ = 0;
};
}
