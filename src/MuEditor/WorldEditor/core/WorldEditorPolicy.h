#pragma once
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace Silver::WorldEditor
{
inline constexpr std::uint16_t LabConnectServerPort = 44406;
inline bool IsAllowedEditorEndpoint(std::wstring_view address, std::uint16_t port)
{
    return port == LabConnectServerPort && (address == L"127.0.0.1" || address == L"localhost");
}
inline bool IsBackgroundEditorRun(std::string_view lab, std::string_view background)
{
    return lab == "1" && background == "1";
}
inline bool BackgroundEditorRequested()
{
    const char* lab = std::getenv("SILVER_WORLD_EDITOR_LAB");
    const char* background = std::getenv("SILVER_WORLD_EDITOR_BACKGROUND");
    return IsBackgroundEditorRun(lab ? lab : "", background ? background : "");
}
}
