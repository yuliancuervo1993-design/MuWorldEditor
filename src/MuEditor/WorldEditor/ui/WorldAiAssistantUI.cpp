#include "WorldAiAssistantUI.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "json.hpp"

namespace Silver::WorldEditor
{
namespace
{
constexpr const wchar_t* AskScript = L"F:\\SilverAI\\SilverKnowledge\\tools\\Ask-SilverAI.ps1";

std::string Trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring QuotePowerShell(std::wstring value)
{
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    for (const wchar_t ch : value)
    {
        if (ch == L'\'') escaped += L"''";
        else escaped += ch;
    }
    return L"'" + escaped + L"'";
}
}

WorldAiAssistantUI::~WorldAiAssistantUI()
{
#ifdef _WIN32
    if (process_) CloseHandle(process_);
#endif
}

void WorldAiAssistantUI::Reset()
{
#ifdef _WIN32
    if (process_)
    {
        CloseHandle(process_);
        process_ = nullptr;
    }
#endif
    question_[0] = 0;
    answer_.clear();
    status_.clear();
    requestPath_.clear();
    responsePath_.clear();
    requestStatus_.clear();
    lastRequestJson_.clear();
    lastRequestFile_.clear();
    lastRequest_.reset();
    workMode_ = 0;
    assetPrompt_[0] = 0;
    decorationRequest_ = false;
    suggestedNames_.clear();
    structuredCandidateStatus_.clear();structuredCandidateFile_.clear();
}

bool WorldAiAssistantUI::PersistRequest(const WorldAiRequest& request)
{
    try
    {
        const auto root = std::filesystem::path("EditorCandidates") / "AI" / "requests";
        std::filesystem::create_directories(root);
        lastRequestFile_ = root / (request.RequestId + ".json");
        std::ofstream output(lastRequestFile_, std::ios::binary | std::ios::trunc);
        if (!output) { requestStatus_ = "No se pudo guardar el request JSON local."; return false; }
        lastRequestJson_ = request.ToJson();
        output << lastRequestJson_;
        if (!output) { requestStatus_ = "No se pudo completar el request JSON local."; return false; }
        lastRequest_ = request;
        requestStatus_ = "Request " + request.RequestId + " guardado en " + lastRequestFile_.generic_string()
            + ". Candidato-only: no se modifica el mapa.";
        return true;
    }
    catch (const std::exception& error)
    {
        requestStatus_ = std::string("No se pudo guardar el request: ") + error.what();
        return false;
    }
}

bool WorldAiAssistantUI::BuildAndSendRequest(int mapId, int worldNumber,
    const WorldObjectDocument& document, IWorldEditorScene& scene, ObjectId selected,
    const SilverWorldStudioContract* bridge)
{
    const auto mode = workMode_ == 1 ? WorldAiWorkMode::CreateNew
        : (workMode_ == 2 ? WorldAiWorkMode::ReferenceGuided : WorldAiWorkMode::ImproveExisting);
    std::string prompt = Trim(assetPrompt_);
    if (prompt.empty()) prompt = mode == WorldAiWorkMode::ImproveExisting
        ? "Mejora el objeto seleccionado preservando identidad, proporciones y material family MU."
        : "Genera un candidato coherente con el contexto verificado de este mapa.";

    try
    {
        WorldAiRequest request;
        if (mode == WorldAiWorkMode::CreateNew)
        {
            std::array<float, 3> position{};
            bool hasPosition = false;
            if (selected != NoObject)
            {
                if (const auto* record = document.Find(selected)) { position = record->Position; hasPosition = true; }
            }
            if (!hasPosition) hasPosition = scene.CameraMapPosition(position);
            if (!hasPosition) throw std::invalid_argument("Elegí un objeto o una posicion de mapa antes de crear.");
            request = WorldAiRequest::ForPlacement(mapId, worldNumber,
                bridge ? bridge->Map : "Map" + std::to_string(mapId), position, bridge, mode, std::move(prompt));
            request.SelectedObject = selected;
        }
        else
        {
            request = WorldAiRequest::FromSelection(mapId, worldNumber, document, selected, bridge, mode, std::move(prompt));
            const auto libraryPath = scene.LibraryModelPath(request.Type);
            if (!libraryPath.empty()) { request.ResolvedBmd = "Data/" + libraryPath; request.VerifiedReferences.push_back(*request.ResolvedBmd); }
        }
        if (!PersistRequest(request)) return false;

        std::string question =
            "SILVER WORLD EDITOR -> SILVER BRAIN / BLENDER AI.\n"
            "Usa exclusivamente referencias verificadas del contexto local. Distinguí el workMode indicado. "
            "Para IMPROVE_EXISTING preservá identidad, proporciones y material family; para cualquier modo "
            "devolvé un candidato y nunca instales ni reemplaces placements. El presupuesto depende de "
            "expectedPlacements; si la topología no llega, marcá NO CUMPLE y pedí retopología o una fuente simple.\n\n"
            "REQUEST JSON:\n" + lastRequestJson_;
        if (!StartQuestion(std::move(question), false)) return false;
        requestStatus_ += " Silver Brain recibió la solicitud local; Blender/TripoSR todavía no se ejecutan desde este panel.";
        return true;
    }
    catch (const std::exception& error)
    {
        requestStatus_ = std::string("Request rechazado: ") + error.what();
        return false;
    }
}


bool WorldAiAssistantUI::ExtractStructuredCandidate()
{
    structuredCandidateStatus_.clear();structuredCandidateFile_.clear();
    if(answer_.empty()){structuredCandidateStatus_="No hay respuesta para extraer.";return false;}
    auto begin=answer_.find('{'),end=answer_.rfind('}');if(begin==std::string::npos||end==std::string::npos||end<=begin){structuredCandidateStatus_="La respuesta no contiene JSON estructurado.";return false;}
    try
    {
        const auto text=answer_.substr(begin,end-begin+1);const auto json=nlohmann::json::parse(text);
        const auto schema=json.value("schema",std::string{});std::string file;
        if(schema=="silver-world-editor-ai-command-batch-v1")file="command-batch.json";
        else if(schema=="silver-world-editor-ai-gameplay-batch-v1")file="ai-gameplay-batch.json";
        else if(schema=="silver-world-editor-ai-spawn-batch-v1")file="ai-spawn-batch.json";
        else if(schema=="silver-world-editor-ai-effect-batch-v1")file="ai-effect-batch.json";
        else if(schema=="silver-world-editor-ai-terrain-batch-v1")file="ai-terrain-batch.json";
        else{structuredCandidateStatus_="Schema JSON no reconocido por el World Editor.";return false;}
        if(json.value("safety",std::string{})!="CANDIDATE_ONLY; NO_AUTO_APPLY"){structuredCandidateStatus_="JSON rechazado: falta politica Candidate-only.";return false;}
        if(lastRequest_&&json.value("requestId",std::string{})!=lastRequest_->RequestId){structuredCandidateStatus_="JSON rechazado: requestId no coincide con la solicitud activa.";return false;}
        const auto root=std::filesystem::path("EditorCandidates")/"AI";std::filesystem::create_directories(root);structuredCandidateFile_=root/file;
        std::ofstream output(structuredCandidateFile_,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("No se pudo crear Candidate JSON.");
        output<<json.dump(2)<<"\n";if(!output)throw std::runtime_error("Candidate JSON incompleto.");
        structuredCandidateStatus_="Candidate estructurado extraido: "+structuredCandidateFile_.generic_string()+". Aun requiere Cargar + validar + aceptar.";
        return true;
    }
    catch(const std::exception& error){structuredCandidateStatus_=std::string("No se pudo extraer Candidate: ")+error.what();return false;}
}

std::string WorldAiAssistantUI::Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string WorldAiAssistantUI::BuildContext(int mapId, int worldNumber,
    const WorldObjectDocument& document, IWorldEditorScene& scene, ObjectId selected,
    bool includePalette) const
{
    std::ostringstream out;
    out << "CONTEXTO DEL WORLD EDITOR (datos del editor, no inventar):\n";
    out << "- MapId: " << mapId << "\n- WorldN: " << worldNumber << "\n";

    std::array<float,3> camera{};
    if (scene.CameraMapPosition(camera))
        out << "- Camara: X=" << std::fixed << std::setprecision(0) << camera[0]
            << " Y=" << camera[1] << " Z=" << camera[2] << "\n";

    std::size_t editable = 0, opaque = 0;
    for (const auto& entry : document.Entries())
    {
        if (!entry.Record) continue;
        if (entry.Record->BaselineOnly) ++opaque; else ++editable;
    }
    out << "- Objetos editables: " << editable << "\n";
    if (opaque) out << "- Registros legacy preservados/no editables: " << opaque << "\n";

    if (selected != NoObject)
    {
        const auto found = std::find_if(document.Entries().begin(), document.Entries().end(),
            [selected](const auto& entry) { return entry.Id == selected && entry.Record.has_value(); });
        if (found != document.Entries().end())
        {
            const auto& r = *found->Record;
            out << "- Seleccion actual: ObjectId=" << selected << " Type=" << r.Type
                << " X=" << r.Position[0] << " Y=" << r.Position[1] << " Z=" << r.Position[2] << "\n";
        }
    }

    if (includePalette)
    {
        out << "\nMODELOS VALIDOS DEL MAPA (solo podes sugerir nombres de esta lista):\n";
        std::size_t count = 0;
        for (const auto& item : scene.Palette())
        {
            if (count++ >= 420) break;
            out << "- " << item.Name << " | " << item.Category << " | Type " << item.Type << "\n";
        }
    }
    else
    {
        out << "\nMUESTRA DE MODELOS CARGADOS:\n";
        std::size_t count = 0;
        for (const auto& item : scene.Palette())
        {
            if (count++ >= 40) break;
            out << "- " << item.Name << " | " << item.Category << "\n";
        }
    }
    return out.str();
}

bool WorldAiAssistantUI::StartQuestion(std::string question, bool decoration)
{
#ifdef _WIN32
    if (process_) return false;
    if (!std::filesystem::is_regular_file(AskScript))
    {
        status_ = "Silver AI no esta disponible: falta Ask-SilverAI.ps1.";
        return false;
    }

    const auto root = std::filesystem::path("EditorCandidates") / "AI";
    std::filesystem::create_directories(root);
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    requestPath_ = root / ("request-" + std::to_string(stamp) + ".txt");
    responsePath_ = root / ("response-" + std::to_string(stamp) + ".txt");

    {
        std::ofstream request(requestPath_, std::ios::binary | std::ios::trunc);
        if (!request) { status_ = "No se pudo crear la solicitud local de Silver AI."; return false; }
        request << question;
    }

    const auto requestAbs = std::filesystem::absolute(requestPath_).wstring();
    const auto responseAbs = std::filesystem::absolute(responsePath_).wstring();
    std::wstring command =
        L"powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "
        L"\"$q=Get-Content -Raw -LiteralPath " + QuotePowerShell(requestAbs)
        + L"; & " + QuotePowerShell(AskScript)
        + L" -Question $q | Set-Content -LiteralPath " + QuotePowerShell(responseAbs)
        + L" -Encoding UTF8\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
    {
        status_ = "No se pudo iniciar Silver AI local.";
        return false;
    }
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    decorationRequest_ = decoration;
    answer_.clear();
    suggestedNames_.clear();
    status_ = decoration ? "Silver AI esta preparando una propuesta de decoracion..."
                         : "Silver AI esta analizando el mapa...";
    return true;
#else
    (void)question; (void)decoration;
    status_ = "Silver AI local solo esta conectado en Windows.";
    return false;
#endif
}

void WorldAiAssistantUI::Poll()
{
#ifdef _WIN32
    if (!process_) return;
    DWORD exitCode = STILL_ACTIVE;
    if (!GetExitCodeProcess(process_, &exitCode) || exitCode == STILL_ACTIVE) return;
    CloseHandle(process_);
    process_ = nullptr;

    if (exitCode != 0 || !std::filesystem::is_regular_file(responsePath_))
    {
        status_ = "Silver AI termino con error. No se aplico ningun cambio.";
        return;
    }

    std::ifstream input(responsePath_, std::ios::binary);
    answer_.assign(std::istreambuf_iterator<char>(input), {});
    if (answer_.size() >= 3
        && static_cast<unsigned char>(answer_[0]) == 0xEF
        && static_cast<unsigned char>(answer_[1]) == 0xBB
        && static_cast<unsigned char>(answer_[2]) == 0xBF)
        answer_.erase(0, 3);
    status_ = "Respuesta recibida. Ningun cambio se aplica automaticamente.";
#endif
}

void WorldAiAssistantUI::ParseSuggestions(const IWorldEditorScene& scene)
{
    suggestedNames_.clear();
    std::istringstream lines(answer_);
    std::string line;
    while (std::getline(lines, line))
    {
        const auto lowered = Lower(line);
        const auto marker = lowered.find("model:");
        if (marker == std::string::npos) continue;
        const auto requested = Trim(line.substr(marker + 6));
        if (requested.empty()) continue;

        const auto requestedLower = Lower(requested);
        const auto found = std::find_if(scene.Palette().begin(), scene.Palette().end(),
            [&requestedLower](const auto& item)
            {
                return Lower(item.Name) == requestedLower;
            });
        if (found != scene.Palette().end()
            && std::find(suggestedNames_.begin(), suggestedNames_.end(), found->Name) == suggestedNames_.end())
            suggestedNames_.push_back(found->Name);
    }
}

const WorldPaletteItem* WorldAiAssistantUI::Draw(int mapId, int worldNumber,
    const WorldObjectDocument& document, IWorldEditorScene& scene, ObjectId selected,
    const SilverWorldStudioContract* bridge)
{
    Poll();
    if (decorationRequest_ && !answer_.empty() && suggestedNames_.empty())
        ParseSuggestions(scene);

    const WorldPaletteItem* chosen = nullptr;
    ImGui::SetNextWindowPos({760, 60}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({560, 620}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Silver AI | asistente del World Editor"))
    {
        ImGui::TextWrapped("Asistente local: usa Ollama + SilverKnowledge. La IA solo propone; nunca escribe Data, OBJ, ATT, BMD ni SQL directamente.");
        ImGui::SeparatorText("Preguntar sobre el mapa");
        ImGui::InputTextMultiline("##silver_ai_question", question_, sizeof(question_), {-1, 90});
        ImGui::BeginDisabled(process_ != nullptr || question_[0] == 0);
        if (ImGui::Button("Preguntar a Silver AI"))
        {
            std::string prompt = BuildContext(mapId, worldNumber, document, scene, selected, false);
            prompt += "\nPREGUNTA DEL USUARIO:\n";
            prompt += question_;
            StartQuestion(std::move(prompt), false);
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("Decoracion asistida");
        ImGui::TextWrapped("Pide una propuesta usando exclusivamente modelos que ya estan cargados en este mapa. La respuesta se valida contra la paleta antes de ofrecer una vista fantasma.");
        ImGui::BeginDisabled(process_ != nullptr);
        if (ImGui::Button("Sugerir decoracion para esta zona"))
        {
            std::string prompt = BuildContext(mapId, worldNumber, document, scene, selected, true);
            prompt +=
                "\nTAREA: propone una decoracion coherente para la zona actual usando SOLO nombres exactos de MODELOS VALIDOS DEL MAPA. "
                "Explica brevemente la idea y termina con hasta 8 lineas independientes con formato exacto MODEL: <nombre exacto>. "
                "No inventes modelos, no propongas editar archivos ni aplicar cambios automaticamente.";
            StartQuestion(std::move(prompt), true);
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("Silver AI / Blender AI | candidato aislado");
        ImGui::TextWrapped("Construye un request JSON con el contexto del editor y lo entrega a Silver Brain local. El resultado queda como candidato; nunca reemplaza el OBJ ni los placements.");
        constexpr const char* modes[]{"IMPROVE_EXISTING", "CREATE_NEW", "REFERENCE_GUIDED"};
        ImGui::Combo("Modo de trabajo", &workMode_, modes, 3);
        ImGui::InputTextMultiline("##silver_ai_asset_prompt", assetPrompt_, sizeof(assetPrompt_), {-1, 72});
        if (selected != NoObject)
        {
            if (const auto* record = document.Find(selected))
                ImGui::Text("Seleccion #%u | Type %d | pos %.0f, %.0f, %.0f", selected, record->Type,
                    record->Position[0], record->Position[1], record->Position[2]);
        }
        else
            ImGui::TextDisabled("Sin seleccion: CREATE_NEW puede usar la posicion de camara del mapa.");
        ImGui::BeginDisabled(process_ != nullptr || (workMode_ != 1 && selected == NoObject));
        if (ImGui::Button("Construir request y enviar a Silver Brain"))
            BuildAndSendRequest(mapId, worldNumber, document, scene, selected, bridge);
        ImGui::EndDisabled();
        if (!requestStatus_.empty()) ImGui::TextWrapped("%s", requestStatus_.c_str());
        if (lastRequest_ && ImGui::TreeNode("Ultimo request JSON (solo lectura)"))
        {
            ImGui::Text("Modo: %s | placements esperados: %d | BMD: %s",
                WorldAiRequest::ModeName(lastRequest_->WorkMode).c_str(), lastRequest_->ExpectedPlacements,
                lastRequest_->ResolvedBmd ? lastRequest_->ResolvedBmd->c_str() : "no resuelto");
            ImGui::BeginChild("##silver_ai_request_json", {-1, 150}, ImGuiChildFlags_Borders);
            ImGui::TextUnformatted(lastRequestJson_.c_str());
            ImGui::EndChild();
            ImGui::TreePop();
        }

        if (!status_.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", status_.c_str());
        }

        if (!answer_.empty())
        {
            ImGui::SeparatorText("Respuesta");
            ImGui::BeginChild("##silver_ai_answer", {-1, 220}, ImGuiChildFlags_Borders);
            ImGui::TextWrapped("%s", answer_.c_str());
            ImGui::EndChild();
            ImGui::BeginDisabled(process_!=nullptr);
            if(ImGui::Button("Extraer JSON Candidate de la respuesta"))ExtractStructuredCandidate();
            ImGui::EndDisabled();
            if(!structuredCandidateStatus_.empty())ImGui::TextWrapped("%s",structuredCandidateStatus_.c_str());
        }

        if (!suggestedNames_.empty())
        {
            ImGui::SeparatorText("Modelos validados");
            ImGui::TextWrapped("Estos nombres existen realmente en la paleta. Elegir uno activa la misma vista fantasma segura que la Biblioteca MU.");
            for (const auto& name : suggestedNames_)
            {
                const auto found = std::find_if(scene.Palette().begin(), scene.Palette().end(),
                    [&name](const auto& item) { return item.Name == name; });
                if (found == scene.Palette().end()) continue;
                const std::string label = "Vista fantasma: " + name;
                if (ImGui::Button(label.c_str()))
                {
                    chosen = &*found;
                    break;
                }
            }
        }
    }
    ImGui::End();
    return chosen;
}
}
