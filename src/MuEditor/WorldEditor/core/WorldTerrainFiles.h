#pragma once
#include "WorldTerrainDocument.h"
#include "WorldCandidateAsset.h"
#include <filesystem>
#include <span>
#include <string>

namespace Silver::WorldEditor
{
class WorldTerrainFiles
{
public:
    WorldTerrainFiles(const std::filesystem::path& world, int expectedMap,
        std::span<const std::uint16_t> loadedAttributes);
    WorldTerrainDocument Open() const;
    void OpenInto(WorldTerrainDocument& document) const;
    std::vector<WorldCandidateAsset> Export(const WorldTerrainDocument& document) const;
    const std::string& AttributeName() const { return attributeName_; }
    bool BaselinesStillCurrent() const;
    std::vector<std::pair<std::string,std::string>> BaselineFingerprints() const;
private:
    TerrainAttributeFile attributes_, server_;
    TerrainMappingFile mapping_;
    TerrainHeightFile height_;
    std::vector<std::uint8_t> attributeBytes_, mappingBytes_, heightBytes_, serverBytes_;
    std::string attributeName_;
    std::string mappingName_;
    std::filesystem::path worldPath_;
    int protectedCell_ = -1;
};
}
