#pragma once
#include <filesystem>
#include <span>
#include <cstdint>
namespace Silver::WorldEditor::GateCandidateFile
{
void Save(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
}
