#pragma once
#include "TerrainBrushDocument.h"
#include "TerrainHeightFile.h"
#include <filesystem>
namespace Silver::WorldEditor
{
class TerrainCandidateStore
{
public:
    TerrainCandidateStore(std::filesystem::path sourceWorld, std::filesystem::path output);
    TerrainBrushDocument NewDocument() const;
    std::filesystem::path Save(const TerrainBrushDocument& document) const;
    const auto& OriginalAttributes() const { return attributes_.Cells; }
private:
    void VerifyOriginals() const;
    std::filesystem::path source_, output_;
    TerrainAttributeFile attributes_, serverAttributes_;
    TerrainHeightFile height_;
    std::vector<std::uint8_t> attributeBytes_, serverBytes_, heightBytes_;
};
}
