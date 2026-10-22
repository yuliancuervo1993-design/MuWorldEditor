#include "WorldSilverHdRegistry.h"
#include "WorldSilverHdLibrary.h"
#include "WorldFileIo.h"
#include "WorldModelCatalog.h"
#include "Core/Platform/ContentHash.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace Silver::WorldEditor
{
namespace
{
constexpr std::size_t MaximumAssets = 10000;
constexpr std::uintmax_t MaximumAssetBytes = 64 * 1024 * 1024;

std::string Normalize(std::string text)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    return WorldModelCatalog::Lower(std::move(text));
}

bool Contains(const std::string& text, const char* token)
{
    return text.find(token) != std::string::npos;
}

bool StartsWithObjectNumber(const std::string& name)
{
    if (!name.starts_with("object") || name.size() <= 6) return false;
    return std::all_of(name.begin() + 6, name.end(), [](unsigned char ch) {
        return ch >= '0' && ch <= '9';
    });
}

std::string StripSuffix(std::string text, const std::string& suffix)
{
    if (text.ends_with(suffix)) text.resize(text.size() - suffix.size());
    return text;
}

std::string FolderAnchor(const std::string& relative)
{
    std::string normalized = Normalize(relative);
    std::size_t start = 0;
    while (start < normalized.size())
    {
        const auto end = normalized.find('/', start);
        const auto part = normalized.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (WorldModelCatalog::IsObjectFolder(part)) return part;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    const auto slash = normalized.find_last_of('/');
    if (slash == std::string::npos) return {};
    const auto prior = normalized.find_last_of('/', slash == 0 ? 0 : slash - 1);
    return normalized.substr(prior == std::string::npos ? 0 : prior + 1,
        slash - (prior == std::string::npos ? 0 : prior + 1));
}
}

std::filesystem::path WorldSilverHdRegistry::EnvironmentRoot()
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "SILVER_GRAPHICS_REMAKE_ROOT") == 0
        && value != nullptr && *value != '\0')
    {
        const auto result = Utf8Path(value);
        std::free(value);
        return result;
    }
    std::free(value);
#else
    if (const char* value = std::getenv("SILVER_GRAPHICS_REMAKE_ROOT"); value && *value)
        return Utf8Path(value);
#endif
    return {};
}

const char* WorldSilverHdRegistry::StateName(WorldSilverHdState state)
{
    switch (state)
    {
    case WorldSilverHdState::Original: return "ORIGINAL";
    case WorldSilverHdState::Remake: return "REMAKE";
    case WorldSilverHdState::Lod: return "LOD";
    case WorldSilverHdState::Candidate: return "CANDIDATE";
    case WorldSilverHdState::Rollback: return "ROLLBACK";
    default: return "SIN CLASIFICAR";
    }
}

const char* WorldSilverHdRegistry::RuntimeStateName(WorldSilverHdRuntimeState state)
{
    switch (state)
    {
    case WorldSilverHdRuntimeState::Exact: return "EXACTO";
    case WorldSilverHdRuntimeState::Variant: return "VARIANTE";
    case WorldSilverHdRuntimeState::Missing: return "AUSENTE";
    default: return "NO VALIDADO";
    }
}

WorldSilverHdState WorldSilverHdRegistry::Classify(std::string relativePath)
{
    const auto path = Normalize(std::move(relativePath));
    if (path.starts_with("work/before-") || Contains(path, "/rollback/") || Contains(path, "/work/before-"))
        return WorldSilverHdState::Rollback;
    if (Contains(path, "candidate"))
        return WorldSilverHdState::Candidate;
    if (Contains(path, "_lod0") || Contains(path, "_lod1") || Contains(path, "_lod2")
        || Contains(path, "/lod0/") || Contains(path, "/lod1/") || Contains(path, "/lod2/"))
        return WorldSilverHdState::Lod;
    if (Contains(path, "/custom/") || path.starts_with("lab-art/") || path.starts_with("silverhd/lab-art/"))
        return WorldSilverHdState::Remake;
    if (path.starts_with("src/bin/data/") || path.starts_with("src/") || path.starts_with("data/"))
        return WorldSilverHdState::Original;
    return WorldSilverHdState::Unclassified;
}

