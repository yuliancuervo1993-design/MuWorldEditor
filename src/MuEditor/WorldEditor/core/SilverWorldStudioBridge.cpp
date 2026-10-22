#include "SilverWorldStudioBridge.h"
#include "WorldFileIo.h"
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "json.hpp"

namespace Silver::WorldEditor
{
namespace
{
using Json = nlohmann::json;
constexpr std::uintmax_t MaximumContractBytes = 8 * 1024 * 1024;

void RefuseLinks(const std::filesystem::path& path)
{
    for (auto current = path; !current.empty(); current = current.parent_path())
    {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(StreamPath(current))))
            throw std::invalid_argument("El contrato del bridge no puede usar enlaces ni junctions.");
        if (current == current.root_path() || current == current.root_name()) break;
    }
}

std::string ReadText(const std::filesystem::path& file)
{
    RefuseLinks(file);
    if (!std::filesystem::is_regular_file(StreamPath(file)))
        throw std::invalid_argument("No se encontro el contrato del bridge.");
    const auto size = std::filesystem::file_size(StreamPath(file));
    if (size == 0 || size > MaximumContractBytes)
        throw std::invalid_argument("El contrato del bridge supera el limite permitido.");
    if (std::filesystem::hard_link_count(StreamPath(file)) != 1)
        throw std::invalid_argument("El contrato del bridge esta enlazado a otro archivo.");

    std::ifstream stream(StreamPath(file), std::ios::binary);
    if (!stream) throw std::runtime_error("No se pudo abrir el contrato del bridge.");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!stream.read(result.data(), static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("No se pudo leer el contrato completo del bridge.");
    return result;
}

const Json& Required(const Json& object, std::string_view name)
{
    if (!object.is_object() || !object.contains(name.data()))
        throw std::invalid_argument("Falta un campo obligatorio del contrato del bridge.");
    return object.at(name.data());
}

int Integer(const Json& object, std::string_view name, int minimum, int maximum)
{
    const auto& value = Required(object, name);
    if (!value.is_number_integer())
        throw std::invalid_argument("El contrato del bridge tiene un entero invalido.");
    const auto number = value.get<long long>();
    if (number < minimum || number > maximum)
        throw std::invalid_argument("El contrato del bridge tiene un entero fuera de rango.");
    return static_cast<int>(number);
}

std::size_t Count(const Json& object, std::string_view name)
{
    const auto& value = Required(object, name);
    if (!value.is_number_unsigned())
        throw std::invalid_argument("El contrato del bridge tiene un contador invalido.");
    const auto number = value.get<unsigned long long>();
    if (number > MaximumObjects)
        throw std::invalid_argument("El contrato del bridge supera el limite de objetos.");
    return static_cast<std::size_t>(number);
}

float Number(const Json& object, std::string_view name)
{
    const auto& value = Required(object, name);
    if (!value.is_number())
        throw std::invalid_argument("El contrato del bridge tiene un numero invalido.");
    const double number = value.get<double>();
    const float result = static_cast<float>(number);
    if (!std::isfinite(number) || !std::isfinite(result))
        throw std::invalid_argument("El contrato del bridge tiene un numero no finito.");
    return result;
}

std::array<float, 3> Vector(const Json& object, std::string_view name)
{
    const auto& value = Required(object, name);
    if (!value.is_array() || value.size() != 3)
        throw std::invalid_argument("El contrato del bridge tiene un vector invalido.");
    std::array<float, 3> result{};
    for (std::size_t axis = 0; axis < result.size(); ++axis)
    {
        const auto& component = value.at(axis);
        if (!component.is_number())
            throw std::invalid_argument("El contrato del bridge tiene un vector no numerico.");
        const double number = component.get<double>();
        result[axis] = static_cast<float>(number);
        if (!std::isfinite(number) || !std::isfinite(result[axis]))
            throw std::invalid_argument("El contrato del bridge tiene un vector no finito.");
    }
    return result;
}

std::optional<std::string> Model(const Json& object)
{
    const auto& value = Required(object, "model");
    if (value.is_null()) return std::nullopt;
    if (!value.is_string())
        throw std::invalid_argument("El modelo del contrato del bridge no es texto.");
    auto result = value.get<std::string>();
    if (result.empty()) throw std::invalid_argument("El modelo del contrato del bridge esta vacio.");
    return result;
}

std::optional<int> Instances(const Json& object)
{
    const auto& value = Required(object, "instancesOfType");
    if (value.is_null()) return std::nullopt;
    if (!value.is_number_integer())
        throw std::invalid_argument("La frecuencia del modelo del bridge no es entera.");
    const auto number = value.get<long long>();
    if (number < 0 || number > static_cast<long long>(MaximumObjects))
        throw std::invalid_argument("La frecuencia del modelo del bridge esta fuera de rango.");
    return static_cast<int>(number);
}

bool PlacementFile(const std::filesystem::directory_entry& entry)
{
    if (!entry.is_regular_file() || entry.path().extension() != ".json") return false;
    const auto name = entry.path().filename().string();
    return name.size() > std::string_view("-placements.json").size()
        && name.ends_with("-placements.json");
}
}

