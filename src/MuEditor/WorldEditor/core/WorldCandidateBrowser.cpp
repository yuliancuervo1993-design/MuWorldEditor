#include "WorldCandidateBrowser.h"
#include "WorldObjectCandidate.h"
#include <algorithm>
namespace Silver::WorldEditor
{
std::vector<WorldCandidateVersion> WorldCandidateBrowser::Discover(const std::filesystem::path&w){std::vector<WorldCandidateVersion> out;if(!std::filesystem::is_directory(w))return out;for(const auto&e:std::filesystem::directory_iterator(w)){if(e.is_symlink()||!e.is_directory())continue;const auto name=e.path().filename().string();if(!name.starts_with("candidate-")||name.ends_with(".partial"))continue;WorldCandidateVersion v;v.Directory=e.path();v.Modified=std::filesystem::last_write_time(e.path());for(const auto&f:std::filesystem::directory_iterator(e.path()))if(f.is_regular_file())++v.Files;const auto manifest=e.path()/"candidate-manifest.json";if(std::filesystem::is_regular_file(manifest)){try{v.Manifest=WorldCandidateManifest::Decode(WorldObjectCandidate::Read(manifest));}catch(...){}}out.push_back(std::move(v));}std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.Modified>b.Modified;});return out;}
}
