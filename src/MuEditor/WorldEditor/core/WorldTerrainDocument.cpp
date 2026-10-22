#include "WorldTerrainDocument.h"
#include "TerrainBrushFootprint.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace
{
constexpr int MaximumRadius = 16;
constexpr std::uint16_t SupportedBrushFlags = 1 | 4 | 8 | 16 | 32 | 64;
constexpr std::size_t MaximumHistoryChanges = TerrainCells * 4;
constexpr std::size_t MaximumHistory = 128;
int Index(int x, int y)
{
    if (x < 0 || y < 0 || x >= TerrainSide || y >= TerrainSide) throw std::out_of_range("Celda fuera del mapa.");
    return y * TerrainSide + x;
}
}
void TerrainBrush::Validate() const
{
    if (Mode < TerrainBrushMode::Raise || Mode > TerrainBrushMode::Ramp) throw std::invalid_argument("Herramienta desconocida.");
    if (Radius < 0 || Radius > MaximumRadius || Strength < 1 || Strength > 64)
        throw std::invalid_argument("Radio o fuerza del pincel fuera de limites.");
    if (!std::isfinite(Falloff) || Falloff < 0.0f || Falloff > 1.0f)
        throw std::invalid_argument("Falloff del pincel fuera de limites.");
    if ((Mode == TerrainBrushMode::AddFlags || Mode == TerrainBrushMode::RemoveFlags)
        && (Flags == 0 || (Flags & ~SupportedBrushFlags) != 0))
        throw std::invalid_argument("Flag no compatible con el cargador del cliente.");
    if ((Mode == TerrainBrushMode::PaintLayer1 || Mode == TerrainBrushMode::PaintLayer2
        || Mode == TerrainBrushMode::BlendMaterial || Mode == TerrainBrushMode::FillLayer1)
        && (Material < 0 || Material > 255))
        throw std::invalid_argument("Material fuera de rango: debe estar entre 0 y 255.");
    if (Mode == TerrainBrushMode::BlendMaterial && (BlendAlpha < 0 || BlendAlpha > 255))
        throw std::invalid_argument("Alpha de mezcla fuera de rango: debe estar entre 0 y 255.");
}
void WorldTerrainDocument::Load(TerrainAttributeFile attributes, std::vector<std::uint8_t> heights, int protectedCell)
{
    Load(std::move(attributes), std::move(heights), nullptr, protectedCell);
}
void WorldTerrainDocument::Load(TerrainAttributeFile attributes, std::vector<std::uint8_t> heights,
    const TerrainMappingFile& mapping, int protectedCell)
{
    Load(std::move(attributes), std::move(heights), &mapping, protectedCell);
}
void WorldTerrainDocument::Load(TerrainAttributeFile attributes, std::vector<std::uint8_t> heights,
    const TerrainMappingFile* mapping, int protectedCell)
{
    TerrainAttributeCodec::Encode(attributes);
    if (heights.size() != TerrainCells || protectedCell < -1 || protectedCell >= TerrainCells)
        throw std::invalid_argument("Cantidad de alturas o control invalida.");
    if (mapping && mapping->Map == 0)
        throw std::invalid_argument("El mapa de materiales no tiene un numero valido.");
    attributes_ = std::move(attributes); heights_ = std::move(heights);
    if (mapping) mapping_ = *mapping;
    else mapping_.reset();
    protectedCell_ = protectedCell; visited_.assign(TerrainCells, 0);
    history_.clear(); pending_.clear(); startHeights_.clear(); previous_.reset();
    active_ = false; cursor_ = storedChanges_ = 0; state_ = ++nextState_; savedState_ = state_; ++revision_;
}
void WorldTerrainDocument::Begin(TerrainBrush brush)
{
    if (active_ || heights_.size() != TerrainCells) throw std::logic_error("Terreno no preparado o pincelada activa.");
    brush.Validate(); brush_ = brush;
    const bool materialMode = brush_.Mode == TerrainBrushMode::PaintLayer1
        || brush_.Mode == TerrainBrushMode::PaintLayer2
        || brush_.Mode == TerrainBrushMode::BlendMaterial
        || brush_.Mode == TerrainBrushMode::EraseBlend
        || brush_.Mode == TerrainBrushMode::FillLayer1;
    if (materialMode && !mapping_)
        throw std::logic_error("El mapa de materiales no fue cargado por el runtime.");
    pending_.clear(); std::fill(visited_.begin(), visited_.end(), static_cast<std::uint8_t>(0)); previous_.reset();
    startHeights_ = heights_; active_ = true;
}
bool WorldTerrainDocument::Cell(int index)
{
    if (visited_[index]) return false;
    visited_[index] = 1;
    const auto beforeHeight = heights_[index]; const auto beforeFlags = attributes_.Cells[index];
    const auto beforeLayer1 = mapping_ ? mapping_->Layer1[index] : std::uint8_t{0};
    const auto beforeLayer2 = mapping_ ? mapping_->Layer2[index] : std::uint8_t{0};
    const auto beforeAlpha = mapping_ ? mapping_->Alpha[index] : std::uint8_t{0};
    int height = beforeHeight; auto flags = beforeFlags;
    auto afterLayer1 = beforeLayer1, afterLayer2 = beforeLayer2, afterAlpha = beforeAlpha;
    switch (brush_.Mode)
    {
    case TerrainBrushMode::Raise: height += effectiveStrength_; break;
    case TerrainBrushMode::Lower: height -= effectiveStrength_; break;
    case TerrainBrushMode::Flatten: height = brush_.FlattenHeight; break;
    case TerrainBrushMode::Smooth:
    {
        int total = 0, samples = 0, x = index % TerrainSide, y = index / TerrainSide;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            if (x + dx >= 0 && y + dy >= 0 && x + dx < TerrainSide && y + dy < TerrainSide)
            { total += startHeights_[Index(x + dx, y + dy)]; ++samples; }
        height = (total + samples / 2) / samples; break;
    }
    case TerrainBrushMode::AddFlags: if (index != protectedCell_) flags |= brush_.Flags; break;
    case TerrainBrushMode::RemoveFlags: if (index != protectedCell_) flags &= static_cast<std::uint16_t>(~brush_.Flags); break;
    case TerrainBrushMode::PaintLayer1: afterLayer1 = static_cast<std::uint8_t>(brush_.Material); break;
    case TerrainBrushMode::PaintLayer2: afterLayer2 = static_cast<std::uint8_t>(brush_.Material); break;
    case TerrainBrushMode::BlendMaterial:
        afterLayer2 = static_cast<std::uint8_t>(brush_.Material);
        afterAlpha = static_cast<std::uint8_t>(brush_.BlendAlpha);
        break;
    case TerrainBrushMode::EraseBlend: afterLayer2 = 255; afterAlpha = 0; break;
    case TerrainBrushMode::Noise:
    {
        const int x=index%TerrainSide,y=index/TerrainSide;
        std::uint32_t hash=static_cast<std::uint32_t>(x)*73856093u ^ static_cast<std::uint32_t>(y)*19349663u;
        hash^=hash>>13; hash*=1274126177u; hash^=hash>>16;
        const int span=effectiveStrength_*2+1;
        height+=static_cast<int>(hash%static_cast<std::uint32_t>(span))-effectiveStrength_;
        break;
    }
    case TerrainBrushMode::Terrace:
    {
        const int step=std::max(1,brush_.Strength);
        height=((static_cast<int>(beforeHeight)+step/2)/step)*step;
        break;
    }
    case TerrainBrushMode::Erode:
    {
        int total = 0, samples = 0;
        const int x = index % TerrainSide, y = index / TerrainSide;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0) continue;
            if (x + dx < 0 || y + dy < 0 || x + dx >= TerrainSide || y + dy >= TerrainSide) continue;
            total += startHeights_[Index(x + dx, y + dy)];
            ++samples;
        }
        if (samples > 0)
        {
            const int neighborAverage = (total + samples / 2) / samples;
            if (static_cast<int>(beforeHeight) > neighborAverage)
                height = std::max(neighborAverage, static_cast<int>(beforeHeight) - effectiveStrength_);
        }
        break;
    }
    case TerrainBrushMode::FillLayer1:
        afterLayer1 = static_cast<std::uint8_t>(brush_.Material);
        break;
    case TerrainBrushMode::Ramp:
        break;
    }
    const auto afterHeight = static_cast<std::uint8_t>(std::clamp(height, 0, 255));
    if (beforeHeight == afterHeight && beforeFlags == flags && beforeLayer1 == afterLayer1
        && beforeLayer2 == afterLayer2 && beforeAlpha == afterAlpha) return false;
    pending_.push_back({index, beforeFlags, flags, beforeHeight, afterHeight,
        beforeLayer1, afterLayer1, beforeLayer2, afterLayer2, beforeAlpha, afterAlpha});
    heights_[index] = afterHeight; attributes_.Cells[index] = flags;
    if (mapping_)
    {
        mapping_->Layer1[index] = afterLayer1;
        mapping_->Layer2[index] = afterLayer2;
        mapping_->Alpha[index] = afterAlpha;
    }
    return true;
}
bool WorldTerrainDocument::Circle(int x, int y)
{
    bool changed = false;
    const int radius = brush_.Radius;
    for (int cy = std::max(0, y - radius); cy <= std::min(TerrainSide - 1, y + radius); ++cy)
        for (int cx = std::max(0, x - radius); cx <= std::min(TerrainSide - 1, x + radius); ++cx)
            if (TerrainBrushFootprint::Contains(cx, cy, x, y, radius))
            {
                if (radius > 0 && brush_.Falloff > 0.0f)
                {
                    const float dx = static_cast<float>(cx - x);
                    const float dy = static_cast<float>(cy - y);
                    const float normalized = std::clamp(std::sqrt(dx * dx + dy * dy) / static_cast<float>(radius), 0.0f, 1.0f);
                    const float weight = 1.0f - brush_.Falloff * normalized;
                    effectiveStrength_ = std::max(0, static_cast<int>(std::lround(brush_.Strength * weight)));
                }
                else effectiveStrength_ = brush_.Strength;
                changed = Cell(Index(cx, cy)) || changed;
            }
    effectiveStrength_ = brush_.Strength;
    return changed;
}
bool WorldTerrainDocument::Stamp(int x, int y)
{
    if (!active_) throw std::logic_error("No hay pincelada activa.");
    const int initialIndex = Index(x, y);
    if (brush_.Mode == TerrainBrushMode::FillLayer1)
    {
        if (!mapping_) throw std::logic_error("El mapa de materiales no fue cargado por el runtime.");
        const auto target = mapping_->Layer1[initialIndex];
        const auto replacement = static_cast<std::uint8_t>(brush_.Material);
        if (target == replacement) return false;

        std::vector<int> queue;
        queue.reserve(TerrainCells);
        std::vector<std::uint8_t> queued(TerrainCells, 0);
        queue.push_back(initialIndex);
        queued[initialIndex] = 1;
        bool changed = false;
        for (std::size_t cursor = 0; cursor < queue.size(); ++cursor)
        {
            const int index = queue[cursor];
            if (mapping_->Layer1[index] != target) continue;
            changed = Cell(index) || changed;
            const int cx = index % TerrainSide, cy = index / TerrainSide;
            constexpr int offsets[4][2]{{1,0},{-1,0},{0,1},{0,-1}};
            for (const auto& offset : offsets)
            {
                const int nx = cx + offset[0], ny = cy + offset[1];
                if (nx < 0 || ny < 0 || nx >= TerrainSide || ny >= TerrainSide) continue;
                const int next = Index(nx, ny);
                if (!queued[next] && mapping_->Layer1[next] == target)
                {
                    queued[next] = 1;
                    queue.push_back(next);
                }
            }
        }
        previous_ = std::pair{x, y};
        if (changed) ++revision_;
        return changed;
    }

    bool changed = false;
    auto [cx, cy] = previous_.value_or(std::pair{x, y});
    const int dx = std::abs(x - cx), dy = -std::abs(y - cy);
    const int sx = cx < x ? 1 : -1, sy = cy < y ? 1 : -1;
    int error = dx + dy;
    while (true)
    {
        changed = Circle(cx, cy) || changed;
        if (cx == x && cy == y) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; cx += sx; }
        if (twice <= dx) { error += dx; cy += sy; }
    }
    previous_ = std::pair{x, y};
    if (changed) ++revision_;
    return changed;
}
bool WorldTerrainDocument::Ramp(int x1, int y1, int x2, int y2, int halfWidth)
{
    if (active_ || heights_.size() != TerrainCells)
        throw std::logic_error("Terreno no preparado o pincelada activa.");
    Index(x1, y1); Index(x2, y2);
    if (halfWidth < 0 || halfWidth > MaximumRadius)
        throw std::invalid_argument("Ancho de rampa fuera de limites.");
    if (x1 == x2 && y1 == y2)
        throw std::invalid_argument("La rampa necesita dos puntos distintos.");

    const int startHeight = heights_[Index(x1, y1)];
    const int endHeight = heights_[Index(x2, y2)];
    const float vx = static_cast<float>(x2 - x1);
    const float vy = static_cast<float>(y2 - y1);
    const float lengthSquared = vx * vx + vy * vy;
    const float width = std::max(0.5f, static_cast<float>(halfWidth));

    pending_.clear();
    const int minX = std::max(0, std::min(x1, x2) - halfWidth);
    const int maxX = std::min(TerrainSide - 1, std::max(x1, x2) + halfWidth);
    const int minY = std::max(0, std::min(y1, y2) - halfWidth);
    const int maxY = std::min(TerrainSide - 1, std::max(y1, y2) + halfWidth);

    for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x)
    {
        const float px = static_cast<float>(x - x1);
        const float py = static_cast<float>(y - y1);
        const float t = std::clamp((px * vx + py * vy) / lengthSquared, 0.0f, 1.0f);
        const float closestX = static_cast<float>(x1) + t * vx;
        const float closestY = static_cast<float>(y1) + t * vy;
        const float dx = static_cast<float>(x) - closestX;
        const float dy = static_cast<float>(y) - closestY;
        if (std::sqrt(dx * dx + dy * dy) > width) continue;

        const int index = Index(x, y);
        const auto beforeHeight = heights_[index];
        const auto afterHeight = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(std::lround(startHeight + (endHeight - startHeight) * t)), 0, 255));
        if (beforeHeight == afterHeight) continue;
        const auto flags = attributes_.Cells[index];
        const auto layer1 = mapping_ ? mapping_->Layer1[index] : std::uint8_t{0};
        const auto layer2 = mapping_ ? mapping_->Layer2[index] : std::uint8_t{0};
        const auto alpha = mapping_ ? mapping_->Alpha[index] : std::uint8_t{0};
        pending_.push_back({index, flags, flags, beforeHeight, afterHeight,
            layer1, layer1, layer2, layer2, alpha, alpha});
        heights_[index] = afterHeight;
    }

    if (pending_.empty()) return false;
    active_ = true;
    ++revision_;
    return Commit();
}

