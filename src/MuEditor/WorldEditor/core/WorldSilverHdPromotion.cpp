#include "WorldSilverHdPromotion.h"
#include "Core/Platform/ContentHash.h"
#include "json.hpp"
#include <stdexcept>
namespace Silver::WorldEditor
{
namespace{using Json=nlohmann::json;constexpr std::size_t MaxBytes=64*1024;}
WorldSilverHdPromotionPlan WorldSilverHdPromotion::Prepare(const WorldSilverHdRegistry& registry,const WorldSilverHdAsset& asset)
{
    if(registry.Root().empty()||asset.Sha256.size()!=64)throw std::invalid_argument("Promocion Silver HD requiere registry/hash validos.");
    if(asset.State!=WorldSilverHdState::Remake&&asset.State!=WorldSilverHdState::Candidate&&asset.State!=WorldSilverHdState::Lod)throw std::invalid_argument("Solo REMAKE/CANDIDATE/LOD pueden proponerse para promocion.");
    WorldSilverHdPromotionPlan p;p.LogicalKey=asset.LogicalKey;p.SourceRelativePath=asset.RelativePath;p.SourceSha256=asset.Sha256;p.SourceBytes=asset.Bytes;p.SourceState=asset.State;p.RuntimeState=asset.RuntimeState;p.RuntimeRelativePath=asset.RuntimeRelativePath;p.RequiresRuntimeReplacement=asset.RuntimeState!=WorldSilverHdRuntimeState::Exact;
    if(!asset.RuntimeRelativePath.empty()&&!registry.RuntimeRoot().empty()){const auto live=registry.RuntimeRoot()/asset.RuntimeRelativePath;if(std::filesystem::is_regular_file(live)){p.RuntimeBytes=std::filesystem::file_size(live);p.RuntimeSha256=mu::crypto::Sha256FileHex(live);}}
    return p;
}
bool WorldSilverHdPromotion::VerifySource(const WorldSilverHdRegistry& registry,const WorldSilverHdPromotionPlan& plan)
{
    if(plan.SourceSha256.size()!=64||registry.Root().empty())return false;const auto source=registry.Root()/plan.SourceRelativePath;
    return std::filesystem::is_regular_file(source)&&std::filesystem::file_size(source)==plan.SourceBytes&&mu::crypto::Sha256FileHex(source)==plan.SourceSha256;
}
std::vector<std::uint8_t> WorldSilverHdPromotionPlan::Encode() const
{
    Json root{{"schema",Schema},{"logicalKey",LogicalKey},{"sourceRelativePath",SourceRelativePath},{"sourceSha256",SourceSha256},{"sourceBytes",SourceBytes},{"sourceState",WorldSilverHdRegistry::StateName(SourceState)},{"runtimeState",WorldSilverHdRegistry::RuntimeStateName(RuntimeState)},{"runtimeRelativePath",RuntimeRelativePath},{"runtimeSha256",RuntimeSha256},{"runtimeBytes",RuntimeBytes},{"requiresRuntimeReplacement",RequiresRuntimeReplacement}};
    auto text=root.dump(2)+"\n";if(text.size()>MaxBytes)throw std::length_error("Plan promocion Silver HD demasiado grande.");return {text.begin(),text.end()};
}
WorldSilverHdPromotionPlan WorldSilverHdPromotionPlan::Decode(std::span<const std::uint8_t> bytes)
{
    if(bytes.empty()||bytes.size()>MaxBytes)throw std::invalid_argument("Plan promocion Silver HD fuera de limites.");Json j;try{j=Json::parse(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));}catch(const Json::exception&){throw std::invalid_argument("Plan promocion Silver HD JSON invalido.");}
    if(j.value("schema","")!=Schema)throw std::invalid_argument("Schema promocion Silver HD incompatible.");WorldSilverHdPromotionPlan p;p.LogicalKey=j.value("logicalKey","");p.SourceRelativePath=j.value("sourceRelativePath","");p.SourceSha256=j.value("sourceSha256","");p.SourceBytes=j.value("sourceBytes",std::uintmax_t{});p.RuntimeRelativePath=j.value("runtimeRelativePath","");p.RuntimeSha256=j.value("runtimeSha256","");p.RuntimeBytes=j.value("runtimeBytes",std::uintmax_t{});p.RequiresRuntimeReplacement=j.value("requiresRuntimeReplacement",false);
    const auto ss=j.value("sourceState","");if(ss=="REMAKE")p.SourceState=WorldSilverHdState::Remake;else if(ss=="CANDIDATE")p.SourceState=WorldSilverHdState::Candidate;else if(ss=="LOD")p.SourceState=WorldSilverHdState::Lod;else throw std::invalid_argument("SourceState promocion Silver HD invalido.");
    const auto rs=j.value("runtimeState","");if(rs=="EXACTO")p.RuntimeState=WorldSilverHdRuntimeState::Exact;else if(rs=="VARIANTE")p.RuntimeState=WorldSilverHdRuntimeState::Variant;else if(rs=="AUSENTE")p.RuntimeState=WorldSilverHdRuntimeState::Missing;else p.RuntimeState=WorldSilverHdRuntimeState::Unchecked;
    if(p.LogicalKey.empty()||p.SourceRelativePath.empty()||p.SourceSha256.size()!=64||p.SourceBytes==0)throw std::invalid_argument("Plan promocion Silver HD incompleto.");return p;
}
}
