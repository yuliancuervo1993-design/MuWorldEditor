#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace Silver::WorldEditor
{
// Stable UTF-8 conversion for editor metadata/UI. Never route filesystem names
// through the process ANSI codepage; MU packs can contain non-ASCII filenames.
inline std::string PathUtf8(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

inline std::filesystem::path Utf8Path(std::string_view value)
{
#if defined(__cpp_char8_t)
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char ch : value)
        utf8.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(utf8);
#else
    return std::filesystem::u8path(value.begin(), value.end());
#endif
}

// std::filesystem traversal and stream opening must address the same absolute file,
// including LAB paths longer than the traditional Win32 filename limit.
inline std::filesystem::path StreamPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto absolute = std::filesystem::absolute(path).lexically_normal().wstring();
    if (absolute.starts_with(LR"(\\?\)")) return absolute;
    if (absolute.starts_with(LR"(\\)")) return LR"(\\?\UNC\)" + absolute.substr(2);
    return LR"(\\?\)" + absolute;
#else
    return path;
#endif
}
}
