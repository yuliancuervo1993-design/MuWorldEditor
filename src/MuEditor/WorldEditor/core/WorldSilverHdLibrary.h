#pragma once
#include <filesystem>
#include <set>
#include <string>

namespace Silver::WorldEditor::SilverHdLibrary
{
inline constexpr const char* ManifestName = "silver-hd-library.json";
inline constexpr const char* HashVariable = "SILVER_WORLD_EDITOR_LIBRARY_SHA256";
// Empty means the original worktree contract; integrated roots return verified BMD paths.
std::set<std::string> ValidateRoot(const std::filesystem::path& root);
}
