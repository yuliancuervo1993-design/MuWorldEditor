#include "WorldEffectDocument.h"
#include "WorldModelCatalog.h"
#include "WorldFileIo.h"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;
constexpr float MapEdge = 25600.0f;
constexpr std::size_t MaximumLights = 4096;
constexpr std::size_t MaximumRuntimeEffects = 4096;

bool Linked(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::exists(StreamPath(path), error) || error) return false;
    return WorldModelCatalog::IsLinkedPath(path);
}

void EnsureParentSafe(const std::filesystem::path& file)
{
    const auto parent = file.parent_path();
    if (parent.empty()) throw std::invalid_argument("Ruta de efectos sin carpeta.");
    std::filesystem::create_directories(StreamPath(parent));
    if (Linked(parent)) throw std::invalid_argument("La carpeta de efectos no puede ser un enlace.");
}

std::array<float, 3> ReadFloat3(const Json& value, const char* name)
{
    if (!value.is_array() || value.size() != 3)
        throw std::invalid_argument(std::string{name} + " debe tener 3 componentes.");
    std::array<float, 3> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        if (!value[i].is_number()) throw std::invalid_argument(std::string{name} + " contiene un valor no numerico.");
        result[i] = value[i].get<float>();
    }
    return result;
}
}

void WorldEffectDocument::Validate(const WorldPointLight& light)
{
    if (light.Id == NoEffect) throw std::invalid_argument("ID de efecto invalido.");
    for (float value : light.Position)
        if (!std::isfinite(value)) throw std::invalid_argument("Posicion de luz no finita.");
    if (light.Position[0] < 0.0f || light.Position[0] >= MapEdge
        || light.Position[1] < 0.0f || light.Position[1] >= MapEdge)
        throw std::invalid_argument("La luz debe permanecer dentro del mapa.");
    for (float value : light.Color)
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            throw std::invalid_argument("Color de luz fuera de 0..1.");
    if (!std::isfinite(light.Intensity) || light.Intensity < 0.0f || light.Intensity > 4.0f)
        throw std::invalid_argument("Intensidad de luz fuera de 0..4.");
    if (light.Radius < 1 || light.Radius > 32)
        throw std::invalid_argument("Radio de luz fuera de 1..32 celdas.");
}

void WorldEffectDocument::Validate(const WorldRuntimeEffect& effect)
{
    if(effect.Id==NoEffect||effect.Preset.empty()||effect.Preset.size()>80||effect.Kind<0||effect.Kind>1||effect.Type<0||effect.SubType<0||effect.SubType>255||!std::isfinite(effect.Scale)||effect.Scale<=0.0f||effect.Scale>10.0f)throw std::invalid_argument("Runtime effect invalido.");
    for(float value:effect.Position)if(!std::isfinite(value))throw std::invalid_argument("Posicion runtime effect no finita.");
    if(effect.Position[0]<0.0f||effect.Position[0]>=MapEdge||effect.Position[1]<0.0f||effect.Position[1]>=MapEdge)throw std::invalid_argument("Runtime effect fuera del mapa.");
}

void WorldEffectDocument::Clear()
{
    lights_.clear(); runtimeEffects_.clear();
    saved_.clear(); savedRuntimeEffects_.clear();
    undo_.clear();
    redo_.clear();
    nextId_ = 1;
}

