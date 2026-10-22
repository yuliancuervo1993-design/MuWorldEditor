#pragma once
#include "WorldObjectDocument.h"
#include "WorldPlacementSurface.h"
#include <utility>

namespace Silver::WorldEditor
{
// Provisional state only. Add() is called exclusively on a fresh primary press.
class WorldPlacementSession
{
public:
    using SceneId = std::uint64_t;
    enum class Status
    {
        Inactive, WaitingForTarget, Ready, Suspended, OutsideMap,
        ModelUnavailable, SurfaceUnavailable, InvalidSurface,
        SceneChanged, DocumentChanged, ObjectLimit
    };
    enum class Support { ModelBase, Pivot };
    static constexpr float RotationStep = 15.0f;
    static constexpr float DefaultGridStep = 50.0f;
    static constexpr float MinimumGridStep = 1.0f;
    static constexpr float MaximumGridStep = 500.0f;
    static constexpr float MinimumPercent = 25.0f;
    static constexpr float MaximumPercent = 400.0f;
    WorldPlacementSession() = default;
    WorldPlacementSession(const WorldPlacementSession&) = delete;
    WorldPlacementSession& operator=(const WorldPlacementSession&) = delete;
    void Select(const WorldObjectRecord& prototype, const WorldObjectDocument& document, SceneId scene);
    void Cancel();
    void Suspend();
    void SetGrid(bool enabled, float step = DefaultGridStep);
    void SetSupport(Support support);
    void SetRepeat(bool enabled) { repeat_ = enabled; }
    void RotateSteps(int steps);
    void ResetRotation();
    void SetScalePercent(float percent);
    std::pair<float, float> ScalePercentRange() const;
    float ScalePercent() const;
    void UpdateTarget(float x, float y, const WorldObjectDocument& document,
        SceneId scene, const WorldPlacementSurface& surface);
    ObjectId PrimaryButton(bool down, WorldObjectDocument& document,
        SceneId scene, const WorldPlacementSurface& surface);
    bool Active() const { return prototype_.has_value(); }
    bool Repeating() const { return repeat_; }
    bool GridEnabled() const { return gridEnabled_; }
    float GridStep() const { return gridStep_; }
    Status CurrentStatus() const { return status_; }
    const std::optional<WorldObjectRecord>& Preview() const { return preview_; }
    const WorldObjectRecord& Transform() const;
private:
    void RequireActive() const;
    void ClearTarget(Status status);
    void Invalidate(Status status);
    bool CheckContext(const WorldObjectDocument& document, SceneId scene);
    bool ResolveTarget(float x, float y, const WorldPlacementSurface& surface);
    bool ResolveSupport(WorldObjectRecord& record, const WorldPlacementSurface& surface);
    std::optional<WorldObjectRecord> prototype_, preview_;
    WorldObjectRecord transform_;
    std::optional<std::array<float, 2>> target_;
    // Identity comparison only; this pointer is never dereferenced or owned.
    const WorldObjectDocument* document_ = nullptr;
    SceneId scene_ = 0;
    std::uint64_t documentRevision_ = 0;
    Status status_ = Status::Inactive;
    Support support_ = Support::ModelBase;
    bool gridEnabled_ = true, repeat_ = false, primaryDown_ = true;
    float gridStep_ = DefaultGridStep;
};
}
