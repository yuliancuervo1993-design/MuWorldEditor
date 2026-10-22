#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace Silver::WorldEditor
{
using SpawnId = std::uint64_t;
constexpr SpawnId NoSpawn = 0;

struct WorldMonsterSpawn
{
    SpawnId Id = NoSpawn;
    std::string PersistentId;
    std::string MapDefinitionId, MapVariant;
    std::int16_t MonsterNumber = -1;
    std::string Designation;
    std::string ObjectKind;
    std::uint8_t X1 = 0;
    std::uint8_t Y1 = 0;
    std::uint8_t X2 = 0;
    std::uint8_t Y2 = 0;
    std::uint8_t Direction = 0;
    std::int16_t Quantity = 0;
    std::string SpawnTrigger;
    std::uint8_t WaveNumber = 0;
    std::int64_t RespawnDelayMilliseconds = 0;

    bool IsPoint() const { return X1 == X2 && Y1 == Y2; }
    bool operator==(const WorldMonsterSpawn&) const = default;
};

class WorldMonsterSpawnDocument
{
public:
    static constexpr const char* Schema = "silver-openmu-monster-spawns-v2";
    static constexpr const char* CandidateSchema = "silver-world-editor-monster-spawn-candidate-v2";
    static constexpr const char* CandidateFileName = "monster-spawns.candidate.json";
    void Clear();
    void Load(std::span<const std::uint8_t> bytes, int expectedMap);
    void LoadCandidate(std::span<const std::uint8_t> bytes, int expectedMap);
    std::vector<std::uint8_t> EncodeCandidate() const;
    SpawnId AddCandidate(const WorldMonsterSpawn& prototype);
    bool SetCandidate(SpawnId id, const WorldMonsterSpawn& value, std::string label = "Edit Monster Spawn");
    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < history_.size(); }
    bool Undo();
    bool Redo();
    bool Dirty() const { return state_ != savedState_; }
    void MarkSaved() { savedState_ = state_; }
    std::uint64_t HistorySerial() const { return historySerial_; }
    std::string UndoLabel() const;
    const std::vector<WorldMonsterSpawn>& Entries() const { return entries_; }
    const WorldMonsterSpawn* Find(SpawnId id) const;
    const WorldMonsterSpawn* FindBaseline(SpawnId id) const;
    std::vector<SpawnId> ChangedIds() const;
    int MapNumber() const { return mapNumber_; }
    const std::vector<std::pair<std::string,std::string>>& MapVariants() const { return mapVariants_; }
    bool Loaded() const { return mapNumber_ >= 0; }

private:
    struct Snapshot { std::vector<WorldMonsterSpawn> Entries; };
    struct HistoryEntry { Snapshot Before; Snapshot After; std::string Label; std::uint64_t BeforeState=0,AfterState=0; };
    static void Validate(const WorldMonsterSpawn& spawn);
    Snapshot Capture() const { return {entries_}; }
    void Restore(const Snapshot& snapshot);
    void PushHistory(Snapshot before, std::string label);
    std::vector<std::pair<std::string,std::string>> mapVariants_;
    int mapNumber_ = -1;
    std::vector<WorldMonsterSpawn> baseline_;
    std::vector<WorldMonsterSpawn> entries_;
    std::vector<HistoryEntry> history_;
    std::size_t cursor_ = 0;
    std::uint64_t historySerial_=0,state_=0,nextState_=0,savedState_=0;
};
}
