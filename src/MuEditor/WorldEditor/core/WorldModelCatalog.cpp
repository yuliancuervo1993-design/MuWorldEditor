#include "WorldModelCatalog.h"
#include "WorldFileIo.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <set>
#include <string_view>
#include <tuple>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace Silver::WorldEditor
{
namespace
{
bool Link(const std::filesystem::path& path)
{
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) return true;
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
    return false;
}
}
bool WorldModelCatalog::IsLinkedPath(const std::filesystem::path& path)
{
    return Link(StreamPath(path));
}
std::string WorldModelCatalog::Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}
bool WorldModelCatalog::IsGameplayEntityPath(const std::string& path)
{
    auto value=Lower(path); std::replace(value.begin(),value.end(),'\\','/');
    return value.starts_with("monster/") || value.starts_with("npc/")
        || value.starts_with("custom/monster/") || value.starts_with("custom/npc/");
}
bool WorldModelCatalog::IsObjectFolder(const std::string& name)
{
    const auto lower = Lower(name);
    if (!lower.starts_with("object") || lower.size() <= 6 || lower.size() > 9) return false;
    return std::all_of(lower.begin()+6,lower.end(),[](unsigned char ch) { return ch >= '0' && ch <= '9'; });
}
std::string WorldModelCatalog::Category(std::string name)
{
    name = Lower(std::move(name));
    const auto has = [&name](std::string_view token) { return name.find(token) != std::string::npos; };
    if (has("tree")) return "Arboles";
    if (has("grass") || has("flower") || has("plant") || has("bush") || has("vine") || has("leaf"))
        return "Vegetacion";
    if (has("house") || has("building") || has("tent") || has("tower") || has("castle") || has("shop") || has("roof"))
        return "Edificios";
    if (has("wall") || has("fence") || has("gate") || has("door") || has("pillar") || has("column"))
        return "Muros y puertas";
    if (has("stone") || has("rock") || has("cliff") || has("mountain") || has("terrain") || has("ground"))
        return "Rocas y terreno";
    if (has("chair") || has("table") || has("bench") || has("bed") || has("barrel") || has("crate") || has("box") || has("shelf") || has("furniture"))
        return "Muebles";
    if (has("bridge") || has("road") || has("path") || has("stair") || has("ladder"))
        return "Puentes y caminos";
    if (has("lamp") || has("light") || has("torch") || has("candle") || has("flag") || has("banner") || has("statue") || has("sign"))
        return "Luces y decoracion";
    if (has("water") || has("fall") || has("river") || has("pool") || has("smoke") || has("fire") || has("effect") || has("portal") || has("crystal"))
        return "Agua y efectos";
    if (has("ruin") || has("altar") || has("temple") || has("tomb") || has("grave") || has("arch"))
        return "Ruinas y estructuras";
    return "Sin clasificar";
}
void WorldModelCatalog::Scan(const std::filesystem::path& dataRoot)
{
    constexpr std::size_t MaximumAssets = 10000;
    constexpr std::uintmax_t MaximumAssetBytes = 64 * 1024 * 1024;
    if (!std::filesystem::is_directory(dataRoot) || Link(dataRoot)) throw std::invalid_argument("Carpeta Data no valida.");
    std::vector<WorldModelAsset> assets; std::size_t folders = 0;
    for (const auto& directory : std::filesystem::directory_iterator(dataRoot))
    {
        const auto folder = PathUtf8(directory.path().filename());
        const auto lower = Lower(folder);
        const bool modelFolder = IsObjectFolder(folder) || lower == "item" || lower == "player"
            || lower == "monster" || lower == "npc" || lower == "skill";
        if (!modelFolder || !directory.is_directory() || Link(directory.path())) continue;
        ++folders;
        for (const auto& file : std::filesystem::directory_iterator(directory.path()))
        {
            if (!file.is_regular_file() || Link(file.path()) || Lower(PathUtf8(file.path().extension())) != ".bmd") continue;
            if (assets.size() >= MaximumAssets) throw std::length_error("La biblioteca supera 10000 modelos.");
            const auto size = file.file_size(); if (size == 0 || size > MaximumAssetBytes) continue;
            const auto name = PathUtf8(file.path().stem());
            const auto relative = folder + "/" + PathUtf8(file.path().filename());
            const auto category = lower == "item" ? "Items decorativos" : lower == "player" ? "Equipamiento"
                : (lower == "monster" || lower == "npc") ? "Personajes decorativos" : Category(name);
            assets.push_back({folder,name,relative,category,size});
        }
    }

    // Reviewed remake creature packs live outside ObjectN. Expose them to the
    // professional Asset Browser for inspection, but their non-ObjectN folder
    // deliberately keeps normal world-object placement disabled.
    const auto monsterRoot = dataRoot / "Custom";
    if (std::filesystem::is_directory(monsterRoot) && !Link(monsterRoot))
    {
        std::set<std::string> monsterFolders;
        for (const auto& file : std::filesystem::recursive_directory_iterator(monsterRoot))
        {
            if (!file.is_regular_file() || Link(file.path())
                || Lower(PathUtf8(file.path().extension())) != ".bmd")
                continue;
            if (assets.size() >= MaximumAssets)
                throw std::length_error("La biblioteca supera 10000 modelos.");
            const auto size = file.file_size();
            if (size == 0 || size > MaximumAssetBytes) continue;

            const auto relativePath = std::filesystem::relative(file.path(), dataRoot);
            const auto folderPath = relativePath.parent_path();
            const auto folder = PathUtf8(folderPath);
            const auto name = PathUtf8(file.path().stem());
            const auto category = Lower(folder).starts_with("custom/monster") ? "Monstruos Remake" : Category(name);
            assets.push_back({folder, name, PathUtf8(relativePath), category, size});
            monsterFolders.insert(folder);
        }
        folders += monsterFolders.size();
    }
    std::sort(assets.begin(),assets.end(),[](const auto& a,const auto& b) { return std::tie(a.Folder,a.Name)<std::tie(b.Folder,b.Name); });
    assets_ = std::move(assets); folders_ = folders;
}
}
