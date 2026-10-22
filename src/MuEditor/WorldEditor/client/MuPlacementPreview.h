#pragma once
#ifdef _EDITOR
#include "../core/WorldObjectRecord.h"
#include "Render/Renderer/MuRenderer.h"
#include <optional>
#include <vector>
class BMD;

namespace Silver::WorldEditor
{
// Owns CPU geometry only; never allocates a game object or changes Models[].
class MuPlacementPreview
{
public:
    bool Prepare(std::int16_t type);
    bool Bind(std::int16_t type, const BMD& model);
    bool Box(const WorldObjectRecord& record, std::array<std::array<float,3>,8>& corners);
    void Clear();
    bool MinimumZ(const WorldObjectRecord& record, float& minimum);
    bool Draw(const WorldObjectRecord& record, bool solid = false);
    struct PreviewMeshView { std::uint32_t Texture; std::span<const mu::Vertex3D> Vertices; };
    bool PrepareThumbnail(std::int16_t type, std::vector<PreviewMeshView>& meshes, std::array<float, 3>& low, std::array<float, 3>& high);
private:
    struct Mesh { std::uint32_t Texture; std::vector<mu::Vertex3D> Local, Transformed, World; };
    void Build(std::int16_t type);
    void BuildModel(const BMD& model);
    void Transform(const WorldObjectRecord& record);
    bool TexturesReady() const;
    std::int16_t type_ = -1;
    bool ready_ = false;
    std::vector<Mesh> meshes_;
    std::optional<WorldObjectRecord> transformed_;
    float minimum_ = 0.0f;
};
}
#endif