bool WorldTerrainDocument::Commit()
{
    if (!active_) return false;
    active_ = false; previous_.reset(); startHeights_.clear();
    if (pending_.empty()) return false;
    for (std::size_t i = cursor_; i < history_.size(); ++i) storedChanges_ -= history_[i].Deltas.size();
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_), history_.end());
    storedChanges_ += pending_.size(); history_.push_back({std::move(pending_), state_, ++nextState_});
    state_ = nextState_;
    while (history_.size() > 1 && (history_.size() > MaximumHistory || storedChanges_ > MaximumHistoryChanges))
    { storedChanges_ -= history_.front().Deltas.size(); history_.erase(history_.begin()); }
    cursor_ = history_.size(); ++revision_; ++historySerial_; return true;
}
void WorldTerrainDocument::Cancel()
{
    if (!active_) return;
    for (const auto& delta : pending_)
    {
        heights_[delta.Index] = delta.BeforeHeight;
        attributes_.Cells[delta.Index] = delta.BeforeFlags;
        if (mapping_)
        {
            mapping_->Layer1[delta.Index] = delta.BeforeLayer1;
            mapping_->Layer2[delta.Index] = delta.BeforeLayer2;
            mapping_->Alpha[delta.Index] = delta.BeforeAlpha;
        }
    }
    pending_.clear(); previous_.reset(); active_ = false; startHeights_.clear(); ++revision_;
}
void WorldTerrainDocument::Apply(const Stroke& stroke, bool forward)
{
    for (const auto& delta : stroke.Deltas)
    {
        heights_[delta.Index] = forward ? delta.AfterHeight : delta.BeforeHeight;
        attributes_.Cells[delta.Index] = forward ? delta.AfterFlags : delta.BeforeFlags;
        if (mapping_)
        {
            mapping_->Layer1[delta.Index] = forward ? delta.AfterLayer1 : delta.BeforeLayer1;
            mapping_->Layer2[delta.Index] = forward ? delta.AfterLayer2 : delta.BeforeLayer2;
            mapping_->Alpha[delta.Index] = forward ? delta.AfterAlpha : delta.BeforeAlpha;
        }
    }
    state_ = forward ? stroke.After : stroke.Before; ++revision_;
}
bool WorldTerrainDocument::Undo() { if (!CanUndo()) return false; Apply(history_[--cursor_], false); return true; }
bool WorldTerrainDocument::Redo() { if (!CanRedo()) return false; Apply(history_[cursor_++], true); return true; }
void WorldTerrainDocument::MarkSaved()
{
    if (active_) throw std::logic_error("Termina la pincelada antes de guardar.");
    savedState_ = state_;
}
float WorldTerrainDocument::Height(int x, int y) const { return heights_.at(Index(x, y)) * HeightUnit; }
std::uint16_t WorldTerrainDocument::Flags(int x, int y) const { return attributes_.Cells.at(Index(x, y)); }
}
