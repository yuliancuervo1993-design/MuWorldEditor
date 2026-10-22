#pragma once
#include "../core/WorldModelInspection.h"
#include "WorldModelThumbnail.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
class WorldModelInspectionUI
{
public:
    void Select(const std::string& relativePath);
    void SelectCandidate(const std::filesystem::path& bmdFile);
    void Clear();
    void Draw() const;
    void DrawThumbnail(float size = 128.0f) const;
private:
    void BuildThumbnail();

    void DrawDependencies() const;
    std::string selected_, error_;
    std::optional<WorldModelInspection> inspection_;
    std::vector<WorldModelThumbnail::Triangle> thumbnail_;
};
}
