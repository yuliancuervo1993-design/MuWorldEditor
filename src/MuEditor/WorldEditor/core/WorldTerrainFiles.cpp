#include "WorldTerrainFiles.h"
#include "WorldObjectCandidate.h"
#include "WorldFileIo.h"
#include "Core/Platform/ContentHash.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace Silver::WorldEditor
{
namespace
{
constexpr int LorenciaControlCell = 123 * TerrainSide + 135;
constexpr std::uint16_t EditableFlags = 1 | 4 | 8 | 16 | 32 | 64;

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool IsEncryptedAttributeFile(const std::filesystem::path& path)
{
    const auto name = Lower(path.filename().string());
    constexpr std::string_view prefix = "encterrain";
    constexpr std::string_view suffix = ".att";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)
        || name.size() <= prefix.size() + suffix.size()) return false;
    const auto digits = std::string_view(name).substr(prefix.size(),
        name.size() - prefix.size() - suffix.size());
    return std::all_of(digits.begin(), digits.end(),
        [](unsigned char value) { return value >= '0' && value <= '9'; });
}

bool MatchesLoaded(const TerrainAttributeFile& file, std::span<const std::uint16_t> loaded)
{
    if (file.Cells.size() != loaded.size()) return false;
    for (std::size_t index = 0; index < loaded.size(); ++index)
        if ((file.Cells[index] & EditableFlags) != (loaded[index] & EditableFlags))
            return false;
    return true;
}
}

WorldTerrainFiles::WorldTerrainFiles(const std::filesystem::path& world, int expectedMap,
    std::span<const std::uint16_t> loadedAttributes)
{
    worldPath_=std::filesystem::absolute(world).lexically_normal();
    if (expectedMap <= 0 || loadedAttributes.size() != TerrainCells)
        throw std::invalid_argument("Mapa o atributos de terreno invalidos.");

    struct Candidate
    {
        std::filesystem::path Path;
        std::vector<std::uint8_t> Bytes;
        TerrainAttributeFile File;
        bool Canonical = false;
    };
    std::vector<Candidate> candidates;
    const auto canonical = Lower("EncTerrain" + std::to_string(expectedMap) + ".att");

    for (const auto& entry : std::filesystem::directory_iterator(StreamPath(world)))
    {
        if (!entry.is_regular_file() || !IsEncryptedAttributeFile(entry.path())) continue;
        auto bytes = WorldObjectCandidate::Read(entry.path());
        auto file = TerrainAttributeCodec::Decode(bytes);
        if (file.Map != expectedMap || !MatchesLoaded(file, loadedAttributes)) continue;
        candidates.push_back({entry.path(), std::move(bytes), std::move(file),
            Lower(entry.path().filename().string()) == canonical});
    }
    if (candidates.empty())
        throw std::invalid_argument("No se encontro el ATT que coincide con el terreno cargado.");

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
    {
        if (left.Canonical != right.Canonical) return left.Canonical > right.Canonical;
        return Lower(left.Path.filename().string()) < Lower(right.Path.filename().string());
    });
    auto selected = std::move(candidates.front());
    attributeName_ = selected.Path.filename().string();
    attributeBytes_ = std::move(selected.Bytes);
    attributes_ = std::move(selected.File);

    heightBytes_ = WorldObjectCandidate::Read(world / "TerrainHeight.OZB");
    height_ = TerrainHeightFile::DecodeOzb(heightBytes_);

    mappingName_ = "EncTerrain" + std::to_string(expectedMap) + ".map";
    mappingBytes_ = WorldObjectCandidate::Read(world / mappingName_);
    mapping_ = TerrainMappingCodec::Decode(mappingBytes_);
    if (mapping_.Map != expectedMap)
        throw std::invalid_argument("El MAP no corresponde al World esperado.");

    if (expectedMap == 1 && (attributes_.Cells[LorenciaControlCell] & 255) == 5)
        protectedCell_ = LorenciaControlCell;

    if (std::filesystem::exists(StreamPath(world / "Terrain.att")))
    {
        serverBytes_ = WorldObjectCandidate::Read(world / "Terrain.att");
        server_ = TerrainAttributeCodec::Decode(serverBytes_);
    }
}


std::vector<std::pair<std::string,std::string>> WorldTerrainFiles::BaselineFingerprints() const
{
    const auto hash=[](const std::vector<std::uint8_t>& bytes){return mu::crypto::Sha256Hex(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));};
    std::vector<std::pair<std::string,std::string>> result{{attributeName_,hash(attributeBytes_)},{"TerrainHeight.OZB",hash(heightBytes_)},{mappingName_,hash(mappingBytes_)}};
    if(!serverBytes_.empty())result.emplace_back("Terrain.att",hash(serverBytes_));return result;
}
bool WorldTerrainFiles::BaselinesStillCurrent() const
{
    try
    {
        if(WorldObjectCandidate::Read(worldPath_/attributeName_)!=attributeBytes_)return false;
        if(WorldObjectCandidate::Read(worldPath_/"TerrainHeight.OZB")!=heightBytes_)return false;
        if(WorldObjectCandidate::Read(worldPath_/mappingName_)!=mappingBytes_)return false;
        if(!serverBytes_.empty()&&WorldObjectCandidate::Read(worldPath_/"Terrain.att")!=serverBytes_)return false;
        return true;
    }
    catch(...){return false;}
}

WorldTerrainDocument WorldTerrainFiles::Open() const
{
    WorldTerrainDocument document;
    OpenInto(document);
    return document;
}

void WorldTerrainFiles::OpenInto(WorldTerrainDocument& document) const
{
    document.Load(attributes_, height_.Pixels(), mapping_, protectedCell_);
}

std::vector<WorldCandidateAsset> WorldTerrainFiles::Export(const WorldTerrainDocument& document) const
{
    if (document.Active()) throw std::logic_error("Termina la pincelada antes de guardar.");
    if (document.Attributes().Map != attributes_.Map
        || document.Attributes().Encoding != attributes_.Encoding)
        throw std::invalid_argument("El formato de terreno cambio.");
    if (!document.Mapping() || document.Mapping()->Map != mapping_.Map)
        throw std::invalid_argument("El mapa de materiales cambio o no fue cargado.");

    std::vector<WorldCandidateAsset> result;
    result.push_back({attributeName_, attributeBytes_,
        TerrainAttributeCodec::Encode(document.Attributes())});
    result.push_back({"TerrainHeight.OZB", heightBytes_, height_.Encode(document.Heights())});
    result.push_back({"TerrainHeight-preview.bmp", std::nullopt,
        height_.ToBitmap(document.Heights())});
    result.push_back({mappingName_, mappingBytes_, TerrainMappingCodec::Encode(*document.Mapping())});

    if (!serverBytes_.empty())
    {
        auto server = server_;
        for (std::size_t i = 0; i < attributes_.Cells.size(); ++i)
        {
            const auto changed = static_cast<std::uint16_t>(
                attributes_.Cells[i] ^ document.Attributes().Cells[i]);
            server.Cells[i] = static_cast<std::uint16_t>(
                (server.Cells[i] & ~changed) | (document.Attributes().Cells[i] & changed));
        }
        result.push_back({"Terrain.att", serverBytes_, TerrainAttributeCodec::Encode(server)});
    }
    return result;
}
}
