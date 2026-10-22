#pragma once
#include "WorldModelPreview.h"
#include <filesystem>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
class WorldModelInspection
{
public:
    enum class TextureState { PackedFilePresent, Missing, RawOnly, UnsupportedName };
    struct Texture
    {
        std::string Name, RelativePath;
        TextureState State = TextureState::Missing;
        std::uintmax_t Bytes = 0;
    };
    static WorldModelInspection Read(const std::filesystem::path& dataRoot, const std::string& relativeModel);
    // Read-only inspection for an isolated AI candidate; never registers the BMD in the game.
    static WorldModelInspection ReadCandidate(const std::filesystem::path& bmdFile);
    bool HasMissingDependencies() const;
    std::string RelativePath;
    std::uintmax_t Bytes = 0;
    WorldModelPreview Model;
    std::vector<Texture> Textures;
};
}
