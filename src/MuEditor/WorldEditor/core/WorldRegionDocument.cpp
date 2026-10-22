#include "WorldRegionDocument.h"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t MaximumRegions = 2048;
constexpr std::size_t MaximumBytes = 1024 * 1024;
constexpr std::size_t MaximumText = 96;
constexpr std::size_t MaximumHistory = 256;

std::string Checked(std::string value, const char* field)
{
    if (value.empty() || value.size() > MaximumText)
        throw std::invalid_argument(std::string("Region: ") + field + " invalido.");
    return value;
}
}

void WorldRegionDocument::Validate(const WorldRegion& region)
{
    const bool finite = std::isfinite(region.X1) && std::isfinite(region.Y1)
        && std::isfinite(region.X2) && std::isfinite(region.Y2) && std::isfinite(region.Radius);
    if (region.Id == NoRegion || !finite || region.Name.empty() || region.Name.size() > MaximumText
        || region.Tag.size() > MaximumText)
        throw std::invalid_argument("Region invalida.");
    if (region.Shape == WorldRegionShape::Rectangle && (region.X2 < region.X1 || region.Y2 < region.Y1))
        throw std::invalid_argument("Region rectangular invertida.");
    if (region.Shape == WorldRegionShape::Circle && region.Radius <= 0.0f)
        throw std::invalid_argument("Region circular sin radio positivo.");
}

void WorldRegionDocument::Reset()
{
    entries_.clear();
    history_.clear();
    pending_.reset();
    nextId_ = 1;
    cursor_ = 0;
    state_ = nextState_ = savedState_ = 0; savedEntries_ = entries_;
    ++revision_;
}

void WorldRegionDocument::Push(Change change)
{
    if (cursor_ < history_.size()) history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_), history_.end());
    change.BeforeState = state_;
    change.AfterState = ++nextState_;
    history_.push_back(std::move(change));
    if (history_.size() > MaximumHistory) history_.erase(history_.begin());
    cursor_ = history_.size();
    state_ = history_.back().AfterState;
    ++historySerial_;
    ++revision_;
}

void WorldRegionDocument::Apply(const Change& change, bool forward)
{
    const auto& value = forward ? change.After : change.Before;
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [id = change.Id](const WorldRegion& region) { return region.Id == id; });
    if (value)
    {
        Validate(*value);
        if (found == entries_.end()) entries_.push_back(*value);
        else *found = *value;
        nextId_ = std::max(nextId_, value->Id + 1);
    }
    else if (found != entries_.end()) entries_.erase(found);
    state_ = forward ? change.AfterState : change.BeforeState;
    ++revision_;
}

RegionId WorldRegionDocument::AddRectangle(std::string name, float x1, float y1, float x2, float y2, std::string tag)
{
    if (entries_.size() >= MaximumRegions) throw std::length_error("Limite de Regions alcanzado.");
    WorldRegion region{nextId_++, Checked(std::move(name), "nombre"), WorldRegionShape::Rectangle,
        x1,y1,x2,y2,0.0f,std::move(tag)};
    Validate(region);
    entries_.push_back(region);
    Push({region.Id, std::nullopt, region, "Create Rectangle Region"});
    return region.Id;
}

RegionId WorldRegionDocument::AddCircle(std::string name, float x, float y, float radius, std::string tag)
{
    if (entries_.size() >= MaximumRegions) throw std::length_error("Limite de Regions alcanzado.");
    WorldRegion region{nextId_++, Checked(std::move(name), "nombre"), WorldRegionShape::Circle,
        x,y,x,y,radius,std::move(tag)};
    Validate(region);
    entries_.push_back(region);
    Push({region.Id, std::nullopt, region, "Create Circle Region"});
    return region.Id;
}

bool WorldRegionDocument::Set(RegionId id, const WorldRegion& value, std::string label)
{
    if (pending_) throw std::logic_error("Region: hay una edicion interactiva activa.");
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const WorldRegion& region){ return region.Id == id; });
    if (found == entries_.end()) return false;
    WorldRegion changed = value;
    changed.Id = id;
    Validate(changed);
    if (*found == changed) return false;
    const WorldRegion before = *found;
    *found = changed;
    Push({id, before, changed, std::move(label)});
    return true;
}

bool WorldRegionDocument::Remove(RegionId id)
{
    if (pending_) throw std::logic_error("Region: hay una edicion interactiva activa.");
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const WorldRegion& r){ return r.Id == id; });
    if (found == entries_.end()) return false;
    const WorldRegion before = *found;
    entries_.erase(found);
    Push({id, before, std::nullopt, "Delete Region"});
    return true;
}

const WorldRegion* WorldRegionDocument::Find(RegionId id) const
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const WorldRegion& r){ return r.Id == id; });
    return found == entries_.end() ? nullptr : &*found;
}