std::filesystem::path SilverWorldStudioBridge::DefaultContractRoot()
{
    return std::filesystem::path("generated") / "world-studio";
}

SilverWorldStudioContract SilverWorldStudioBridge::Load(const std::filesystem::path& file)
{
    const auto normalized = std::filesystem::absolute(file).lexically_normal();
    const auto document = Json::parse(ReadText(normalized));
    if (!document.is_object()) throw std::invalid_argument("El contrato del bridge no es un objeto JSON.");

    SilverWorldStudioContract result;
    result.File = normalized;
    result.Schema = Required(document, "schema").get<std::string>();
    if (result.Schema != SilverWorldStudioPlacementSchema)
        throw std::invalid_argument("Esquema de contrato del bridge no reconocido.");
    result.ReadOnlySource = Required(document, "readOnlySource").get<bool>();
    if (!result.ReadOnlySource)
        throw std::invalid_argument("El contrato del bridge no declara una fuente read-only.");
    result.Map = Required(document, "map").get<std::string>();
    if (result.Map.empty()) throw std::invalid_argument("El contrato del bridge no identifica el mapa.");
    result.MapNumber = Integer(document, "mapNumber", 0, 96);
    result.MapWorld = Integer(document, "mapWorld", 1, 96);
    result.PlacementCount = Count(document, "placementCount");
    result.ResolvedModelCount = Count(document, "resolvedModelCount");

    const auto& placements = Required(document, "placements");
    if (!placements.is_array() || placements.size() != result.PlacementCount)
        throw std::invalid_argument("La cantidad de placements del bridge no coincide.");

    result.Placements.reserve(placements.size());
    std::size_t resolvedModels = 0;
    for (std::size_t index = 0; index < placements.size(); ++index)
    {
        const auto& value = placements.at(index);
        if (!value.is_object()) throw std::invalid_argument("Un placement del bridge no es un objeto.");
        SilverWorldStudioPlacement placement;
        placement.Index = Integer(value, "index", 0, static_cast<int>(MaximumObjects - 1));
        if (placement.Index != static_cast<int>(index))
            throw std::invalid_argument("Los indices del contrato del bridge no son secuenciales.");
        placement.Type = Integer(value, "type", 0, std::numeric_limits<std::int16_t>::max());
        placement.ModelSequence = Integer(value, "modelSequence", 1, std::numeric_limits<std::int16_t>::max() + 1);
        if (placement.ModelSequence != placement.Type + 1)
            throw std::invalid_argument("La secuencia de modelo no coincide con el tipo del bridge.");
        placement.Model = Model(value);
        if (placement.Model) ++resolvedModels;
        placement.Position = Vector(value, "position");
        placement.Rotation = Vector(value, "rotation");
        placement.Scale = Number(value, "scale");
        if (placement.Scale < MinimumScale || placement.Scale > MaximumScale)
            throw std::invalid_argument("La escala del contrato del bridge esta fuera de rango.");
        placement.InstancesOfType = Instances(value);
        result.Placements.push_back(std::move(placement));
    }
    if (resolvedModels != result.ResolvedModelCount)
        throw std::invalid_argument("Los modelos resueltos del bridge no coinciden.");
    return result;
}

std::optional<SilverWorldStudioContract> SilverWorldStudioBridge::TryLoadForMap(
    const std::filesystem::path& contractRoot, int mapNumber, int mapWorld)
{
    if (!std::filesystem::is_directory(contractRoot)) return std::nullopt;
    RefuseLinks(contractRoot);
    std::optional<SilverWorldStudioContract> result;
    for (const auto& entry : std::filesystem::directory_iterator(contractRoot))
    {
        if (!PlacementFile(entry)) continue;
        auto contract = Load(entry.path());
        if (contract.MapNumber != mapNumber || contract.MapWorld != mapWorld) continue;
        if (result) throw std::invalid_argument("Hay mas de un contrato para el mismo mapa del bridge.");
        result = std::move(contract);
    }
    return result;
}

