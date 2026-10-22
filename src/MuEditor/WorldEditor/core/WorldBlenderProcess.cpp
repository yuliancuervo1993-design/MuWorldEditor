#include "WorldBlenderProcess.h"
#include "WorldFileIo.h"
#include "json.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Silver::WorldEditor
{
WorldBlenderProcess::~WorldBlenderProcess()
{
#ifdef _WIN32
    if (handle_)
    {
        try { Poll(); if (handle_) Record("interactive-user-owned-editor-closed"); } catch (...) {}
        if (handle_) CloseHandle(handle_);
    }
#endif
}

void WorldBlenderProcess::Record(const char* phase) const
{
    nlohmann::json state{{"phase", phase}, {"pid", pid_}, {"startedUnixMs", started_},
        {"executable", PathUtf8(executable_)}, {"interactive", true}};
#ifdef _WIN32
    state["editorPid"] = GetCurrentProcessId();
#endif
    if (exitCode_) state["exitCode"] = *exitCode_;
    std::ofstream file(StreamPath(stateFile_), std::ios::binary | std::ios::trunc);
    file << state.dump(2);
    if (!file) throw std::runtime_error("No se pudo registrar el estado de Blender.");
}

void WorldBlenderProcess::Start(const std::filesystem::path& executable, const std::wstring& arguments,
    const std::filesystem::path& stateFile)
{
    Poll();
    if (Running()) throw std::runtime_error("La sesion Blender anterior sigue abierta.");
    if (!executable.is_absolute() || !std::filesystem::is_regular_file(executable))
        throw std::invalid_argument("Ejecutable Blender invalido.");
    executable_ = executable; stateFile_ = stateFile; exitCode_.reset(); pid_ = 0;
    started_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Record("starting"); // Fail before launching if the package cannot record its process.
#ifdef _WIN32
    std::wstring command = L"\"" + executable.wstring() + L"\" " + arguments;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
    {
        Record("launch-failed");
        throw std::runtime_error("Windows no pudo iniciar Blender.");
    }
    CloseHandle(process.hThread);
    handle_ = process.hProcess; pid_ = process.dwProcessId;
    Record("running");
#else
    Record("unsupported-platform");
    throw std::runtime_error("El puente interactivo solo esta disponible en Windows.");
#endif
}

void WorldBlenderProcess::Poll()
{
#ifdef _WIN32
    if (!handle_) return;
    const auto wait = WaitForSingleObject(handle_, 0);
    if (wait == WAIT_TIMEOUT) return;
    DWORD code = 0;
    if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(handle_, &code))
        throw std::runtime_error("No se pudo consultar el proceso Blender propio.");
    CloseHandle(handle_); handle_ = nullptr; exitCode_ = code;
    Record(code == 0 ? "exited" : "failed");
#endif
}
}
