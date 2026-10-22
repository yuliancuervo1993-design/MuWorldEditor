#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
struct WorldModelPackageEntry
{
    std::string Name;
    std::filesystem::path Root;
    std::filesystem::path Bmd;
    std::size_t FileCount = 0;
};

class WorldModelPackage
{
public:
    static constexpr const char* Schema = "silver-world-model-package-v1";

    static std::filesystem::path ExportRoot();
    static std::filesystem::path ImportInbox();
    static std::filesystem::path ImportedRoot();

    static std::filesystem::path ExportOriginal(
        const std::filesystem::path& dataRoot,
        const std::string& relativeModel,
        const std::filesystem::path& exportRoot = ExportRoot());

    static std::filesystem::path ImportCandidate(
        const std::filesystem::path& packageRoot,
        const std::filesystem::path& importedRoot = ImportedRoot());

    static std::vector<WorldModelPackageEntry> Discover(
        const std::filesystem::path& root);
};
}
