#include "WorldGlobalAssetRegistry.h"
#include "WorldFileIo.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace Silver::WorldEditor
{
namespace
{
std::optional<int> ParseWorld(std::string folder)
{
    folder = WorldModelCatalog::Lower(std::move(folder));
    if (!folder.starts_with("world") || folder.size() <= 5) return std::nullopt;
    int value = 0;
    for (std::size_t index = 5; index < folder.size(); ++index)
    {
        const unsigned char ch = static_cast<unsigned char>(folder[index]);
        if (ch < '0' || ch > '9') return std::nullopt;
        value = value * 10 + static_cast<int>(ch - '0');
        if (value > 9999) return std::nullopt;
    }
    return value > 0 ? std::optional<int>{value} : std::nullopt;
}

std::string TwoDigits(int value)
{
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

void AppendUnique(std::vector<int>& values, int value)
{
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

void AppendUnique(std::vector<std::string>& values, const std::string& value)
{
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}
}

std::string WorldGlobalAssetRegistry::TerrainKey(std::string nameOrPath)
{
    std::replace(nameOrPath.begin(), nameOrPath.end(), '\\', '/');
    const auto slash = nameOrPath.find_last_of('/');
    if (slash != std::string::npos) nameOrPath.erase(0, slash + 1);
    const auto dot = nameOrPath.find_last_of('.');
    if (dot != std::string::npos) nameOrPath.resize(dot);
    return WorldModelCatalog::Lower(std::move(nameOrPath));
}

std::optional<int> WorldGlobalAssetRegistry::TerrainSlot(std::string nameOrPath)
{
    const auto key = TerrainKey(std::move(nameOrPath));
    if (key == "tilegrass01") return 0;
    if (key == "tilegrass02") return 1;
    if (key == "tileground01" || key == "alphatileground01") return 2;
    if (key == "tileground02" || key == "alphatileground02") return 3;
    if (key == "tileground03" || key == "alphatileground03") return 4;
    if (key == "tilewater01") return 5;
    if (key == "tilewood01") return 6;
    for (int rock = 1; rock <= 7; ++rock)
        if (key == "tilerock" + TwoDigits(rock)) return 6 + rock;
    for (int extended = 1; extended <= 16; ++extended)
        if (key == "exttile" + TwoDigits(extended)) return 13 + extended;
    return std::nullopt;
}

std::string WorldGlobalAssetRegistry::TerrainCategory(std::string nameOrPath)
{
    const auto key = TerrainKey(std::move(nameOrPath));
    if (key.starts_with("tilegrass")) return "Cesped";
    if (key.starts_with("tileground") || key.starts_with("alphatileground")) return "Suelo";
    if (key.starts_with("tilewater")) return "Agua";
    if (key.starts_with("tilewood")) return "Madera";
    if (key.starts_with("tilerock")) return "Roca";
    if (key.starts_with("exttile")) return "Extendido";
    return "Especial";
}

std::string WorldGlobalAssetRegistry::TerrainDisplayName(std::string nameOrPath)
{
    const auto key = TerrainKey(std::move(nameOrPath));
    const auto slot = TerrainSlot(key);
    if (key.starts_with("tilegrass")) return "Cesped " + key.substr(std::string("tilegrass").size());
    if (key.starts_with("tileground")) return "Suelo " + key.substr(std::string("tileground").size());
    if (key.starts_with("alphatileground")) return "Suelo alpha " + key.substr(std::string("alphatileground").size());
    if (key.starts_with("tilewater")) return "Agua " + key.substr(std::string("tilewater").size());
    if (key.starts_with("tilewood")) return "Madera " + key.substr(std::string("tilewood").size());
    if (key.starts_with("tilerock")) return "Roca " + key.substr(std::string("tilerock").size());
    if (key.starts_with("exttile")) return "Extendido " + key.substr(std::string("exttile").size());
    return slot ? "Material " + std::to_string(*slot) : key;
}

void WorldGlobalAssetRegistry::Scan(const std::filesystem::path& dataRoot,
    const std::filesystem::path& importedRoot)
{
    models_.Scan(dataRoot);
    imported_ = WorldModelPackage::Discover(importedRoot);
    terrain_.clear();
    terrainWorlds_ = 0;

    std::error_code error;
    if (!std::filesystem::is_directory(StreamPath(dataRoot), error) || error
        || WorldModelCatalog::IsLinkedPath(dataRoot))
        throw std::invalid_argument("Data no es una raiz valida para el Global Asset Registry.");

    std::map<std::string, std::size_t> byKey;
    std::set<int> worlds;
    constexpr std::size_t MaximumTerrainFiles = 8192;
    std::size_t scanned = 0;

    for (const auto& directory : std::filesystem::directory_iterator(StreamPath(dataRoot)))
    {
        if (!directory.is_directory() || WorldModelCatalog::IsLinkedPath(directory.path())) continue;
        const auto world = ParseWorld(PathUtf8(directory.path().filename()));
        if (!world) continue;
        worlds.insert(*world);

        for (const auto& file : std::filesystem::directory_iterator(StreamPath(directory.path())))
        {
            if (!file.is_regular_file() || WorldModelCatalog::IsLinkedPath(file.path())) continue;
            if (++scanned > MaximumTerrainFiles)
                throw std::length_error("El Global Asset Registry supera el limite de archivos de terreno.");

            const auto extension = WorldModelCatalog::Lower(PathUtf8(file.path().extension()));
            if (extension != ".ozj" && extension != ".ozt" && extension != ".tga") continue;
            const auto slot = TerrainSlot(PathUtf8(file.path().filename()));
            if (!slot) continue;

            const auto key = TerrainKey(PathUtf8(file.path().filename()));
            const auto identity = PathUtf8(std::filesystem::relative(file.path(), dataRoot));
            auto found = byKey.find(identity);
            if (found == byKey.end())
            {
                WorldTerrainMaterialAsset asset;
                asset.Slot = *slot;
                asset.Key = key;
                asset.Identity = identity;
                asset.Name = "World" + std::to_string(*world) + " / " + TerrainDisplayName(key);
                asset.Category = TerrainCategory(key);
                terrain_.push_back(std::move(asset));
                found = byKey.emplace(identity, terrain_.size() - 1).first;
            }

            auto& asset = terrain_[found->second];
            ++asset.Occurrences;
            AppendUnique(asset.Worlds, *world);
            AppendUnique(asset.RelativePaths,
                PathUtf8(std::filesystem::relative(file.path(), dataRoot)));
        }
    }

    terrainWorlds_ = worlds.size();
    for (auto& asset : terrain_)
    {
        std::sort(asset.Worlds.begin(), asset.Worlds.end());
        std::sort(asset.RelativePaths.begin(), asset.RelativePaths.end());
    }
    std::sort(terrain_.begin(), terrain_.end(), [](const auto& left, const auto& right) {
        return std::tie(left.Category, left.Name, left.Identity) < std::tie(right.Category, right.Name, right.Identity);
    });
}
}
