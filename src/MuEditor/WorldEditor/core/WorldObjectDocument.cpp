#include "WorldObjectDocument.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Silver::WorldEditor
{
void WorldObjectDocument::Load(WorldObjectFile file)
{
    WorldObjectCodec::Encode(file); // Validate the complete document before replacing it.
    std::vector<WorldObjectEntry> entries;
    entries.reserve(file.Records.size());
    ObjectId id = 1;
    for (const auto& record : file.Records) entries.push_back({id++, record});
    file.Records.clear();
    header_ = std::move(file);
    entries_ = std::move(entries);
    pending_.reset(); history_.clear(); cursor_ = 0;
    state_ = ++nextState_; savedState_ = state_; ++revision_;
}
WorldObjectEntry& WorldObjectDocument::Entry(ObjectId id)
{
    if (id == NoObject || id > entries_.size()) throw std::out_of_range("Objeto no encontrado.");
    return entries_[id - 1];
}
const WorldObjectRecord* WorldObjectDocument::Find(ObjectId id) const
{
    if (id == NoObject || id > entries_.size() || !entries_[id - 1].Record) return nullptr;
    return &*entries_[id - 1].Record;
}
WorldObjectFile WorldObjectDocument::Export() const
{
    WorldObjectFile result = header_;
    result.Records.reserve(entries_.size());
    for (const auto& entry : entries_) if (entry.Record) result.Records.push_back(*entry.Record);
    return result;
}
void WorldObjectDocument::Push(Change change)
{
    if (change.Items.empty()) return;
    constexpr std::size_t MaximumUndo = 512;
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_), history_.end());
    change.BeforeState = state_; change.AfterState = ++nextState_;
    history_.push_back(std::move(change));
    state_ = history_.back().AfterState;
    if (history_.size() > MaximumUndo) history_.erase(history_.begin());
    cursor_ = history_.size(); ++revision_; ++historySerial_;
}
ObjectId WorldObjectDocument::Add(const WorldObjectRecord& record)
{
    if (pending_) throw std::logic_error("Termina la transformacion antes de agregar.");
    if (record.BaselineOnly) throw std::invalid_argument("Un registro opaco del baseline no se puede duplicar.");
    SpatialBlock(record);
    if (entries_.size() >= MaximumObjects) throw std::length_error("Limite de objetos de esta sesion alcanzado.");
    const auto id = static_cast<ObjectId>(entries_.size() + 1);
    entries_.push_back({id, record});
    Change change;
    change.Items.push_back({id, std::nullopt, record});
    change.Label = "Agregar objeto";
    Push(std::move(change));
    return id;
}
std::vector<ObjectId> WorldObjectDocument::AddMany(const std::vector<WorldObjectRecord>& records, std::string label)
{
    if (pending_) throw std::logic_error("Termina la transformacion antes de agregar un grupo.");
    if (records.empty()) return {};
    if (entries_.size() + records.size() > MaximumObjects)
        throw std::length_error("El grupo supera el limite de objetos de esta sesion.");

    for (const auto& record : records)
    {
        if (record.BaselineOnly)
            throw std::invalid_argument("Un registro opaco del baseline no se puede duplicar.");
        SpatialBlock(record);
    }

    Change change;
    change.Label = std::move(label);
    change.Items.reserve(records.size());
    std::vector<ObjectId> ids;
    ids.reserve(records.size());
    for (const auto& record : records)
    {
        const auto id = static_cast<ObjectId>(entries_.size() + 1);
        entries_.push_back({id, record});
        change.Items.push_back({id, std::nullopt, record});
        ids.push_back(id);
    }
    Push(std::move(change));
    return ids;
}
void WorldObjectDocument::Begin(ObjectId id, std::string label)
{
    if (pending_) throw std::logic_error("Ya hay una transformacion en curso.");
    auto& entry = Entry(id);
    if (!entry.Record) throw std::invalid_argument("Objeto retirado.");
    if (entry.Record->BaselineOnly) throw std::invalid_argument("Este registro legacy se preserva, pero no es editable.");
    Change change;
    change.Items.push_back({id, entry.Record, entry.Record});
    change.Label = std::move(label);
    pending_ = std::move(change);
}
void WorldObjectDocument::Preview(const WorldObjectRecord& record)
{
    if (!pending_) throw std::logic_error("No hay transformacion activa.");
    auto& item = pending_->Items.front();
    SpatialBlock(record);
    if (!item.Before || record.Type != item.Before->Type) throw std::invalid_argument("No se cambia el modelo durante una transformacion.");
    Entry(item.Id).Record = record;
    item.After = record; ++revision_;
}
bool WorldObjectDocument::Commit()
{
    if (!pending_) return false;
    auto change = std::move(*pending_); pending_.reset();
    const bool changed = std::any_of(change.Items.begin(), change.Items.end(),
        [](const ChangeItem& item) { return item.Before != item.After; });
    if (!changed) return false;
    Push(std::move(change)); return true;
}
void WorldObjectDocument::Cancel()
{
    if (!pending_) return;
    for (const auto& item : pending_->Items) Entry(item.Id).Record = item.Before;
    pending_.reset(); ++revision_;
}
bool WorldObjectDocument::Set(ObjectId id, const WorldObjectRecord& record, std::string label)
{
    Begin(id, std::move(label));
    try { Preview(record); return Commit(); }
    catch (...) { Cancel(); throw; }
}
bool WorldObjectDocument::SetMany(const std::vector<WorldObjectUpdate>& updates, std::string label)
{
    if (pending_) throw std::logic_error("Termina la transformacion antes de editar una seleccion multiple.");
    if (updates.empty()) return false;

    Change change;
    change.Label = std::move(label);
    change.Items.reserve(updates.size());
    std::vector<ObjectId> seen;
    seen.reserve(updates.size());

    for (const auto& update : updates)
    {
        if (update.Id == NoObject || std::find(seen.begin(), seen.end(), update.Id) != seen.end())
            throw std::invalid_argument("La seleccion multiple contiene objetos repetidos o invalidos.");
        seen.push_back(update.Id);

        auto& entry = Entry(update.Id);
        if (!entry.Record) throw std::invalid_argument("La seleccion multiple contiene un objeto retirado.");
        if (entry.Record->BaselineOnly)
            throw std::invalid_argument("Un registro legacy preservado no se puede transformar en grupo.");
        SpatialBlock(update.Record);
        if (update.Record.Type != entry.Record->Type)
            throw std::invalid_argument("No se cambia el modelo durante una transformacion multiple.");
        if (*entry.Record != update.Record)
            change.Items.push_back({update.Id, entry.Record, update.Record});
    }

    if (change.Items.empty()) return false;
    for (const auto& item : change.Items) Entry(item.Id).Record = item.After;
    Push(std::move(change));
    return true;
}
bool WorldObjectDocument::Remove(ObjectId id)
{
    if (pending_) throw std::logic_error("Termina la transformacion antes de retirar.");
    auto& entry = Entry(id);
    if (!entry.Record) return false;
    if (entry.Record->BaselineOnly) throw std::invalid_argument("Este registro legacy se preserva, pero no se puede retirar.");
    auto previous = entry.Record;
    entry.Record.reset();
    Change change;
    change.Items.push_back({id, previous, std::nullopt});
    change.Label = "Retirar objeto";
    Push(std::move(change)); return true;
}
bool WorldObjectDocument::RemoveMany(const std::vector<ObjectId>& ids, std::string label)
{
    if (pending_) throw std::logic_error("Termina la transformacion antes de retirar un grupo.");
    if (ids.empty()) return false;

    Change change;
    change.Label = std::move(label);
    change.Items.reserve(ids.size());
    std::vector<ObjectId> seen;
    seen.reserve(ids.size());
    for (ObjectId id : ids)
    {
        if (id == NoObject || std::find(seen.begin(), seen.end(), id) != seen.end())
            throw std::invalid_argument("El retiro multiple contiene objetos repetidos o invalidos.");
        seen.push_back(id);
        auto& entry = Entry(id);
        if (!entry.Record) throw std::invalid_argument("El retiro multiple contiene un objeto ya retirado.");
        if (entry.Record->BaselineOnly)
            throw std::invalid_argument("Un registro legacy preservado no se puede retirar en grupo.");
        change.Items.push_back({id, entry.Record, std::nullopt});
    }

    for (const auto& item : change.Items) Entry(item.Id).Record.reset();
    Push(std::move(change));
    return true;
}
bool WorldObjectDocument::Undo()
{
    if (!CanUndo()) return false;
    const auto& change = history_[--cursor_];
    for (const auto& item : change.Items) Entry(item.Id).Record = item.Before;
    state_ = change.BeforeState; ++revision_; return true;
}
bool WorldObjectDocument::Redo()
{
    if (!CanRedo()) return false;
    const auto& change = history_[cursor_++];
    for (const auto& item : change.Items) Entry(item.Id).Record = item.After;
    state_ = change.AfterState; ++revision_; return true;
}
bool WorldObjectDocument::Dirty() const
{
    if (state_ != savedState_) return true;
    if (!pending_) return false;
    return std::any_of(pending_->Items.begin(), pending_->Items.end(),
        [](const ChangeItem& item) { return item.Before != item.After; });
}
void WorldObjectDocument::MarkSaved()
{
    if (pending_) throw std::logic_error("No se guarda a mitad de una transformacion.");
    savedState_ = state_;
}
}
