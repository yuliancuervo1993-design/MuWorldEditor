#include "WorldGateDocument.h"
#include "Core/Platform/ContentHash.h"
#include "json.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t MaximumBytes = 1024 * 1024;
constexpr std::size_t MaximumGates = 4096;
constexpr std::size_t MaximumHistory = 256;

// Validate before narrowing: JSON 256 must never become uint8_t(0).
template<class T> T Integer(const Json& object, const char* key)
{
    const auto& value = object.at(key);
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        throw std::invalid_argument(std::string("Gate requiere entero positivo: ") + key);
    const auto number = value.get<std::uint64_t>();
    if (number > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
        throw std::invalid_argument(std::string("Gate fuera de rango: ") + key);
    return static_cast<T>(number);
}

Json Read(std::span<const std::uint8_t> bytes)
{
    if (bytes.empty() || bytes.size() > MaximumBytes) throw std::invalid_argument("Gates fuera de limites.");
    try { return Json::parse(bytes.begin(), bytes.end()); }
    catch (const Json::exception&) { throw std::invalid_argument("Gates JSON invalido."); }
}

WorldGate Parse(const Json& value)
{
    WorldGate g;
    g.Id=Integer<GateId>(value,"id"); g.Number=Integer<std::int16_t>(value,"number");
    g.LevelRequirement=Integer<std::int16_t>(value,"levelRequirement");
    g.X1=Integer<std::uint8_t>(value,"x1"); g.Y1=Integer<std::uint8_t>(value,"y1");
    g.X2=Integer<std::uint8_t>(value,"x2"); g.Y2=Integer<std::uint8_t>(value,"y2");
    g.TargetGateId=Integer<GateId>(value,"targetGateId"); g.TargetMap=Integer<int>(value,"targetMap");
    g.TargetX1=Integer<std::uint8_t>(value,"targetX1"); g.TargetY1=Integer<std::uint8_t>(value,"targetY1");
    g.TargetX2=Integer<std::uint8_t>(value,"targetX2"); g.TargetY2=Integer<std::uint8_t>(value,"targetY2");
    g.TargetDirection=Integer<std::uint8_t>(value,"targetDirection");
    WorldGateDocument::Validate(g);
    return g;
}

std::vector<WorldGate> ParseRows(const Json& root)
{
    const auto& rows = root.at("gates");
    if (!rows.is_array() || rows.size() > MaximumGates) throw std::invalid_argument("Lista Gates invalida.");
    std::vector<WorldGate> parsed; std::unordered_set<GateId> ids;
    for (const auto& row : rows)
    {
        auto gate = Parse(row);
        if (!ids.insert(gate.Id).second) throw std::invalid_argument("ID Gate duplicado.");
        parsed.push_back(gate);
    }
    return parsed;
}

Json Encode(const WorldGate& g)
{
    return {{"id",g.Id},{"number",g.Number},{"levelRequirement",g.LevelRequirement},
        {"x1",g.X1},{"y1",g.Y1},{"x2",g.X2},{"y2",g.Y2},
        {"targetGateId",g.TargetGateId},{"targetMap",g.TargetMap},
        {"targetX1",g.TargetX1},{"targetY1",g.TargetY1},{"targetX2",g.TargetX2},{"targetY2",g.TargetY2},
        {"targetDirection",g.TargetDirection}};
}
}

void WorldGateDocument::Validate(const WorldGate& gate)
{
    if (gate.Id==NoGate || gate.Number<0 || gate.LevelRequirement<0 || gate.X1>gate.X2 || gate.Y1>gate.Y2
        || gate.TargetGateId==NoGate || gate.TargetMap<0 || gate.TargetX1>gate.TargetX2
        || gate.TargetY1>gate.TargetY2 || gate.TargetDirection>7)
        throw std::invalid_argument("Gate contiene propiedades fuera de rango.");
}

void WorldGateDocument::Clear()
{
    mapNumber_=-1; entries_.clear(); baseline_.clear(); baselineSha256_.clear(); history_.clear();
    cursor_=0; state_=savedState_=nextState_=0; ++revision_;
}

