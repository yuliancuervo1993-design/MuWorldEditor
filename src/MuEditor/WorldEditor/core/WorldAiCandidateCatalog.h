#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
struct WorldAiCandidateAsset
{
    std::string Name;
    std::filesystem::path Root;
    std::filesystem::path Bmd;
    std::filesystem::path Texture;
    std::string Status;
    std::string VisualApproval;
    std::size_t Triangles = 0;
    int ExpectedPlacements = -1;
    bool StructuralValidation = false;
    bool NativeLoaderVerified = false;
    std::string LOD0 = "No informado";
    std::string LOD1 = "No informado";
    std::string LOD2 = "No informado";
};

class WorldAiCandidateCatalog
{
public:
    static std::filesystem::path DefaultRoot();
    void Scan(const std::filesystem::path& root = DefaultRoot());
    const std::vector<WorldAiCandidateAsset>& Assets() const { return assets_; }
    const std::string& Error() const { return error_; }
    const std::filesystem::path& Root() const { return root_; }
private:
    std::filesystem::path root_;
    std::vector<WorldAiCandidateAsset> assets_;
    std::string error_;
};
}
