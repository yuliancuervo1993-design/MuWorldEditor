#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldModelAsset
{
    std::string Folder, Name, RelativePath, Category;
    std::uintmax_t Bytes = 0;
};
class WorldModelCatalog
{
public:
    void Scan(const std::filesystem::path& dataRoot);
    const std::vector<WorldModelAsset>& Assets() const { return assets_; }
    std::size_t FolderCount() const { return folders_; }
    static std::string Category(std::string name);
    static std::string Lower(std::string text);
    static bool IsGameplayEntityPath(const std::string& path);
    static bool IsObjectFolder(const std::string& name);
    static bool IsLinkedPath(const std::filesystem::path& path);
private:
    std::vector<WorldModelAsset> assets_;
    std::size_t folders_ = 0;
};
}