void WorldEffectDocument::Load(const std::filesystem::path& file, int mapId, int worldNumber)
{
    Clear();
    std::error_code error;
    if (!std::filesystem::exists(StreamPath(file), error) || error)
        return;
    if (!std::filesystem::is_regular_file(StreamPath(file)) || Linked(file))
        throw std::invalid_argument("Sidecar de efectos invalido.");
    const auto size = std::filesystem::file_size(StreamPath(file));
    if (size == 0 || size > 4 * 1024 * 1024)
        throw std::invalid_argument("Sidecar de efectos fuera de limites.");

    std::ifstream input(StreamPath(file), std::ios::binary);
    if (!input) throw std::runtime_error("No se pudo leer el sidecar de efectos.");
    const auto json = Json::parse(std::string(std::istreambuf_iterator<char>(input), {}));
    if (!json.is_object() || json.value("schema", "") != Schema)
        throw std::invalid_argument("Schema de efectos no compatible.");
    if (json.value("mapId", -1) != mapId || json.value("worldNumber", -1) != worldNumber)
        throw std::invalid_argument("El sidecar de efectos pertenece a otro mapa.");

    if (json.contains("pointLights"))
    {
        if (!json.at("pointLights").is_array()) throw std::invalid_argument("pointLights debe ser un array.");
        if (json.at("pointLights").size() > MaximumLights) throw std::length_error("Demasiadas luces en el sidecar.");
        for (const auto& value : json.at("pointLights"))
        {
            if (!value.is_object()) throw std::invalid_argument("Luz invalida.");
            WorldPointLight light;
            light.Id = value.value("id", static_cast<EffectId>(0));
            light.Position = ReadFloat3(value.at("position"), "position");
            light.Color = ReadFloat3(value.at("color"), "color");
            light.Intensity = value.value("intensity", 1.0f);
            light.Radius = value.value("radius", 3);
            Validate(light);
            if (Find(light.Id)) throw std::invalid_argument("ID de luz duplicado.");
            nextId_ = std::max(nextId_, light.Id + 1);
            lights_.push_back(light);
        }
    }
    if(json.contains("runtimeEffects"))
    {
        if(!json.at("runtimeEffects").is_array()||json.at("runtimeEffects").size()>MaximumRuntimeEffects)throw std::invalid_argument("runtimeEffects invalido.");
        for(const auto& value:json.at("runtimeEffects")){WorldRuntimeEffect effect;effect.Id=value.value("id",NoEffect);effect.Preset=value.value("preset",std::string{});effect.Kind=value.value("kind",0);effect.Type=value.value("type",-1);effect.SubType=value.value("subType",0);effect.Position=ReadFloat3(value.at("position"),"position");effect.Scale=value.value("scale",1.0f);effect.Enabled=value.value("enabled",true);Validate(effect);if(FindRuntimeEffect(effect.Id)||Find(effect.Id))throw std::invalid_argument("ID de efecto duplicado.");nextId_=std::max(nextId_,effect.Id+1);runtimeEffects_.push_back(std::move(effect));}
    }
    saved_ = lights_; savedRuntimeEffects_=runtimeEffects_;
}

