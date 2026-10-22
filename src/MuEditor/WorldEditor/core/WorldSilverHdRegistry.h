#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
enum class WorldSilverHdState
{
    Original,
    Remake,
    Lod,
    Candidate,
    Rollback,
    Unclassified
};

enum class WorldSilverHdRuntimeState
{
    Unchecked,
    Exact,
    Variant,
    Missing
};

struct WorldSilverHdAsset
{
    std::filesystem::path Bmd;
    std::string RelativePath;
    std::string Name;
    std::string LogicalKey;
    WorldSilverHdState State = WorldSilverHdState::Unclassified;
    WorldSilverHdRuntimeState RuntimeState = WorldSilverHdRuntimeState::Unchecked;
    std::string Lod;
    std::string Sha256;
    std::string RuntimeRelativePath;
    std::uintmax_t Bytes = 0;
    std::size_t VariantCount = 1;
    std::size_t DuplicateCount = 1;
};

class WorldSilverHdRegistry
{
public:
    static std::filesystem::path EnvironmentRoot();
    static const char* StateName(WorldSilverHdState state);
    static const char* RuntimeStateName(WorldSilverHdRuntimeState state);
    static WorldSilverHdState Classify(std::string relativePath);
    static std::string LogicalKey(std::string relativePath);
    static std::string LodName(std::string relativePath);

    void Scan(const std::filesystem::path& root, const std::filesystem::path& runtimeData = {});

    const std::filesystem::path& Root() const { return root_; }
    const std::filesystem::path& RuntimeRoot() const { return runtimeRoot_; }
    const std::vector<WorldSilverHdAsset>& Assets() const { return assets_; }
    std::size_t Count(WorldSilverHdState state) const;
    std::size_t CountRuntime(WorldSilverHdRuntimeState state) const;
    std::size_t UniquePayloadCount() const { return uniquePayloadCount_; }
    std::size_t HashFailureCount() const { return hashFailureCount_; }

private:
    std::filesystem::path root_;
    std::filesystem::path runtimeRoot_;
    std::vector<WorldSilverHdAsset> assets_;
    std::size_t uniquePayloadCount_ = 0;
    std::size_t hashFailureCount_ = 0;
};
}
