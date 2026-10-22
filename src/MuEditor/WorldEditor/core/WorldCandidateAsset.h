#pragma once
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
namespace Silver::WorldEditor
{
struct WorldCandidateAsset
{
    std::string Name;
    std::optional<std::vector<std::uint8_t>> Original;
    std::vector<std::uint8_t> Updated;
};
}
