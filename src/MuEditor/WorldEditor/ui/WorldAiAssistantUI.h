#pragma once
#include "IWorldEditorScene.h"
#include "../core/WorldObjectDocument.h"
#include "../core/WorldAiRequest.h"
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace Silver::WorldEditor
{
class WorldAiAssistantUI
{
public:
    ~WorldAiAssistantUI();
    const WorldPaletteItem* Draw(int mapId, int worldNumber,
        const WorldObjectDocument& document, IWorldEditorScene& scene, ObjectId selected,
        const SilverWorldStudioContract* bridge = nullptr);
    void Reset();
private:
    bool StartQuestion(std::string question, bool decoration);
    void Poll();
    void ParseSuggestions(const IWorldEditorScene& scene);
    bool PersistRequest(const WorldAiRequest& request);
    bool BuildAndSendRequest(int mapId, int worldNumber, const WorldObjectDocument& document,
        IWorldEditorScene& scene, ObjectId selected, const SilverWorldStudioContract* bridge);
    std::string BuildContext(int mapId, int worldNumber,
        const WorldObjectDocument& document, IWorldEditorScene& scene, ObjectId selected,
        bool includePalette) const;
    static std::string Lower(std::string value);
    bool ExtractStructuredCandidate();

    char question_[768]{};
    std::string answer_;
    std::string status_;
    std::string requestStatus_;
    std::string lastRequestJson_;
    std::filesystem::path lastRequestFile_;
    std::optional<WorldAiRequest> lastRequest_;
    int workMode_ = 0;
    char assetPrompt_[768]{};
    std::filesystem::path requestPath_, responsePath_;
    bool decorationRequest_ = false;
    std::vector<std::string> suggestedNames_;
    std::string structuredCandidateStatus_;
    std::filesystem::path structuredCandidateFile_;
#ifdef _WIN32
    HANDLE process_ = nullptr;
#endif
};
}
