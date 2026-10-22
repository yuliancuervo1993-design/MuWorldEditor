#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Silver::WorldEditor
{
using EffectId = std::uint64_t;
constexpr EffectId NoEffect = 0;

struct WorldRuntimeEffect
{
    EffectId Id = NoEffect;
    std::string Preset;
    int Kind = 0;
    int Type = 0;
    int SubType = 0;
    std::array<float, 3> Position{};
    float Scale = 1.0f;
    bool Enabled = true;
    bool operator==(const WorldRuntimeEffect&) const = default;
};

struct WorldPointLight
{
    EffectId Id = NoEffect;
    std::array<float, 3> Position{};
    std::array<float, 3> Color{1.0f, 0.85f, 0.55f};
    float Intensity = 1.0f;
    int Radius = 3;
    bool operator==(const WorldPointLight&) const = default;
};

class WorldEffectDocument
{
public:
    static constexpr const char* Schema = "silver-world-editor-effects-v1";

    void Load(const std::filesystem::path& file, int mapId, int worldNumber);
    void Save(const std::filesystem::path& file, int mapId, int worldNumber);
    void Clear();

    const std::vector<WorldPointLight>& Lights() const { return lights_; }
    const std::vector<WorldRuntimeEffect>& RuntimeEffects() const { return runtimeEffects_; }
    WorldPointLight* Find(EffectId id);
    const WorldPointLight* Find(EffectId id) const;

    EffectId AddPointLight(const std::array<float, 3>& position);
    bool UpdatePointLight(EffectId id, const WorldPointLight& value);
    bool Remove(EffectId id);
    EffectId AddRuntimeEffect(const WorldRuntimeEffect& value);
    WorldRuntimeEffect* FindRuntimeEffect(EffectId id);
    const WorldRuntimeEffect* FindRuntimeEffect(EffectId id) const;
    bool UpdateRuntimeEffect(EffectId id, const WorldRuntimeEffect& value);
    bool RemoveRuntimeEffect(EffectId id);

    bool Dirty() const { return lights_ != saved_ || runtimeEffects_ != savedRuntimeEffects_; }
    const std::vector<WorldPointLight>& SavedLights() const { return saved_; }
    const std::vector<WorldRuntimeEffect>& SavedRuntimeEffects() const { return savedRuntimeEffects_; }
    std::uint64_t HistorySerial() const { return historySerial_; }
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    void Undo();
    void Redo();
    void MarkSaved();

private:
    void PushUndo();
    static void Validate(const WorldPointLight& light);
    static void Validate(const WorldRuntimeEffect& effect);

    struct Snapshot { std::vector<WorldPointLight> Lights; std::vector<WorldRuntimeEffect> RuntimeEffects; };
    Snapshot Capture() const { return {lights_, runtimeEffects_}; }
    void Restore(Snapshot snapshot) { lights_=std::move(snapshot.Lights); runtimeEffects_=std::move(snapshot.RuntimeEffects); }
    std::vector<WorldPointLight> lights_;
    std::vector<WorldPointLight> saved_;
    std::vector<WorldRuntimeEffect> runtimeEffects_, savedRuntimeEffects_;
    std::vector<Snapshot> undo_, redo_;
    EffectId nextId_ = 1;
    std::uint64_t historySerial_ = 0;
};
}
