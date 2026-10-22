#pragma once
#include "WorldCandidateBrowser.h"
#include <string>
#include <map>
#include <vector>
namespace Silver::WorldEditor
{
struct WorldCandidateDifference{std::string Domain,Name,Left,Right;};
struct WorldCandidateSemanticChange{std::string Domain,Text;std::uint64_t Id=0;int Index=-1;int MinX=-1,MinY=-1,MaxX=-1,MaxY=-1;};
struct WorldCandidateSemanticSummary{std::size_t Regions=0,Triggers=0,Lights=0,RuntimeEffects=0,Objects=0;};
struct WorldCandidateChangeStats{std::size_t Added=0,Edited=0,Deleted=0;};
struct WorldCandidateCompareReport{std::vector<WorldCandidateDifference> Differences;std::vector<WorldCandidateSemanticChange> SemanticChanges;std::map<std::string,WorldCandidateChangeStats> Stats;WorldCandidateSemanticSummary LeftSummary,RightSummary;bool Identical()const{return Differences.empty()&&SemanticChanges.empty();}};
class WorldCandidateCompare{public:static WorldCandidateCompareReport Compare(const WorldCandidateVersion&,const WorldCandidateVersion&);};
}
