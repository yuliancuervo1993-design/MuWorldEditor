#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace Silver::WorldEditor
{
using GateId = std::uint64_t;
constexpr GateId NoGate = 0;
struct WorldGate
{
    GateId Id = NoGate;
    std::int16_t Number = -1;
    std::int16_t LevelRequirement = 0;
    std::uint8_t X1=0,Y1=0,X2=0,Y2=0;
    GateId TargetGateId = NoGate;
    int TargetMap = -1;
    std::uint8_t TargetX1=0,TargetY1=0,TargetX2=0,TargetY2=0,TargetDirection=0;
    bool operator==(const WorldGate&) const = default;
};
class WorldGateDocument
{
public:
    static constexpr const char* Schema = "silver-openmu-gates-v1";
    static constexpr const char* CandidateSchema = "silver-world-editor-gates-candidate-v1";
    static constexpr const char* CandidateFileName = "gates.candidate.json";
    void Clear();
    void Load(std::span<const std::uint8_t> bytes,int expectedMap);
    bool SetCandidate(GateId id, const WorldGate& value);
    std::vector<std::uint8_t> EncodeCandidate() const;
    void LoadCandidate(std::span<const std::uint8_t> bytes);
    static void Validate(const WorldGate& gate);
    bool Undo();
    bool Redo();
    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < history_.size(); }
    bool Dirty() const { return state_ != savedState_; }
    void MarkSaved() { savedState_ = state_; }
    std::uint64_t HistorySerial() const { return historySerial_; }
    std::uint64_t Revision() const { return revision_; }
    const std::string& BaselineSha256() const { return baselineSha256_; }
    const std::vector<WorldGate>& Entries() const { return entries_; }
    const std::vector<WorldGate>& BaselineEntries() const { return baseline_; }
    const WorldGate* Find(GateId id) const;
    bool Loaded() const { return mapNumber_>=0; }
private:
    int mapNumber_=-1;
    std::vector<WorldGate> entries_;
    std::vector<WorldGate> baseline_;
    struct Change { WorldGate Before, After; std::uint64_t BeforeState, AfterState; };
    std::vector<Change> history_;
    std::size_t cursor_ = 0;
    std::uint64_t historySerial_ = 0, revision_ = 0, state_ = 0, savedState_ = 0, nextState_ = 0;
    std::string baselineSha256_;
};
}
