#pragma once
#include "WorldCandidateManifest.h"
#include <filesystem>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
enum class WorldCandidateVerifyState{Verified,Stale,Tampered,Missing};
struct WorldCandidateVerifyIssue{WorldCandidateVerifyState State;std::string File,Message;};
struct WorldCandidateVerifyReport{WorldCandidateVerifyState State=WorldCandidateVerifyState::Verified;std::vector<WorldCandidateVerifyIssue> Issues;bool Verified()const{return State==WorldCandidateVerifyState::Verified;}};
class WorldCandidateVerifier
{
public:
 static WorldCandidateVerifyReport Verify(const WorldCandidateManifest&,const std::filesystem::path& candidateDir,const std::filesystem::path& worldDir);
 static const char* StateName(WorldCandidateVerifyState);
};
}
