#include "WorldOpenMuBackupManifest.h"
#include "Core/Platform/ContentHash.h"
#include "json.hpp"
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace{using Json=nlohmann::json;constexpr std::size_t MaxManifestBytes=64*1024;std::string Hash(std::span<const std::uint8_t> bytes){return mu::crypto::Sha256Hex(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}}
WorldOpenMuBackupManifest WorldOpenMuBackupManifest::Create(int map,std::span<const std::uint8_t> baseline,std::span<const std::uint8_t> candidate,std::size_t changed)
{
    if(map<0||baseline.empty()||candidate.empty())throw std::invalid_argument("Backup OpenMU requiere mapa y payloads validos.");
    WorldOpenMuBackupManifest m;m.MapNumber=map;m.BaselineSha256=Hash(baseline);m.CandidateSha256=Hash(candidate);m.BaselineBytes=baseline.size();m.CandidateBytes=candidate.size();m.ChangedSpawns=changed;return m;
}
std::vector<std::uint8_t> WorldOpenMuBackupManifest::Encode() const
{
    if(MapNumber<0||BaselineSha256.size()!=64||CandidateSha256.size()!=64)throw std::invalid_argument("Manifest Backup OpenMU invalido.");
    Json root{{"schema",Schema},{"mapNumber",MapNumber},{"baselineSha256",BaselineSha256},{"candidateSha256",CandidateSha256},{"baselineBytes",BaselineBytes},{"candidateBytes",CandidateBytes},{"changedSpawns",ChangedSpawns}};
    auto text=root.dump(2)+"\n";return {text.begin(),text.end()};
}
WorldOpenMuBackupManifest WorldOpenMuBackupManifest::Decode(std::span<const std::uint8_t> bytes)
{
    if(bytes.empty()||bytes.size()>MaxManifestBytes)throw std::invalid_argument("Manifest Backup OpenMU fuera de limites.");Json root;
    try{root=Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}catch(const Json::exception&){throw std::invalid_argument("Manifest Backup OpenMU JSON invalido.");}
    if(root.value("schema","")!=Schema)throw std::invalid_argument("Schema Backup OpenMU incompatible.");WorldOpenMuBackupManifest m;
    m.MapNumber=root.value("mapNumber",-1);m.BaselineSha256=root.value("baselineSha256",std::string{});m.CandidateSha256=root.value("candidateSha256",std::string{});
    m.BaselineBytes=root.value("baselineBytes",std::size_t{});m.CandidateBytes=root.value("candidateBytes",std::size_t{});m.ChangedSpawns=root.value("changedSpawns",std::size_t{});
    if(m.MapNumber<0||m.BaselineSha256.size()!=64||m.CandidateSha256.size()!=64)throw std::invalid_argument("Manifest Backup OpenMU incompleto.");return m;
}
bool WorldOpenMuBackupManifest::BaselineMatches(std::span<const std::uint8_t> currentBaseline) const
{return currentBaseline.size()==BaselineBytes&&Hash(currentBaseline)==BaselineSha256;}
bool WorldOpenMuBackupManifest::CandidateMatches(std::span<const std::uint8_t> currentCandidate) const
{return currentCandidate.size()==CandidateBytes&&Hash(currentCandidate)==CandidateSha256;}
bool WorldOpenMuBackupManifest::Verify(std::span<const std::uint8_t> baseline,std::span<const std::uint8_t> candidate) const
{return BaselineMatches(baseline)&&CandidateMatches(candidate);}
}