std::string WorldSilverHdRegistry::LodName(std::string relativePath)
{
    const auto path = Normalize(std::move(relativePath));
    if (Contains(path, "_lod0") || Contains(path, "/lod0/")) return "LOD0";
    if (Contains(path, "_lod1") || Contains(path, "/lod1/")) return "LOD1";
    if (Contains(path, "_lod2") || Contains(path, "/lod2/")) return "LOD2";
    return {};
}

std::string WorldSilverHdRegistry::LogicalKey(std::string relativePath)
{
    std::replace(relativePath.begin(), relativePath.end(), '\\', '/');
    auto normalized = Normalize(relativePath);
    const auto slash = normalized.find_last_of('/');
    auto stem = slash == std::string::npos ? normalized : normalized.substr(slash + 1);
    const auto dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem.resize(dot);

    stem = StripSuffix(std::move(stem), ".original");
    stem = StripSuffix(std::move(stem), "_lod0");
    stem = StripSuffix(std::move(stem), "_lod1");
    stem = StripSuffix(std::move(stem), "_lod2");
    stem = StripSuffix(std::move(stem), "-lod0");
    stem = StripSuffix(std::move(stem), "-lod1");
    stem = StripSuffix(std::move(stem), "-lod2");

    if (const auto silver = stem.find("_silver"); silver != std::string::npos)
        stem.resize(silver);
    else if (const auto silverDash = stem.find("-silver"); silverDash != std::string::npos)
        stem.resize(silverDash);

    if (StartsWithObjectNumber(stem))
    {
        const auto anchor = FolderAnchor(normalized);
        if (!anchor.empty()) return anchor + "/" + stem;
    }
    return stem;
}

