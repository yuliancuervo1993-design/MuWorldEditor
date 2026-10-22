#pragma once
#ifdef _EDITOR
#include "MuPlacementPreview.h"
#include "../core/WorldLibraryManifest.h"
#include <memory>
#include <map>
#include <filesystem>
class BMD;
namespace Silver::WorldEditor
{
class MuLibraryResources
{
public:
    MuLibraryResources();
    ~MuLibraryResources();
    void Clear();
    int LoadModel(const std::string& path, int requested = -1);
    MuPlacementPreview* Mesh(int type) const;
    std::uintptr_t TexturePreview(const std::string& path);
    int SelectTerrain(const std::string& path, const TerrainMappingFile& mapping);
    const WorldLibraryManifest& Manifest() const { return manifest_; }
    void Restore(const WorldLibraryManifest& manifest);
private:
    struct Model;
    std::map<int,std::unique_ptr<Model>> models_;
    std::map<std::string,unsigned int> previews_;
    std::map<std::string,int> previewFrames_;
    WorldLibraryManifest manifest_;
    static std::filesystem::path TexturePath(const std::string& path);
    void LoadTerrain(int slot, const std::string& path);
};
}
#endif
