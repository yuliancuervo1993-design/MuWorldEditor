#include "WorldLayerDocument.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace Silver::WorldEditor
{
namespace
{
constexpr std::size_t MaximumLayers = 64;
constexpr std::size_t MaximumLayerName = 48;
constexpr std::size_t MaximumHistory = 256;

bool EqualInsensitive(const std::string& left, const std::string& right)
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}
}

WorldLayerDocument::WorldLayerDocument() { Reset(); }

WorldLayerDocument::Snapshot WorldLayerDocument::Capture() const
{
    return {layers_, assignments_, nextId_};
}

void WorldLayerDocument::Restore(const Snapshot& snapshot)
{
    layers_ = snapshot.Layers;
    assignments_ = snapshot.Assignments;
    nextId_ = snapshot.NextId;
    ++revision_;
}

void WorldLayerDocument::PushHistory(Snapshot before, std::string label)
{
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(historyCursor_), history_.end());
    history_.push_back({std::move(before), Capture(), std::move(label)});
    if (history_.size() > MaximumHistory) history_.erase(history_.begin());
    historyCursor_ = history_.size();
    ++historySerial_;
}

bool WorldLayerDocument::Dirty() const
{
    const auto current = Capture();
    return current.Layers != saved_.Layers || current.Assignments != saved_.Assignments
        || current.NextId != saved_.NextId;
}

void WorldLayerDocument::MarkSaved()
{
    saved_ = Capture();
}

void WorldLayerDocument::AcceptLoadedState()
{
    history_.clear();
    historyCursor_ = 0;
    MarkSaved();
}

bool WorldLayerDocument::Undo()
{
    if (!CanUndo()) return false;
    Restore(history_[--historyCursor_].Before);
    return true;
}

bool WorldLayerDocument::Redo()
{
    if (!CanRedo()) return false;
    Restore(history_[historyCursor_++].After);
    return true;
}

std::string WorldLayerDocument::NormalizeName(std::string name)
{
    const auto first = std::find_if_not(name.begin(), name.end(),
        [](unsigned char value) { return std::isspace(value) != 0; });
    const auto last = std::find_if_not(name.rbegin(), name.rend(),
        [](unsigned char value) { return std::isspace(value) != 0; }).base();
    if (first >= last) throw std::invalid_argument("El nombre de la capa no puede estar vacio.");

    std::string result(first, last);
    if (result.size() > MaximumLayerName)
        throw std::invalid_argument("El nombre de la capa es demasiado largo.");
    if (std::any_of(result.begin(), result.end(),
        [](unsigned char value) { return std::iscntrl(value) != 0; }))
        throw std::invalid_argument("El nombre de la capa contiene caracteres no permitidos.");
    return result;
}

void WorldLayerDocument::RequireUniqueName(const std::string& name, WorldLayerId except) const
{
    const auto duplicate = std::find_if(layers_.begin(), layers_.end(),
        [&name, except](const WorldLayer& layer)
        {
            return layer.Id != except && EqualInsensitive(layer.Name, name);
        });
    if (duplicate != layers_.end())
        throw std::invalid_argument("Ya existe una capa con ese nombre.");
}

WorldLayer& WorldLayerDocument::Require(WorldLayerId id)
{
    const auto found = std::find_if(layers_.begin(), layers_.end(),
        [id](const WorldLayer& layer) { return layer.Id == id; });
    if (found == layers_.end()) throw std::out_of_range("Capa no encontrada.");
    return *found;
}

const WorldLayer* WorldLayerDocument::Find(WorldLayerId id) const
{
    const auto found = std::find_if(layers_.begin(), layers_.end(),
        [id](const WorldLayer& layer) { return layer.Id == id; });
    return found == layers_.end() ? nullptr : &*found;
}

void WorldLayerDocument::Reset()
{
    layers_.clear();
    assignments_.clear();
    layers_.push_back({DefaultWorldLayer, "Base", true, false});
    nextId_ = DefaultWorldLayer + 1;
    history_.clear();
    historyCursor_ = 0;
    ++revision_;
    MarkSaved();
}

WorldLayerId WorldLayerDocument::Add(std::string name)
{
    if (layers_.size() >= MaximumLayers)
        throw std::length_error("Se alcanzo el maximo de capas de esta sesion.");
    name = NormalizeName(std::move(name));
    RequireUniqueName(name, NoWorldLayer);
    auto before = Capture();
    const WorldLayerId id = nextId_++;
    layers_.push_back({id, std::move(name), true, false});
    ++revision_;
    PushHistory(std::move(before), "Crear capa");
    return id;
}

