#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldOpenMuBackupManifest
{
    static constexpr const char* Schema="silver-world-editor-openmu-backup-v1";
    int MapNumber=-1;
    std::string BaselineSha256;
    std::string CandidateSha256;
    std::size_t BaselineBytes=0;
    std::size_t CandidateBytes=0;
    std::size_t ChangedSpawns=0;
    std::vector<std::uint8_t> Encode() const;
    static WorldOpenMuBackupManifest Decode(std::span<const std::uint8_t> bytes);
    static WorldOpenMuBackupManifest Create(int mapNumber,std::span<const std::uint8_t> baseline,std::span<const std::uint8_t> candidate,std::size_t changedSpawns);
    bool Verify(std::span<const std::uint8_t> baseline,std::span<const std::uint8_t> candidate) const;
    bool BaselineMatches(std::span<const std::uint8_t> currentBaseline) const;
    bool CandidateMatches(std::span<const std::uint8_t> currentCandidate) const;
};
}
