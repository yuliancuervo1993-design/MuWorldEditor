#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace Silver::WorldEditor
{
// Tracks the exact interactive child. Closing the editor never kills unsaved Blender work.
class WorldBlenderProcess
{
public:
    WorldBlenderProcess() = default;
    ~WorldBlenderProcess();
    WorldBlenderProcess(const WorldBlenderProcess&) = delete;
    WorldBlenderProcess& operator=(const WorldBlenderProcess&) = delete;
    void Start(const std::filesystem::path& executable, const std::wstring& arguments,
        const std::filesystem::path& stateFile);
    void Poll();
    bool Running() const { return handle_ != nullptr; }
    unsigned long ProcessId() const { return pid_; }
    std::optional<unsigned long> ExitCode() const { return exitCode_; }
private:
    void Record(const char* phase) const;
    void* handle_ = nullptr;
    unsigned long pid_ = 0;
    std::optional<unsigned long> exitCode_;
    std::filesystem::path executable_, stateFile_;
    long long started_ = 0;
};
}
