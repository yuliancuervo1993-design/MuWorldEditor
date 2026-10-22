#include "WorldPrefabCodec.h"
#include "json.hpp"
#include <cmath>
#include <stdexcept>

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t MaximumPrefabBytes = 512 * 1024;

void ValidatePrefabRecord(const WorldObjectRecord& record)
{
    if (record.BaselineOnly)
        throw std::invalid_argument("Un registro legacy opaco no puede formar parte de un Prefab.");
    Silver::WorldEditor::ValidateRecord(record);
}

void ValidateName(const std::string& name)
{
    if (name.empty() || name.size() > 64)
        throw std::invalid_argument("Nombre de Prefab vacio o demasiado largo.");
    for (unsigned char value : name)
        if (value < 32) throw std::invalid_argument("Nombre de Prefab contiene caracteres no permitidos.");
}
}

WorldPrefab WorldPrefabCodec::FromSelection(
    std::string name,
    std::span<const WorldObjectRecord> records,
    const std::array<float, 3>& pivot)
{
    ValidateName(name);
    if (records.empty() || records.size() > MaximumRecords)
        throw std::invalid_argument("Cantidad de objetos invalida para Prefab.");
    for (float value : pivot)
        if (!std::isfinite(value)) throw std::invalid_argument("Pivot de Prefab invalido.");

    WorldPrefab prefab;
    prefab.Name = std::move(name);
    prefab.Records.reserve(records.size());
    for (const auto& source : records)
    {
        ValidatePrefabRecord(source);
        auto relative = source;
        for (std::size_t axis = 0; axis < 3; ++axis)
            relative.Position[axis] -= pivot[axis];
        prefab.Records.push_back(relative);
    }
    return prefab;
}

std::vector<WorldObjectRecord> WorldPrefabCodec::Instantiate(
    const WorldPrefab& prefab,
    const std::array<float, 3>& position)
{
    ValidateName(prefab.Name);
    if (prefab.Records.empty() || prefab.Records.size() > MaximumRecords)
        throw std::invalid_argument("Prefab vacio o demasiado grande.");
    for (float value : position)
        if (!std::isfinite(value)) throw std::invalid_argument("Posicion de Prefab invalida.");

    std::vector<WorldObjectRecord> result;
    result.reserve(prefab.Records.size());
    for (const auto& source : prefab.Records)
    {
        ValidatePrefabRecord(source);
        auto record = source;
        for (std::size_t axis = 0; axis < 3; ++axis)
            record.Position[axis] += position[axis];
        ValidatePrefabRecord(record);
        result.push_back(record);
    }
    return result;
}

std::vector<std::uint8_t> WorldPrefabCodec::Encode(const WorldPrefab& prefab)
{
    ValidateName(prefab.Name);
    if (prefab.Records.empty() || prefab.Records.size() > MaximumRecords)
        throw std::invalid_argument("Prefab vacio o demasiado grande.");

    Json records = Json::array();
    for (const auto& record : prefab.Records)
    {
        ValidatePrefabRecord(record);
        records.push_back({
            {"type", record.Type},
            {"position", {record.Position[0], record.Position[1], record.Position[2]}},
            {"angle", {record.Angle[0], record.Angle[1], record.Angle[2]}},
            {"scale", record.Scale}
        });
    }
    Json json{{"schema", Schema}, {"name", prefab.Name}, {"records", records}};
    const std::string text = json.dump(2) + "\n";
    if (text.size() > MaximumPrefabBytes)
        throw std::length_error("Prefab excede el limite permitido.");
    return {text.begin(), text.end()};
}

WorldPrefab WorldPrefabCodec::Decode(std::span<const std::uint8_t> bytes)
{
    if (bytes.empty() || bytes.size() > MaximumPrefabBytes)
        throw std::invalid_argument("Prefab fuera de limites.");

    Json json;
    try { json = Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())); }
    catch (const Json::exception&) { throw std::invalid_argument("Prefab JSON invalido."); }

    if (!json.is_object() || json.value("schema", "") != Schema)
        throw std::invalid_argument("Schema de Prefab no compatible.");

    WorldPrefab prefab;
    prefab.Name = json.value("name", std::string{});
    ValidateName(prefab.Name);
    if (!json.contains("records") || !json.at("records").is_array()
        || json.at("records").empty() || json.at("records").size() > MaximumRecords)
        throw std::invalid_argument("Lista de objetos del Prefab invalida.");

    for (const auto& value : json.at("records"))
    {
        if (!value.is_object()) throw std::invalid_argument("Registro de Prefab invalido.");
        WorldObjectRecord record;
        record.Type = value.at("type").get<std::int16_t>();
        const auto position = value.at("position");
        const auto angle = value.at("angle");
        if (!position.is_array() || position.size() != 3 || !angle.is_array() || angle.size() != 3)
            throw std::invalid_argument("Transform de Prefab invalida.");
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            record.Position[axis] = position.at(axis).get<float>();
            record.Angle[axis] = angle.at(axis).get<float>();
        }
        record.Scale = value.at("scale").get<float>();
        ValidatePrefabRecord(record);
        prefab.Records.push_back(record);
    }
    return prefab;
}
}