WorldObjectFile SilverWorldStudioBridge::Apply(const SilverWorldStudioContract& contract,
    const WorldObjectFile& baseline, int expectedMapNumber, int expectedMapWorld)
{
    if (contract.MapNumber != expectedMapNumber || contract.MapWorld != expectedMapWorld)
        throw std::invalid_argument("El contrato del bridge no corresponde al mapa cargado.");
    if (baseline.Map != expectedMapWorld || baseline.Records.size() != contract.Placements.size())
        throw std::invalid_argument("El contrato del bridge no corresponde al OBJ cargado.");

    WorldObjectFile result = baseline;
    for (std::size_t index = 0; index < contract.Placements.size(); ++index)
    {
        const auto& placement = contract.Placements[index];
        WorldObjectRecord record;
        record.Type = static_cast<std::int16_t>(placement.Type);
        record.Position = placement.Position;
        record.Angle = placement.Rotation;
        record.Scale = placement.Scale;
        record.BaselineOnly = baseline.Records[index].BaselineOnly;
        ValidateRecord(record);
        if(record.Type!=baseline.Records[index].Type||record.BaselineOnly!=baseline.Records[index].BaselineOnly)
            throw std::invalid_argument("El bridge no puede cambiar Type ni propiedad baselineOnly.");
        result.Records[index] = record;
    }
    return result;
}


std::vector<SilverWorldStudioDiff> SilverWorldStudioBridge::Diff(const SilverWorldStudioContract& contract,
    const WorldObjectFile& baseline,int expectedMapNumber,int expectedMapWorld)
{
    const auto proposed=Apply(contract,baseline,expectedMapNumber,expectedMapWorld);
    std::vector<SilverWorldStudioDiff> result;
    for(std::size_t i=0;i<proposed.Records.size();++i)if(!(proposed.Records[i]==baseline.Records[i]))
        result.push_back({static_cast<int>(i),baseline.Records[i],proposed.Records[i]});
    return result;
}

std::vector<std::uint8_t> SilverWorldStudioBridge::EncodeCandidate(const SilverWorldStudioContract& contract,
    const WorldObjectFile& baseline, int expectedMapNumber, int expectedMapWorld)
{
    const auto verified = Apply(contract, baseline, expectedMapNumber, expectedMapWorld);
    Json records = Json::array();
    for (const auto& record : verified.Records)
    {
        records.push_back({{"type", record.Type}, {"position", record.Position}, {"rotation", record.Angle},
            {"scale", record.Scale}, {"baselineOnly", record.BaselineOnly}});
    }
    Json root{{"schema", SilverWorldStudioCandidateSchema}, {"mapNumber", expectedMapNumber},
        {"mapWorld", expectedMapWorld}, {"sourceSchema", contract.Schema},
        {"sourceReadOnly", contract.ReadOnlySource}, {"records", records}};
    const auto text = root.dump(2) + "\n";
    if (text.size() > MaximumContractBytes) throw std::length_error("Candidate del bridge supera el limite permitido.");
    return {text.begin(), text.end()};
}

WorldObjectFile SilverWorldStudioBridge::DecodeCandidate(std::span<const std::uint8_t> bytes,
    const WorldObjectFile& baseline, int expectedMapNumber, int expectedMapWorld)
{
    if (bytes.empty() || bytes.size() > MaximumContractBytes) throw std::invalid_argument("Candidate del bridge fuera de limites.");
    Json root;
    try { root = Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())); }
    catch (const Json::exception&) { throw std::invalid_argument("Candidate del bridge JSON invalido."); }
    if (root.value("schema", "") != SilverWorldStudioCandidateSchema
        || root.value("mapNumber", -1) != expectedMapNumber || root.value("mapWorld", -1) != expectedMapWorld
        || root.value("sourceSchema", "") != SilverWorldStudioPlacementSchema || !root.value("sourceReadOnly", false))
        throw std::invalid_argument("Candidate del bridge no corresponde al mapa/fuente.");
    const auto& records = Required(root, "records");
    if (!records.is_array() || records.size() != baseline.Records.size()) throw std::invalid_argument("Candidate del bridge no coincide con OBJ baseline.");
    WorldObjectFile result = baseline;
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        const auto& value = records.at(i); WorldObjectRecord record;
        record.Type = static_cast<std::int16_t>(Integer(value, "type", 0, std::numeric_limits<std::int16_t>::max()));
        record.Position = Vector(value, "position"); record.Angle = Vector(value, "rotation"); record.Scale = Number(value, "scale");
        const auto& baselineOnly = Required(value, "baselineOnly");
        if (!baselineOnly.is_boolean()) throw std::invalid_argument("Candidate bridge baselineOnly invalido.");
        record.BaselineOnly = baselineOnly.get<bool>(); ValidateRecord(record);
        if(record.Type!=baseline.Records[i].Type||record.BaselineOnly!=baseline.Records[i].BaselineOnly)throw std::invalid_argument("Candidate bridge intenta cambiar identidad OBJ.");
        result.Records[i] = record;
    }
    return result;
}}
