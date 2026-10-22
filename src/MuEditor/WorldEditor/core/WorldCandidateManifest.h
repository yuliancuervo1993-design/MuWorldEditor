#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldCandidateManifest
{
 static constexpr const char* Schema="silver-world-editor-candidate-manifest-v1";
 int MapId=-1,WorldNumber=-1;std::string ObjectBaselineSha256,OpenMuBaselineSha256;
 std::vector<std::pair<std::string,std::string>> TerrainBaselines;
 std::vector<std::pair<std::string,std::string>> Sidecars;
 std::vector<std::uint8_t> Encode()const;
 static WorldCandidateManifest Decode(std::span<const std::uint8_t>);
};
}
