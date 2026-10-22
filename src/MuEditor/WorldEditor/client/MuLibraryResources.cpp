#include "stdafx.h"
#ifdef _EDITOR
#include "MuLibraryResources.h"
#include "../core/WorldModelInspection.h"
#include "../core/WorldModelCatalog.h"
#include "../core/WorldFileIo.h"
#include "Core/Platform/ContentHash.h"
#include "Render/Models/ZzzBMD.h"
#include "Render/Sprites/GlobalBitmap.h"
#include <filesystem>
#include "imgui.h"
#include <stdexcept>
namespace { std::array<bool,30> editorLibraryTerrain{}; }
extern "C" bool SilverWorldEditor_IsLibraryTerrain(int slot)
{
    return slot>=0 && slot<static_cast<int>(editorLibraryTerrain.size()) && editorLibraryTerrain[static_cast<std::size_t>(slot)];
}
namespace Silver::WorldEditor
{
struct MuLibraryResources::Model
{
    BMD Native;
    MuPlacementPreview Preview;
    std::vector<unsigned int> Textures;
    ~Model() { Native.Release(); for (const auto id : Textures) Bitmaps.UnloadImage(id); }
};
MuLibraryResources::MuLibraryResources() = default;
MuLibraryResources::~MuLibraryResources() { Clear(); }
void MuLibraryResources::Clear()
{
    models_.clear(); editorLibraryTerrain.fill(false);
    for (const auto& [path,id] : previews_) Bitmaps.UnloadImage(id);
    previews_.clear(); previewFrames_.clear(); manifest_ = {};
}
int MuLibraryResources::LoadModel(const std::string& path, int requested)
{
    if (!WorldLibraryManifest::SafePath(path) || WorldModelCatalog::Lower(std::filesystem::path(path).extension().string()) != ".bmd")
        throw std::invalid_argument("Elegir un BMD local de la biblioteca.");
    for (const auto& [type,binding] : manifest_.Models) if (binding.Path == path) return type;
    const int type = requested < 0 ? WorldLibraryManifest::FirstModel + static_cast<int>(models_.size()) : requested;
    if (!WorldLibraryManifest::IsLibraryModel(type) || models_.contains(type))
        throw std::runtime_error("Limite de 256 modelos de biblioteca por mapa.");
    const auto full = std::filesystem::path("Data") / Utf8Path(path);
    const auto inspection = WorldModelInspection::ReadCandidate(full);
    if (inspection.HasMissingDependencies()) throw std::runtime_error("Faltan texturas del BMD: revise sus dependencias antes de colocarlo.");
    auto model = std::make_unique<Model>();
    const auto directory = full.parent_path().wstring() + L"\\";
    if (!model->Native.Open2(directory.c_str(),full.filename().c_str())) throw std::runtime_error("El cargador nativo rechazo el BMD.");
    if (model->Native.NumMeshs != static_cast<int>(inspection.Textures.size())) throw std::runtime_error("Texturas del BMD inconsistentes.");
    for (int i=0;i<model->Native.NumMeshs;++i)
    {
        const auto texturePath = full.parent_path()/Utf8Path(inspection.Textures[static_cast<std::size_t>(i)].Name);
        const auto texture = Bitmaps.LoadImage(texturePath.wstring(),GL_LINEAR,GL_REPEAT);
        if (texture == BITMAP_UNKNOWN) throw std::runtime_error("No se pudo cargar una textura del modelo.");
        model->Textures.push_back(texture); model->Native.IndexTexture[i]=texture;
    }
    if (!model->Preview.Bind(static_cast<std::int16_t>(type),model->Native)) throw std::runtime_error("El modelo no admite colocacion estatica segura.");
    manifest_.Models.emplace(type,WorldLibraryManifest::Binding{path,mu::crypto::Sha256FileHex(full)});
    models_.emplace(type,std::move(model)); return type;
}
MuPlacementPreview* MuLibraryResources::Mesh(int type) const
{
    const auto found=models_.find(type);return found==models_.end()?nullptr:&found->second->Preview;
}
std::filesystem::path MuLibraryResources::TexturePath(const std::string& path)
{
    if (!WorldLibraryManifest::SafePath(path)) throw std::invalid_argument("Ruta de suelo invalida.");
    const auto source=std::filesystem::path("Data")/Utf8Path(path);
    for(auto current=source;!current.empty();current=current.parent_path())
        if(WorldModelCatalog::IsLinkedPath(current))throw std::invalid_argument("No se cargan enlaces de suelo.");
    const auto ext=WorldModelCatalog::Lower(source.extension().string());
    if(ext!=".ozj"&&ext!=".ozt"&&ext!=".jpg"&&ext!=".tga")throw std::invalid_argument("Formato de suelo no admitido.");
    if(!std::filesystem::is_regular_file(source)||std::filesystem::file_size(source)>16*1024*1024)throw std::invalid_argument("Suelo ausente o demasiado grande.");
    auto load=source;if(ext==".ozj")load.replace_extension(".jpg");if(ext==".ozt")load.replace_extension(".tga");return load;
}
std::uintptr_t MuLibraryResources::TexturePreview(const std::string& path)
{
    auto found=previews_.find(path);
    if(found==previews_.end())
    {
        // Bounded previews; the selected material still loads through SelectTerrain.
        if(previews_.size()>=128)
        {
            const int frame=ImGui::GetFrameCount();
            const auto old=std::find_if(previewFrames_.begin(),previewFrames_.end(),[frame](const auto& pair){return pair.second < frame-3;});
            if(old==previewFrames_.end())return 0;
            Bitmaps.UnloadImage(previews_.at(old->first));previews_.erase(old->first);previewFrames_.erase(old);
        }
        const auto texture=Bitmaps.LoadImage(TexturePath(path).wstring(),GL_LINEAR,GL_REPEAT);
        if(texture==BITMAP_UNKNOWN)return 0;
        found=previews_.emplace(path,texture).first;
    }
    previewFrames_[path]=ImGui::GetFrameCount();
    auto* bitmap=Bitmaps.FindTexture(found->second);return bitmap?reinterpret_cast<std::uintptr_t>(bitmap->sdlTexture):0;
}
void MuLibraryResources::LoadTerrain(int slot,const std::string& path)
{
    const auto load=TexturePath(path);
    if(!Bitmaps.LoadImage(BITMAP_MAPTILE+static_cast<unsigned int>(slot),load.wstring(),GL_LINEAR,GL_REPEAT))
        throw std::runtime_error("No se pudo cargar el suelo seleccionado.");
    editorLibraryTerrain[static_cast<std::size_t>(slot)] = true;
}
int MuLibraryResources::SelectTerrain(const std::string& path,const TerrainMappingFile& mapping)
{
    for(const auto& [slot,binding]:manifest_.Terrain)if(binding.Path==path)return slot;
    const int slot=WorldLibraryManifest::FreeTerrainSlot(mapping,manifest_.Terrain);
    if(slot<0)throw std::runtime_error("Este mapa usa sus 30 materiales. No se reemplaza un suelo existente; libere un slot extendido antes de importar otro.");
    LoadTerrain(slot,path);
    manifest_.Terrain.emplace(slot,WorldLibraryManifest::Binding{path,mu::crypto::Sha256FileHex(std::filesystem::path("Data")/Utf8Path(path))});return slot;
}
void MuLibraryResources::Restore(const WorldLibraryManifest& manifest)
{
    for(const auto* entries:{&manifest.Models,&manifest.Terrain})for(const auto& [id,binding]:*entries)
    {
        if(!WorldLibraryManifest::SafePath(binding.Path)||mu::crypto::Sha256FileHex(std::filesystem::path("Data")/Utf8Path(binding.Path))!=binding.Sha256)
            throw std::runtime_error("El recurso guardado cambio: "+binding.Path);
    }
    for(const auto& [id,binding]:manifest.Models)LoadModel(binding.Path,id);
    for(const auto& [id,binding]:manifest.Terrain){LoadTerrain(id,binding.Path);manifest_.Terrain.emplace(id,binding);}
}
}
#endif