void WorldRegionDocument::BeginEdit(RegionId id, std::string label)
{
    if (pending_) throw std::logic_error("Region: ya existe una edicion interactiva.");
    const auto* region = Find(id);
    if (!region) throw std::invalid_argument("Region: ID inexistente.");
    pending_ = Change{id, *region, *region, std::move(label)};
}

bool WorldRegionDocument::Preview(const WorldRegion& value)
{
    if (!pending_) return false;
    WorldRegion changed = value;
    changed.Id = pending_->Id;
    Validate(changed);
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [id = pending_->Id](const WorldRegion& region){ return region.Id == id; });
    if (found == entries_.end()) throw std::logic_error("Region: desaparecio durante la edicion.");
    *found = changed;
    pending_->After = changed;
    ++revision_;
    return true;
}

bool WorldRegionDocument::CommitEdit()
{
    if (!pending_) return false;
    Change change = std::move(*pending_);
    pending_.reset();
    if (change.Before == change.After) return false;
    Push(std::move(change));
    return true;
}

void WorldRegionDocument::CancelEdit()
{
    if (!pending_) return;
    const Change change = *pending_;
    pending_.reset();
    if (change.Before)
    {
        const auto found = std::find_if(entries_.begin(), entries_.end(),
            [id = change.Id](const WorldRegion& region){ return region.Id == id; });
        if (found != entries_.end()) *found = *change.Before;
    }
    ++revision_;
}

bool WorldRegionDocument::Undo()
{
    if (!CanUndo()) return false;
    Apply(history_[--cursor_], false);
    return true;
}

bool WorldRegionDocument::Redo()
{
    if (!CanRedo()) return false;
    Apply(history_[cursor_], true);
    ++cursor_;
    return true;
}

std::vector<std::uint8_t> WorldRegionDocument::Encode(int mapId) const
{
    if (pending_) throw std::logic_error("No se puede guardar Regions durante una edicion interactiva.");
    Json regions = Json::array();
    for (const auto& region : entries_)
    {
        Validate(region);
        regions.push_back({
            {"id",region.Id},{"name",region.Name},
            {"shape",region.Shape == WorldRegionShape::Rectangle ? "rectangle" : "circle"},
            {"x1",region.X1},{"y1",region.Y1},{"x2",region.X2},{"y2",region.Y2},
            {"radius",region.Radius},{"tag",region.Tag}
        });
    }
    Json root{{"schema",Schema},{"mapId",mapId},{"regions",regions}};
    const auto text = root.dump(2) + "\n";
    if (text.size() > MaximumBytes) throw std::length_error("Regions excede el limite.");
    return {text.begin(), text.end()};
}

void WorldRegionDocument::Decode(std::span<const std::uint8_t> bytes, int expectedMap)
{
    if (bytes.empty() || bytes.size() > MaximumBytes) throw std::invalid_argument("Regions fuera de limites.");
    Json root;
    try { root = Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())); }
    catch (const Json::exception&) { throw std::invalid_argument("Regions JSON invalido."); }
    if (!root.is_object() || root.value("schema","") != Schema || root.value("mapId",-1) != expectedMap)
        throw std::invalid_argument("Regions schema/mapa incompatible.");
    if (!root.contains("regions") || !root.at("regions").is_array() || root.at("regions").size() > MaximumRegions)
        throw std::invalid_argument("Lista Regions invalida.");

    std::vector<WorldRegion> parsed;
    std::unordered_set<RegionId> ids;
    RegionId next = 1;
    for (const auto& value : root.at("regions"))
    {
        WorldRegion region;
        region.Id = value.value("id", static_cast<RegionId>(NoRegion));
        region.Name = value.value("name", std::string{});
        const auto shape = value.value("shape", std::string{});
        if (shape == "rectangle") region.Shape = WorldRegionShape::Rectangle;
        else if (shape == "circle") region.Shape = WorldRegionShape::Circle;
        else throw std::invalid_argument("Region shape desconocido.");
        region.X1=value.value("x1",0.0f); region.Y1=value.value("y1",0.0f);
        region.X2=value.value("x2",0.0f); region.Y2=value.value("y2",0.0f);
        region.Radius=value.value("radius",0.0f); region.Tag=value.value("tag",std::string{});
        Validate(region);
        if (!ids.insert(region.Id).second) throw std::invalid_argument("Region ID duplicado.");
        next = std::max(next, region.Id + 1);
        parsed.push_back(std::move(region));
    }
    entries_ = std::move(parsed);
    nextId_ = next;
    history_.clear();
    pending_.reset();
    cursor_ = 0;
    state_ = nextState_ = savedState_ = 0; savedEntries_ = entries_;
    ++revision_;
}
}
