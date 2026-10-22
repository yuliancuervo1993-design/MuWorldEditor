#include "WorldCandidateVerifier.h"
#include "Core/Platform/ContentHash.h"
#include <algorithm>
namespace Silver::WorldEditor
{
namespace{void Add(WorldCandidateVerifyReport&r,WorldCandidateVerifyState s,std::string f,std::string m){r.Issues.push_back({s,std::move(f),std::move(m)});if(static_cast<int>(s)>static_cast<int>(r.State))r.State=s;}
void Check(WorldCandidateVerifyReport&r,const std::filesystem::path&p,const std::string&expected,WorldCandidateVerifyState mismatch){if(!std::filesystem::is_regular_file(p)){Add(r,WorldCandidateVerifyState::Missing,p.filename().string(),"Archivo requerido ausente.");return;}try{const auto actual=mu::crypto::Sha256FileHex(p);if(actual!=expected)Add(r,mismatch,p.filename().string(),"SHA-256 no coincide con manifest.");}catch(...){Add(r,WorldCandidateVerifyState::Missing,p.filename().string(),"No se pudo leer/verificar archivo.");}}}
WorldCandidateVerifyReport WorldCandidateVerifier::Verify(const WorldCandidateManifest&m,const std::filesystem::path&candidateDir,const std::filesystem::path&worldDir){WorldCandidateVerifyReport r;for(const auto&[name,sha]:m.TerrainBaselines)Check(r,worldDir/name,sha,WorldCandidateVerifyState::Stale);for(const auto&[name,sha]:m.Sidecars)Check(r,candidateDir/name,sha,WorldCandidateVerifyState::Tampered);return r;}
const char* WorldCandidateVerifier::StateName(WorldCandidateVerifyState s){switch(s){case WorldCandidateVerifyState::Verified:return"VERIFIED";case WorldCandidateVerifyState::Stale:return"STALE";case WorldCandidateVerifyState::Tampered:return"TAMPERED";case WorldCandidateVerifyState::Missing:return"MISSING";}return"UNKNOWN";}
}