void WorldEffectDocument::Save(const std::filesystem::path& file, int mapId, int worldNumber)
{
    EnsureParentSafe(file);
    Json lights = Json::array();
    for (const auto& light : lights_)
    {
        Validate(light);
        lights.push_back({
            {"id", light.Id},
            {"position", light.Position},
            {"color", light.Color},
            {"intensity", light.Intensity},
            {"radius", light.Radius}
        });
    }

    Json runtimeEffects=Json::array();for(const auto& effect:runtimeEffects_){Validate(effect);runtimeEffects.push_back({{"id",effect.Id},{"preset",effect.Preset},{"kind",effect.Kind},{"type",effect.Type},{"subType",effect.SubType},{"position",effect.Position},{"scale",effect.Scale},{"enabled",effect.Enabled}});}
    Json json{{"schema", Schema},{"mapId", mapId},{"worldNumber", worldNumber},{"pointLights", lights},{"runtimeEffects",runtimeEffects}};

    const auto partial = file.string() + ".partial";
    {
        std::ofstream output(StreamPath(partial), std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("No se pudo escribir el sidecar temporal de efectos.");
        output << json.dump(2) << "\n";
        if (!output) throw std::runtime_error("No se pudo completar el sidecar de efectos.");
    }

    std::error_code error;
    if (std::filesystem::exists(StreamPath(file), error) && !error)
    {
        if (Linked(file)) throw std::invalid_argument("No se reemplaza un sidecar enlazado.");
        std::filesystem::remove(StreamPath(file), error);
        if (error) throw std::runtime_error("No se pudo reemplazar el sidecar anterior de efectos.");
    }
    std::filesystem::rename(StreamPath(partial), StreamPath(file));
    MarkSaved();
}

WorldPointLight* WorldEffectDocument::Find(EffectId id)
{
    const auto found = std::find_if(lights_.begin(), lights_.end(), [id](const auto& light) { return light.Id == id; });
    return found == lights_.end() ? nullptr : &*found;
}

const WorldPointLight* WorldEffectDocument::Find(EffectId id) const
{
    const auto found = std::find_if(lights_.begin(), lights_.end(), [id](const auto& light) { return light.Id == id; });
    return found == lights_.end() ? nullptr : &*found;
}

void WorldEffectDocument::PushUndo()
{
    undo_.push_back(Capture());
    if (undo_.size() > 128) undo_.erase(undo_.begin());
    redo_.clear();
    ++historySerial_;
}

EffectId WorldEffectDocument::AddPointLight(const std::array<float, 3>& position)
{
    if (lights_.size() >= MaximumLights) throw std::length_error("Se alcanzo el maximo de luces del editor.");
    WorldPointLight light;
    light.Id = nextId_++;
    light.Position = position;
    Validate(light);
    PushUndo();
    lights_.push_back(light);
    return light.Id;
}

bool WorldEffectDocument::UpdatePointLight(EffectId id, const WorldPointLight& value)
{
    auto* current = Find(id);
    if (!current) return false;
    WorldPointLight changed = value;
    changed.Id = id;
    Validate(changed);
    if (*current == changed) return false;
    PushUndo();
    *Find(id) = changed;
    return true;
}

bool WorldEffectDocument::Remove(EffectId id)
{
    const auto found = std::find_if(lights_.begin(), lights_.end(), [id](const auto& light) { return light.Id == id; });
    if (found == lights_.end()) return false;
    PushUndo();
    lights_.erase(std::find_if(lights_.begin(), lights_.end(), [id](const auto& light) { return light.Id == id; }));
    return true;
}


WorldRuntimeEffect* WorldEffectDocument::FindRuntimeEffect(EffectId id)
{
    const auto found=std::find_if(runtimeEffects_.begin(),runtimeEffects_.end(),[id](const auto& effect){return effect.Id==id;});return found==runtimeEffects_.end()?nullptr:&*found;
}
const WorldRuntimeEffect* WorldEffectDocument::FindRuntimeEffect(EffectId id) const
{
    const auto found=std::find_if(runtimeEffects_.begin(),runtimeEffects_.end(),[id](const auto& effect){return effect.Id==id;});return found==runtimeEffects_.end()?nullptr:&*found;
}
EffectId WorldEffectDocument::AddRuntimeEffect(const WorldRuntimeEffect& value)
{
    if(runtimeEffects_.size()>=MaximumRuntimeEffects)throw std::length_error("Se alcanzo el maximo de runtime effects.");
    WorldRuntimeEffect effect=value;effect.Id=nextId_++;Validate(effect);PushUndo();runtimeEffects_.push_back(effect);return effect.Id;
}
bool WorldEffectDocument::UpdateRuntimeEffect(EffectId id,const WorldRuntimeEffect& value)
{
    auto* current=FindRuntimeEffect(id);if(!current)return false;WorldRuntimeEffect changed=value;changed.Id=id;Validate(changed);if(*current==changed)return false;PushUndo();*FindRuntimeEffect(id)=changed;return true;
}
bool WorldEffectDocument::RemoveRuntimeEffect(EffectId id)
{
    const auto found=std::find_if(runtimeEffects_.begin(),runtimeEffects_.end(),[id](const auto& effect){return effect.Id==id;});if(found==runtimeEffects_.end())return false;PushUndo();runtimeEffects_.erase(found);return true;
}

void WorldEffectDocument::Undo()
{
    if (undo_.empty()) return;
    redo_.push_back(Capture());
    Restore(std::move(undo_.back()));
    undo_.pop_back();
}

void WorldEffectDocument::Redo()
{
    if (redo_.empty()) return;
    undo_.push_back(Capture());
    Restore(std::move(redo_.back()));
    redo_.pop_back();
}

void WorldEffectDocument::MarkSaved()
{
    saved_ = lights_; savedRuntimeEffects_=runtimeEffects_;
}
}
