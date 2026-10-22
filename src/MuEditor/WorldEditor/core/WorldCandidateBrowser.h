#pragma once
#include "WorldCandidateManifest.h"
#include <filesystem>
#include <optional>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldCandidateVersion{std::filesystem::path Directory;std::filesystem::file_time_type Modified{};std::optional<WorldCandidateManifest> Manifest;std::size_t Files=0;};
class WorldCandidateBrowser{public:static std::vector<WorldCandidateVersion> Discover(const std::filesystem::path& workspace);};
}