bool WorldLayerDocument::Rename(WorldLayerId id, std::string name)
{
    name = NormalizeName(std::move(name));
    RequireUniqueName(name, id);
    auto& layer = Require(id);
    if (layer.Name == name) return false;
    auto before = Capture();
    layer.Name = std::move(name);
    ++revision_;
    PushHistory(std::move(before), "Renombrar capa");
    return true;
}

bool WorldLayerDocument::SetVisible(WorldLayerId id, bool visible)
{
    auto& layer = Require(id);
    if (layer.Visible == visible) return false;
    auto before = Capture();
    layer.Visible = visible;
    ++revision_;
    PushHistory(std::move(before), visible ? "Mostrar capa" : "Ocultar capa");
    return true;
}

bool WorldLayerDocument::SetLocked(WorldLayerId id, bool locked)
{
    auto& layer = Require(id);
    if (layer.Locked == locked) return false;
    auto before = Capture();
    layer.Locked = locked;
    ++revision_;
    PushHistory(std::move(before), locked ? "Bloquear capa" : "Desbloquear capa");
    return true;
}

bool WorldLayerDocument::Remove(WorldLayerId id)
{
    if (id == DefaultWorldLayer)
        throw std::invalid_argument("La capa Base no se puede eliminar.");
    const auto found = std::find_if(layers_.begin(), layers_.end(),
        [id](const WorldLayer& layer) { return layer.Id == id; });
    if (found == layers_.end()) return false;

    auto before = Capture();
    for (auto assignment = assignments_.begin(); assignment != assignments_.end();)
    {
        if (assignment->second == id) assignment = assignments_.erase(assignment);
        else ++assignment;
    }
    layers_.erase(found);
    ++revision_;
    PushHistory(std::move(before), "Eliminar capa");
    return true;
}

bool WorldLayerDocument::Assign(ObjectId object, WorldLayerId layer)
{
    if (object == NoObject) throw std::invalid_argument("Objeto invalido para asignar a una capa.");
    Require(layer);
    if (LayerFor(object) == layer) return false;
    auto before = Capture();
    if (layer == DefaultWorldLayer) assignments_.erase(object);
    else assignments_[object] = layer;
    ++revision_;
    PushHistory(std::move(before), "Asignar objeto a capa");
    return true;
}

bool WorldLayerDocument::AssignMany(std::span<const ObjectId> objects, WorldLayerId layer)
{
    Require(layer);
    std::unordered_set<ObjectId> unique;
    unique.reserve(objects.size());
    for (ObjectId object : objects)
    {
        if (object == NoObject || !unique.insert(object).second)
            throw std::invalid_argument("La asignacion de capa contiene objetos repetidos o invalidos.");
    }

    bool changed = false;
    for (ObjectId object : objects)
        if (LayerFor(object) != layer) { changed = true; break; }
    if (!changed) return false;

    auto before = Capture();
    for (ObjectId object : objects)
    {
        if (layer == DefaultWorldLayer) assignments_.erase(object);
        else assignments_[object] = layer;
    }
    ++revision_;
    PushHistory(std::move(before), "Asignar seleccion a capa");
    return true;
}

WorldLayerId WorldLayerDocument::LayerFor(ObjectId object) const
{
    if (const auto found = assignments_.find(object); found != assignments_.end() && Find(found->second))
        return found->second;
    return DefaultWorldLayer;
}

bool WorldLayerDocument::IsVisible(ObjectId object) const
{
    const auto* layer = Find(LayerFor(object));
    return layer == nullptr || layer->Visible;
}

bool WorldLayerDocument::IsLocked(ObjectId object) const
{
    const auto* layer = Find(LayerFor(object));
    return layer != nullptr && layer->Locked;
}

void WorldLayerDocument::Prune(const WorldObjectDocument& document)
{
    const auto& entries = document.Entries();
    bool changed = false;
    for (auto assignment = assignments_.begin(); assignment != assignments_.end();)
    {
        const ObjectId id = assignment->first;
        const bool outsideDocument = id == NoObject || id > entries.size();
        const bool baselineOnly = !outsideDocument && entries[id - 1].Record
            && entries[id - 1].Record->BaselineOnly;
        if (outsideDocument || baselineOnly)
        {
            assignment = assignments_.erase(assignment);
            changed = true;
        }
        else ++assignment;
    }
    if (changed) ++revision_;
}
}
