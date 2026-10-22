#pragma once
#include "WorldObjectCodec.h"
#include "WorldCandidateAsset.h"
#include <filesystem>

namespace Silver::WorldEditor
{
class WorldObjectCandidate
{
public:
    WorldObjectCandidate(std::filesystem::path baseline, std::filesystem::path workspace);
    const WorldObjectFile& Baseline() const { return baseline_; }
    const std::filesystem::path& Workspace() const { return workspace_; }
    const std::filesystem::path& BaselinePath() const { return baselinePath_; }
    std::string BaselineSha256() const;
    bool BaselineStillCurrent() const;
    std::filesystem::path Save(const WorldObjectFile& document, std::span<const WorldCandidateAsset> assets = {}) const;
    static std::vector<std::uint8_t> Read(const std::filesystem::path& path);
private:
    void CheckPaths() const;
    void VerifyAssets(std::span<const WorldCandidateAsset> assets) const;
    std::filesystem::path baselinePath_, workspace_;
    std::vector<std::uint8_t> originalBytes_;
    WorldObjectFile baseline_;
};
}
