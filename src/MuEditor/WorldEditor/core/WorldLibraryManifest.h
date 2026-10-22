#pragma once
#include "WorldObjectDocument.h"
#include "TerrainMappingCodec.h"
#include "json.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
namespace Silver::WorldEditor
{
// Editor resources never borrow game Type IDs. The legacy OBJ export excludes these records.
class WorldLibraryManifest
{
public:
    static constexpr int FirstModel = 30000;
    static constexpr int MaximumModels = 256;
    static constexpr const char* FileName = "library-assets.json";
    struct Binding { std::string Path, Sha256; };
    std::map<int, Binding> Models, Terrain;
    std::vector<WorldObjectRecord> Objects;
    std::vector<std::uint8_t> MappingBytes;
    std::string MappingBaseline;
    static bool IsLibraryModel(int type) { return type >= FirstModel && type < FirstModel + MaximumModels; }
    static bool SafePath(const std::string& path)
    {
        return !path.empty() && path.size() <= 240 && path.front() != '/'
            && path.find('\\') == std::string::npos && path.find(':') == std::string::npos
            && path.find("..") == std::string::npos && path.find('\0') == std::string::npos;
    }
    static int FreeTerrainSlot(const TerrainMappingFile& mapping, const std::map<int, Binding>& reserved)
    {
        for (int slot = 29; slot >= 14; --slot)
        {
            if (reserved.contains(slot)) continue;
            if (std::find(mapping.Layer1.begin(), mapping.Layer1.end(), slot) != mapping.Layer1.end()) continue;
            // Reserve even invisible layer-2 references; undo may make them visible again.
            if (std::find(mapping.Layer2.begin(), mapping.Layer2.end(), slot) == mapping.Layer2.end()) return slot;
        }
        return -1;
    }
    std::vector<std::uint8_t> Encode(int map, const std::string& baseline) const
    {
        nlohmann::json value{{"schema",1},{"map",map},{"baseline",baseline},{"models",nlohmann::json::array()},
            {"terrain",nlohmann::json::array()},{"objects",nlohmann::json::array()}};
        for (const auto& [id,binding] : Models) value["models"].push_back({{"id",id},{"path",binding.Path},{"sha256",binding.Sha256}});
        for (const auto& [id,binding] : Terrain) value["terrain"].push_back({{"id",id},{"path",binding.Path},{"sha256",binding.Sha256}});
        for (const auto& object : Objects) value["objects"].push_back({{"type",object.Type},{"position",object.Position},{"angle",object.Angle},{"scale",object.Scale}});
        value["mapping"] = MappingBytes; value["mappingBaseline"] = MappingBaseline;
        const auto text = value.dump(2); return {text.begin(),text.end()};
    }
    static WorldLibraryManifest Decode(std::span<const std::uint8_t> bytes, int map, const std::string& baseline)
    {
        if (bytes.size() > 8*1024*1024) throw std::invalid_argument("Biblioteca guardada demasiado grande.");
        const auto value = nlohmann::json::parse(bytes.begin(),bytes.end());
        if (value.at("schema") != 1 || value.at("map") != map || value.at("baseline") != baseline)
            throw std::invalid_argument("La biblioteca guardada pertenece a otro mapa o version.");
        WorldLibraryManifest result;
        result.MappingBytes = value.value("mapping", std::vector<std::uint8_t>{});
        result.MappingBaseline = value.value("mappingBaseline", std::string{});
        if (!result.MappingBytes.empty()) (void)TerrainMappingCodec::Decode(result.MappingBytes);
        ReadBindings(value.at("models"), result.Models, true);
        ReadBindings(value.at("terrain"), result.Terrain, false);
        if (!value.at("objects").is_array() || value.at("objects").size() > MaximumObjects)
            throw std::invalid_argument("Cantidad de objetos de biblioteca fuera de limites.");
        for (const auto& row : value.at("objects"))
        {
            const int type = row.at("type").get<int>();
            if (!result.Models.contains(type)) throw std::invalid_argument("Objeto sin recurso de biblioteca.");
            WorldObjectRecord object; object.Type = static_cast<std::int16_t>(type);
            object.Position = row.at("position").get<std::array<float,3>>();
            object.Angle = row.at("angle").get<std::array<float,3>>(); object.Scale = row.at("scale").get<float>();
            ValidateRecord(object); SpatialBlock(object); result.Objects.push_back(object);
        }
        return result;
    }
private:
    static void ReadBindings(const nlohmann::json& rows, std::map<int, Binding>& target, bool models)
    {
        if (!rows.is_array() || rows.size() > static_cast<std::size_t>(models ? MaximumModels : 16))
            throw std::invalid_argument("Catalogo guardado fuera de limites.");
        for (const auto& row : rows)
        {
            const int id = row.at("id").get<int>();
            Binding binding{row.at("path").get<std::string>(),row.at("sha256").get<std::string>()};
            const bool hash = binding.Sha256.size() == 64 && std::all_of(binding.Sha256.begin(),binding.Sha256.end(),
                [](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
            if (!(models ? IsLibraryModel(id) : id>=14 && id<=29) || !SafePath(binding.Path) || !hash
                || !target.emplace(id,binding).second) throw std::invalid_argument("Recurso de biblioteca invalido.");
        }
    }
};
}
