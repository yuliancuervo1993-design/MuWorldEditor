#include "stdafx.h"
#ifdef _EDITOR
#include "MuWorldScene.h"
#include "../core/WorldModelCatalog.h"
#include "../core/WorldFileIo.h"
#include "Core/Platform/ContentHash.h"
#include <filesystem>
#include <limits>
#include <algorithm>
#include <cmath>
namespace Silver::WorldEditor
{
void MuWorldScene::AddLibraryPalette(int type,const std::string& path)
{
    if (std::any_of(palette_.begin(),palette_.end(),[type](const auto& item){return item.Type==type;}))return;
    WorldPaletteItem item;item.Type=static_cast<std::int16_t>(type);item.Prototype.Type=item.Type;
    item.Name=PathUtf8(Utf8Path(path).stem());item.Category=path.starts_with("Item/")?"Items decorativos":WorldModelCatalog::Category(item.Name);
    palette_.push_back(std::move(item));
}
const WorldPaletteItem* MuWorldScene::LoadLibraryModel(const std::string& path)
{
    if(!attached_)throw std::runtime_error("Abra primero un mapa.");
    const int type=library_.LoadModel(path);AddLibraryPalette(type,path);
    for(const auto& item:palette_)if(item.Type==type)return &item;
    return nullptr;
}
std::string MuWorldScene::LibraryModelPath(int type) const
{
    const auto& models=library_.Manifest().Models;const auto found=models.find(type);
    return found==models.end()?std::string{}:found->second.Path;
}
std::uintptr_t MuWorldScene::LibraryTexturePreview(const std::string& path)
{
    try{return library_.TexturePreview(path);}catch(const std::exception&){return 0;}
}
int MuWorldScene::UseLibraryTerrain(const std::string& path,const TerrainMappingFile& mapping)
{
    if(!attached_)throw std::runtime_error("Abra primero un mapa.");return library_.SelectTerrain(path,mapping);
}
WorldObjectFile MuWorldScene::ExportNativeObjects(const WorldObjectDocument& document) const
{
    auto file=document.Export();
    std::erase_if(file.Records,[this](const auto& record){return library_.Mesh(record.Type)!=nullptr;});return file;
}
std::vector<std::uint8_t> MuWorldScene::ExportLibrary(const WorldObjectDocument& document,const std::string& baseline,const TerrainMappingFile* mapping) const
{
    auto manifest=library_.Manifest();
    if(mapping && !manifest.Terrain.empty())
    {
        const auto file=std::filesystem::path("Data")/("World"+std::to_string(worldNumber_))/("EncTerrain"+std::to_string(worldNumber_)+".map");
        manifest.MappingBytes=TerrainMappingCodec::Encode(*mapping);manifest.MappingBaseline=mu::crypto::Sha256FileHex(file);
    }
    for(const auto& entry:document.Entries())if(entry.Record&&manifest.Models.contains(entry.Record->Type))manifest.Objects.push_back(*entry.Record);
    return manifest.Encode(worldId_,baseline);
}
void MuWorldScene::RestoreLibrary(std::span<const std::uint8_t> bytes,WorldObjectDocument& document,const std::string& baseline)
{
    const auto manifest=WorldLibraryManifest::Decode(bytes,worldId_,baseline);
    library_.Restore(manifest);
    for(const auto& [type,binding]:manifest.Models)AddLibraryPalette(type,binding.Path);
    if(!manifest.Objects.empty())document.AddMany(manifest.Objects,"Restaurar objetos de biblioteca");
    Synchronize(document);
}
bool MuWorldScene::DrawLibraryObjects()
{
    for(std::size_t index=0;index<bindings_.size();++index)
    {
        const auto& record=bindings_[index].Last;
        if(!record||hiddenIds_.contains(static_cast<ObjectId>(index+1)))continue;
        if(auto* mesh=library_.Mesh(record->Type))if(!mesh->Draw(*record,true))return false;
    }
    return true;
}
ObjectId MuWorldScene::PickLibrary(const std::array<float,3>& origin,const std::array<float,3>& target) const
{
    ObjectId result=NoObject;float nearest=std::numeric_limits<float>::max();
    for(std::size_t index=0;index<bindings_.size();++index)
    {
        const auto id=static_cast<ObjectId>(index+1);const auto& record=bindings_[index].Last;
        if(!record||!library_.Mesh(record->Type)||hiddenIds_.contains(id)||nonSelectable_.contains(id))continue;
        std::array<std::array<float,3>,8> corners{};if(!Bounds(id,corners))continue;
        float enter=0,leave=std::numeric_limits<float>::max();bool hit=true;
        for(int axis=0;axis<3;++axis)
        {
            const float delta=target[axis]-origin[axis],low=corners[0][axis],high=corners[7][axis];
            if(std::abs(delta)<0.00001f){if(origin[axis]<low||origin[axis]>high)hit=false;continue;}
            float a=(low-origin[axis])/delta,b=(high-origin[axis])/delta;if(a>b)std::swap(a,b);
            enter=std::max(enter,a);leave=std::min(leave,b);
        }
        if(hit&&enter<=leave&&enter<nearest){nearest=enter;result=id;}
    }
    return result;
}
}
#endif