void WorldSilverHdRegistry::Scan(const std::filesystem::path& root, const std::filesystem::path& runtimeData)
{
    assets_.clear();
    root_.clear();
    runtimeRoot_.clear();
    uniquePayloadCount_ = 0;
    hashFailureCount_ = 0;
    const auto integratedModels = SilverHdLibrary::ValidateRoot(root);

    std::map<std::string, std::size_t> groupCounts;
    std::map<std::string, std::size_t> hashCounts;
    std::set<std::uintmax_t> sourceSizes;

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(StreamPath(root),
        std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error))
    {
        if (error) { error.clear(); continue; }
        const auto& entry = *iterator;
        if (entry.is_directory(error))
        {
            if (WorldModelCatalog::IsLinkedPath(entry.path()))
                iterator.disable_recursion_pending();
            continue;
        }
        if (error || !entry.is_regular_file(error) || error || WorldModelCatalog::IsLinkedPath(entry.path()))
        {
            error.clear();
            continue;
        }
        if (WorldModelCatalog::Lower(PathUtf8(entry.path().extension())) != ".bmd") continue;
        if (assets_.size() >= MaximumAssets)
            throw std::length_error("Silver HD supera el limite de 10000 BMD.");

        const auto bytes = entry.file_size(error);
        if (error || bytes == 0 || bytes > MaximumAssetBytes) { error.clear(); continue; }
        const auto relative = PathUtf8(std::filesystem::relative(entry.path(), root));
        if (!integratedModels.empty() && !integratedModels.contains(relative)) continue;
        WorldSilverHdAsset asset;
        asset.Bmd = entry.path();
        asset.RelativePath = relative;
        asset.Name = PathUtf8(entry.path().stem());
        asset.LogicalKey = LogicalKey(relative);
        asset.State = Classify(relative);
        asset.Lod = LodName(relative);
        asset.Bytes = bytes;
        asset.Sha256 = mu::crypto::Sha256FileHex(entry.path());
        if (asset.Sha256.empty())
            ++hashFailureCount_;
        else
            ++hashCounts[asset.Sha256];
        sourceSizes.insert(bytes);
        ++groupCounts[asset.LogicalKey];
        assets_.push_back(std::move(asset));
    }

    uniquePayloadCount_ = hashCounts.size() + hashFailureCount_;
    for (auto& asset : assets_)
    {
        asset.VariantCount = groupCounts[asset.LogicalKey];
        if (!asset.Sha256.empty())
            asset.DuplicateCount = hashCounts[asset.Sha256];
    }

    const bool runtimeAvailable = !runtimeData.empty()
        && std::filesystem::is_directory(StreamPath(runtimeData))
        && !WorldModelCatalog::IsLinkedPath(runtimeData);

    if (runtimeAvailable)
    {
        runtimeRoot_ = std::filesystem::absolute(runtimeData).lexically_normal();
        std::map<std::string, std::string> runtimeByHash;
        std::map<std::string, std::string> runtimeByKey;

        error.clear();
        std::filesystem::recursive_directory_iterator runtimeIterator(StreamPath(runtimeData),
            std::filesystem::directory_options::skip_permission_denied, error);
        for (; runtimeIterator != end; runtimeIterator.increment(error))
        {
            if (error) { error.clear(); continue; }
            const auto& entry = *runtimeIterator;
            if (entry.is_directory(error))
            {
                if (WorldModelCatalog::IsLinkedPath(entry.path()))
                    runtimeIterator.disable_recursion_pending();
                error.clear();
                continue;
            }
            if (error || !entry.is_regular_file(error) || error || WorldModelCatalog::IsLinkedPath(entry.path()))
            {
                error.clear();
                continue;
            }
            if (WorldModelCatalog::Lower(PathUtf8(entry.path().extension())) != ".bmd") continue;

            const auto bytes = entry.file_size(error);
            if (error || bytes == 0 || bytes > MaximumAssetBytes) { error.clear(); continue; }
            const auto relative = PathUtf8(std::filesystem::relative(entry.path(), runtimeData));
            runtimeByKey.try_emplace(LogicalKey(relative), relative);

            if (sourceSizes.contains(bytes))
            {
                const auto hash = mu::crypto::Sha256FileHex(entry.path());
                if (hash.empty())
                    ++hashFailureCount_;
                else
                    runtimeByHash.try_emplace(hash, relative);
            }
        }

        for (auto& asset : assets_)
        {
            if (asset.Sha256.empty())
            {
                asset.RuntimeState = WorldSilverHdRuntimeState::Unchecked;
                continue;
            }
            if (const auto exact = runtimeByHash.find(asset.Sha256); exact != runtimeByHash.end())
            {
                asset.RuntimeState = WorldSilverHdRuntimeState::Exact;
                asset.RuntimeRelativePath = exact->second;
            }
            else if (const auto variant = runtimeByKey.find(asset.LogicalKey); variant != runtimeByKey.end())
            {
                asset.RuntimeState = WorldSilverHdRuntimeState::Variant;
                asset.RuntimeRelativePath = variant->second;
            }
            else
            {
                asset.RuntimeState = WorldSilverHdRuntimeState::Missing;
            }
        }
    }

    std::sort(assets_.begin(), assets_.end(), [](const auto& left, const auto& right) {
        return std::tie(left.State, left.LogicalKey, left.RelativePath)
            < std::tie(right.State, right.LogicalKey, right.RelativePath);
    });
    root_ = std::filesystem::absolute(root).lexically_normal();
}

std::size_t WorldSilverHdRegistry::Count(WorldSilverHdState state) const
{
    return static_cast<std::size_t>(std::count_if(assets_.begin(), assets_.end(),
        [state](const auto& asset) { return asset.State == state; }));
}

std::size_t WorldSilverHdRegistry::CountRuntime(WorldSilverHdRuntimeState state) const
{
    return static_cast<std::size_t>(std::count_if(assets_.begin(), assets_.end(),
        [state](const auto& asset) { return asset.RuntimeState == state; }));
}
}
