#include "TerrainBrushDocument.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Silver::WorldEditor
{
namespace
{
constexpr int MaximumRadius = 16;
constexpr int MaximumStrength = 64;
constexpr std::size_t MaximumHistory = 64;
bool InMap(int x, int y) { return x >= 0 && y >= 0 && x < TerrainSide && y < TerrainSide; }
bool IsAttributeTool(TerrainBrushTool tool) { return tool == TerrainBrushTool::AddFlag || tool == TerrainBrushTool::RemoveFlag; }
}
void TerrainBrushDocument::Load(std::vector<std::uint16_t> attributes, std::vector<std::uint8_t> heights,
    std::vector<int> protectedCells)
{
    if (attributes.size() != TerrainCells || heights.size() != TerrainCells) throw std::invalid_argument("Cantidad de celdas incorrecta.");
    std::vector<std::uint8_t> protectedMap(TerrainCells, 0);
    for (int index : protectedCells)
    {
        if (index < 0 || index >= TerrainCells) throw std::invalid_argument("Celda protegida fuera del mapa.");
        protectedMap[index] = 1;
    }
    attributes_ = std::move(attributes); heights_ = std::move(heights); protected_ = std::move(protectedMap);
    visited_.assign(TerrainCells, 0); active_.reset(); history_.clear(); cursor_ = 0;
    state_ = ++nextState_; savedState_ = state_; ++revision_;
}
void TerrainBrushDocument::Begin(TerrainBrushSettings settings)
{
    if (Drawing() || heights_.size() != TerrainCells) throw std::logic_error("Termina el pincel anterior o abre un mapa.");
    if (settings.Radius < 0 || settings.Radius > MaximumRadius || settings.Strength < 1 || settings.Strength > MaximumStrength)
        throw std::invalid_argument("Radio o intensidad fuera de limites.");
    if (static_cast<unsigned>(settings.Tool) > static_cast<unsigned>(TerrainBrushTool::RemoveFlag)) throw std::invalid_argument("Pincel desconocido.");
    if (IsAttributeTool(settings.Tool) && settings.Flag != 1 && settings.Flag != 4)
        throw std::invalid_argument("Solo zona segura o bloqueo de paso estan habilitados.");
    settings_ = settings; std::fill(visited_.begin(), visited_.end(), 0);
    startHeights_ = heights_; previousX_ = previousY_ = -1;
    active_ = Stroke{};
}
std::uint8_t TerrainBrushDocument::HeightFor(int x, int y) const
{
    const int initial = startHeights_[y * TerrainSide + x];
    switch (settings_.Tool)
    {
    case TerrainBrushTool::Raise: return static_cast<std::uint8_t>(std::min(255, initial + settings_.Strength));
    case TerrainBrushTool::Lower: return static_cast<std::uint8_t>(std::max(0, initial - settings_.Strength));
    case TerrainBrushTool::Flatten: return settings_.TargetHeight;
    case TerrainBrushTool::Smooth:
    {
        int total = 0, count = 0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            if (InMap(x+dx, y+dy)) { total += startHeights_[(y+dy)*TerrainSide+x+dx]; ++count; }
        const int target = (total + count/2) / count;
        return static_cast<std::uint8_t>(initial + std::clamp(target-initial, -settings_.Strength, settings_.Strength));
    }
    default: return static_cast<std::uint8_t>(initial);
    }
}
void TerrainBrushDocument::Dab(int x, int y)
{
    const int radius = settings_.Radius;
    for (int cy = std::max(0,y-radius); cy <= std::min(TerrainSide-1,y+radius); ++cy)
        for (int cx = std::max(0,x-radius); cx <= std::min(TerrainSide-1,x+radius); ++cx)
        {
            const int index = cy * TerrainSide + cx;
            if ((cx-x)*(cx-x)+(cy-y)*(cy-y) > radius*radius || visited_[index]) continue;
            visited_[index] = 1;
            auto attribute = attributes_[index]; const auto height = HeightFor(cx,cy);
            if (IsAttributeTool(settings_.Tool) && protected_[index]) continue;
            if (settings_.Tool == TerrainBrushTool::AddFlag) attribute |= settings_.Flag;
            if (settings_.Tool == TerrainBrushTool::RemoveFlag) attribute &= static_cast<std::uint16_t>(~settings_.Flag);
            if (attribute == attributes_[index] && height == heights_[index]) continue;
            active_->Cells.push_back({index,attributes_[index],attribute,heights_[index],height});
            attributes_[index] = attribute; heights_[index] = height; ++revision_;
        }
}
void TerrainBrushDocument::Paint(int x, int y)
{
    if (!Drawing()) throw std::logic_error("No hay un trazo activo.");
    if (!InMap(x,y)) { previousX_ = previousY_ = -1; return; }
    if (previousX_ < 0) { Dab(x,y); previousX_=x; previousY_=y; return; }
    int cx=previousX_, cy=previousY_;
    const int dx=std::abs(x-cx), dy=-std::abs(y-cy), sx=cx<x?1:-1, sy=cy<y?1:-1;
    int error=dx+dy;
    while (true)
    {
        Dab(cx,cy); if (cx==x && cy==y) break;
        const int twice=error*2;
        if (twice>=dy) { error+=dy; cx+=sx; }
        if (twice<=dx) { error+=dx; cy+=sy; }
    }
    previousX_=x; previousY_=y;
}
void TerrainBrushDocument::Apply(const Stroke& stroke, bool after)
{
    for (const auto& cell : stroke.Cells)
    {
        attributes_[cell.Index] = after ? cell.AfterAttribute : cell.BeforeAttribute;
        heights_[cell.Index] = after ? cell.AfterHeight : cell.BeforeHeight;
    }
    ++revision_;
}
bool TerrainBrushDocument::Commit()
{
    if (!active_) return false;
    auto stroke=std::move(*active_); active_.reset();
    if (stroke.Cells.empty()) return false;
    history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
    stroke.Before=state_;stroke.After=++nextState_;state_=stroke.After;
    history_.push_back(std::move(stroke));
    if (history_.size()>MaximumHistory) history_.erase(history_.begin());
    cursor_=history_.size(); return true;
}
void TerrainBrushDocument::Cancel() { if (active_) { Apply(*active_,false); active_.reset(); } }
bool TerrainBrushDocument::Undo() { if (!CanUndo()) return false; const auto& s=history_[--cursor_]; Apply(s,false); state_=s.Before; return true; }
bool TerrainBrushDocument::Redo() { if (!CanRedo()) return false; const auto& s=history_[cursor_++]; Apply(s,true); state_=s.After; return true; }
bool TerrainBrushDocument::Dirty() const { return state_!=savedState_ || (active_ && !active_->Cells.empty()); }
void TerrainBrushDocument::MarkSaved() { if (Drawing()) throw std::logic_error("Solta el pincel antes de guardar."); savedState_=state_; }
}