void WorldGateDocument::Load(std::span<const std::uint8_t> bytes, int expectedMap)
{
    const auto root=Read(bytes);
    if (!root.is_object() || root.value("schema", "")!=Schema || expectedMap<0
        || Integer<int>(root,"mapNumber")!=expectedMap)
        throw std::invalid_argument("Export Gates schema/mapa incompatible.");
    auto parsed=ParseRows(root);
    const auto hash=mu::crypto::Sha256Hex({reinterpret_cast<const char*>(bytes.data()),bytes.size()});
    Clear(); mapNumber_=expectedMap; entries_=std::move(parsed); baseline_=entries_; baselineSha256_=hash;
}

const WorldGate* WorldGateDocument::Find(GateId id) const
{
    const auto found=std::find_if(entries_.begin(),entries_.end(),[id](const WorldGate& gate){return gate.Id==id;});
    return found==entries_.end()?nullptr:&*found;
}

bool WorldGateDocument::SetCandidate(GateId id, const WorldGate& value)
{
    const auto found=std::find_if(entries_.begin(),entries_.end(),[id](const WorldGate& gate){return gate.Id==id;});
    if (found==entries_.end()) return false;
    if (value.Id!=id || value.Number!=found->Number) throw std::invalid_argument("Candidate conserva identidad y numero del Gate original.");
    Validate(value);
    if (*found==value) return false;
    if (cursor_<history_.size()) history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
    const Change change{*found,value,state_,++nextState_};
    history_.push_back(change);
    if (history_.size()>MaximumHistory) history_.erase(history_.begin());
    *found=value; state_=change.AfterState; cursor_=history_.size(); ++historySerial_; ++revision_;
    return true;
}

bool WorldGateDocument::Undo()
{
    if (!CanUndo()) return false;
    const auto& change=history_[--cursor_];
    for (auto& gate:entries_) if (gate.Id==change.Before.Id) { gate=change.Before; break; }
    state_=change.BeforeState; ++revision_; return true;
}

bool WorldGateDocument::Redo()
{
    if (!CanRedo()) return false;
    const auto& change=history_[cursor_++];
    for (auto& gate:entries_) if (gate.Id==change.After.Id) { gate=change.After; break; }
    state_=change.AfterState; ++revision_; return true;
}

std::vector<std::uint8_t> WorldGateDocument::EncodeCandidate() const
{
    if (!Loaded()) throw std::logic_error("Falta export Gates actual.");
    Json rows=Json::array();
    for (const auto& gate:entries_) { Validate(gate); rows.push_back(Encode(gate)); }
    const Json root{{"schema",CandidateSchema},{"mapNumber",mapNumber_},
        {"baselineSha256",baselineSha256_},{"gates",rows}};
    const auto text=root.dump(2);
    if (text.size()>MaximumBytes) throw std::length_error("Gates Candidate excede limites.");
    return {text.begin(),text.end()};
}

void WorldGateDocument::LoadCandidate(std::span<const std::uint8_t> bytes)
{
    if (!Loaded()) throw std::logic_error("Candidate requiere export Gates actual.");
    const auto root=Read(bytes);
    if (!root.is_object() || root.value("schema", "")!=CandidateSchema
        || Integer<int>(root,"mapNumber")!=mapNumber_ || root.value("baselineSha256", "")!=baselineSha256_)
        throw std::invalid_argument("Candidate Gates no coincide con el mapa/export actual.");
    auto parsed=ParseRows(root);
    if (parsed.size()!=baseline_.size()) throw std::invalid_argument("Candidate no puede agregar o quitar Gates.");
    for (const auto& gate:parsed)
    {
        const auto found=std::find_if(baseline_.begin(),baseline_.end(),[&](const WorldGate& base){return base.Id==gate.Id;});
        if (found==baseline_.end() || found->Number!=gate.Number) throw std::invalid_argument("Candidate cambia identidad del Gate.");
    }
    entries_=std::move(parsed); history_.clear(); cursor_=0; state_=savedState_=nextState_=0; ++revision_;
}
}
