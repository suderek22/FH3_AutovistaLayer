#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "fh3mod.h"

namespace {
// FH3 Opus 1.0.125.0. Layouts are from the native car-scene and model readers.
constexpr std::uintptr_t kParseModelRva = 0x1EC0380;
constexpr std::uintptr_t kReadSceneModelRva = 0x1C02B10;
constexpr std::uintptr_t kReadSceneRva = 0x1C03640;
constexpr unsigned char kReadSceneCode[] = {
    0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
};
constexpr unsigned char kReadSceneModelCode[] = {
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55
};
constexpr unsigned char kParseModelCode[] = {
    0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x48, 0x89, 0x50, 0x10
};
constexpr std::size_t kScenePartPath = 0x200;
constexpr std::size_t kScenePartMask = 0x28;
constexpr std::size_t kAttachmentTransform = 0x30;
constexpr std::size_t kBoneName = 0x220;
constexpr std::size_t kMeshStride = 0x1C0;
constexpr std::size_t kMeshName = 0x120;
constexpr std::size_t kMeshMaterial = 0x140;
constexpr std::size_t kMeshMask = 0x144;
constexpr std::size_t kMeshLodIndex = 0x146;
constexpr std::size_t kMeshShowroomIndex = 0x147;

constexpr std::uint16_t kShowroom = 1;
constexpr std::uint16_t kLod0 = 2;
constexpr std::string_view kCarsRoot = "game:\\media\\cars\\";
constexpr std::string_view kShowroomSuffix = "__slod.modelbin";

using ParseModel = void* (*)(void*, void*, void*, const void*, void*);
using StatusEmpty = bool (*)(const void*);
using ReadSceneModel = std::uintptr_t (*)(void*, void*, void*);
ParseModel originalParseModel = nullptr;
StatusEmpty modelParseSucceeded = nullptr;
ReadSceneModel originalReadSceneModel = nullptr;
ReadSceneModel originalReadScene = nullptr;
const FH3ModApi* platform = nullptr;
FH3Mod* mod = nullptr;
std::atomic<unsigned> modelMessages{0};
std::atomic<unsigned> sceneMessages{0};
std::mutex pairedBayMutex;
std::unordered_set<std::string> pairedBayAssets;

struct ScenePart {
    unsigned char* record;
    std::string path;
    std::uint16_t originalMask;
};
thread_local std::vector<ScenePart>* sceneParts = nullptr;

bool copyAssetPath(const unsigned char* path, char* output, std::size_t capacity) {
    if (!path)
        return false;
    __try {
        const auto length = *reinterpret_cast<const std::size_t*>(path + 0x10);
        const auto allocation = *reinterpret_cast<const std::size_t*>(path + 0x18);
        if (!length || length >= capacity || length > allocation || allocation < 15)
            return false;
        const char* text = allocation < 16 ? reinterpret_cast<const char*>(path)
                                           : *reinterpret_cast<const char* const*>(path);
        for (std::size_t index = 0; index < length; ++index) {
            const auto value = static_cast<unsigned char>(text[index]);
            if (value < 0x20 || value > 0x7E)
                return false;
            char lower = value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
            output[index] = lower == '/' ? '\\' : lower;
        }
        output[length] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void logMask(const char* label, const char* path, std::uint16_t mask) {
    char message[640];
    std::snprintf(message, sizeof message, "%s 0x%04X: %s",
                  label, static_cast<unsigned>(mask), path);
    platform->log(mod, message);
}

bool isCarModel(std::string_view path) {
    return path.rfind(kCarsRoot, 0) == 0 && path.size() > kCarsRoot.size() &&
           path.size() >= 9 && path.substr(path.size() - 9) == ".modelbin";
}

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool isBayPart(std::string_view path) {
    const auto name = path.substr(path.find_last_of('\\') + 1);
    return name.rfind("enginebay_", 0) == 0 || name.rfind("trunkbay_", 0) == 0 ||
           name.rfind("trunkliner_", 0) == 0;
}

bool sameAttachment(const ScenePart& a, const ScenePart& b) {
    // Both model records carry the attachment matrix and the resolved bone ID.
    char aBone[128], bBone[128];
    return std::memcmp(a.record + kAttachmentTransform, b.record + kAttachmentTransform, 64) == 0 &&
           copyAssetPath(a.record + kBoneName, aBone, sizeof aBone) &&
           copyAssetPath(b.record + kBoneName, bBone, sizeof bBone) &&
           std::strcmp(aBone, bBone) == 0;
}

bool isReading(void* reader) {
    using IsReading = bool (*)(void*);
    const auto* vtable = *static_cast<void***>(reader);
    return reinterpret_cast<IsReading>(vtable[0xB8 / sizeof(void*)])(reader);
}

std::uintptr_t readScene(void* scene, void* reader, void* context) {
    if (!isReading(reader))
        return originalReadScene(scene, reader, context);
    std::vector<ScenePart> parts;
    auto* previous = sceneParts;
    sceneParts = &parts;
    const auto result = originalReadScene(scene, reader, context);
    sceneParts = previous;

    for (const auto& showroom : parts) {
        if (showroom.originalMask != kShowroom || !endsWith(showroom.path, kShowroomSuffix))
            continue;
        std::string stockPath = showroom.path.substr(0, showroom.path.size() - kShowroomSuffix.size());
        stockPath += ".modelbin";
        const ScenePart* replacement = nullptr;
        for (const auto& stock : parts) {
            if (stock.path != stockPath || !(stock.originalMask & kLod0) || !sameAttachment(showroom, stock))
                continue;
            if (replacement) {
                replacement = nullptr;
                break;
            }
            replacement = &stock;
        }
        if (replacement) {
            auto& mask = *reinterpret_cast<std::uint16_t*>(replacement->record + kScenePartMask);
            mask &= ~kLod0;
            if (isBayPart(replacement->path)) {
                std::lock_guard<std::mutex> lock(pairedBayMutex);
                pairedBayAssets.insert(replacement->path);
            }
            if (sceneMessages.fetch_add(1, std::memory_order_relaxed) < 240)
                logMask("Cleared paired stock LOD0; mask is", replacement->path.c_str(), mask);
        }
    }
    return result;
}

bool hasPairedBay(const char* path) {
    if (!isBayPart(path))
        return false;
    std::lock_guard<std::mutex> lock(pairedBayMutex);
    return pairedBayAssets.count(path) != 0;
}

void suppressParsedBay(unsigned char* model, const char* path) {
    auto& mask = *reinterpret_cast<std::uint16_t*>(model + 6);
    if (!(mask & kLod0) || !hasPairedBay(path))
        return;
    auto* mesh = *reinterpret_cast<unsigned char**>(model + 0x98);
    const auto count = *reinterpret_cast<const std::size_t*>(model + 0xA0);
    if (!mesh || !count || count > 4096)
        return;

    for (std::size_t index = 0; index < count; ++index, mesh += kMeshStride) {
        auto& meshMask = *reinterpret_cast<std::uint16_t*>(mesh + kMeshMask);
        meshMask &= ~kLod0;
    }
    mask &= ~kLod0;
    logMask("Suppressed LOD0 meshes in paired bay; mask is", path, mask);
}

std::uintptr_t readSceneModel(void* record, void* reader, void* context) {
    const auto result = originalReadSceneModel(record, reader, context);
    auto* bytes = static_cast<unsigned char*>(record);
    char path[512];
    if (!isReading(reader) || !copyAssetPath(bytes + kScenePartPath, path, sizeof path) || !isCarModel(path))
        return result;

    // The native carbin reader stores the rendering LOD mask here before
    // the scene builds the list of models to load.
    auto& mask = *reinterpret_cast<std::uint16_t*>(bytes + kScenePartMask);
    if (sceneParts && (mask == kShowroom || (mask & kLod0)))
        sceneParts->push_back({bytes, path, mask});
    if (mask == kShowroom) {
        mask |= kLod0;
        if (sceneMessages.fetch_add(1, std::memory_order_relaxed) < 240)
            logMask("Promoted car-scene LOD mask to", path, mask);
    }
    return result;
}

void promoteParsedModel(unsigned char* model, const char* path) {
    auto& mask = *reinterpret_cast<std::uint16_t*>(model + 6);
    if (!(mask & kShowroom))
        return;
    auto* mesh = *reinterpret_cast<unsigned char**>(model + 0x98);
    const auto count = *reinterpret_cast<const std::size_t*>(model + 0xA0);
    if (!mesh || !count || count > 4096)
        return;

    std::vector<std::string> lod0Names;
    if (mask & kLod0) {
        for (std::size_t index = 0; index < count; ++index) {
            const auto* candidate = mesh + index * kMeshStride;
            if (!(*reinterpret_cast<const std::uint16_t*>(candidate + kMeshMask) & kLod0))
                continue;
            char name[256];
            if (copyAssetPath(candidate + kMeshName, name, sizeof name)) {
                std::string_view key(name);
                if (endsWith(key, "_lods0"))
                    key.remove_suffix(6);
                else if (endsWith(key, "_lod0"))
                    key.remove_suffix(5);
                lod0Names.emplace_back(key);
            }
        }
    }

    unsigned promoted = 0;
    for (std::size_t index = 0; index < count; ++index, mesh += kMeshStride) {
        const auto material = *reinterpret_cast<const std::uint16_t*>(mesh + kMeshMaterial);
        auto& meshMask = *reinterpret_cast<std::uint16_t*>(mesh + kMeshMask);
        if (material == 0xFFFF || meshMask != kShowroom || mesh[kMeshLodIndex] != 0xFF || mesh[kMeshShowroomIndex] != 0xFF)
            continue;
        if (mask & kLod0) {
            char name[256];
            if (!copyAssetPath(mesh + kMeshName, name, sizeof name))
                continue;
            std::string_view key(name);
            if (endsWith(key, "_lods"))
                key.remove_suffix(5);
            bool duplicatesLod0 = false;
            for (const auto& existing : lod0Names)
                duplicatesLod0 |= key == existing;
            if (duplicatesLod0)
                continue;
        }
        meshMask |= kLod0;
        mesh[kMeshLodIndex] = 0;
        ++promoted;
    }
    if (promoted) {
        if (model[2] == 0xFF)
            model[2] = 0;
        mask |= kLod0;
        if (modelMessages.fetch_add(1, std::memory_order_relaxed) < 240)
            logMask("Promoted parsed model LOD mask to", path, mask);
    }
}

void* parseModel(void* model, void* status, void* bundle, const void* member, void* context) {
    auto* result = originalParseModel(model, status, bundle, member, context);
    if (!member || !modelParseSucceeded(result))
        return result;

    // The parser receives resource+0xF8. The resource path is at +0xC0.
    char path[512];
    if (copyAssetPath(static_cast<const unsigned char*>(member) - 0x38, path, sizeof path) &&
        isCarModel(path)) {
        auto* bytes = static_cast<unsigned char*>(model);
        promoteParsedModel(bytes, path);
        suppressParsedBay(bytes, path);
    }
    return result;
}

DWORD WINAPI install(void*) {
    auto* game = static_cast<unsigned char*>(platform->wait_for_code(INFINITE));
    constexpr std::uintptr_t kModelStatusEmptyRva = 0x1A2FDE0;
    constexpr unsigned char kModelStatusEmptyCode[] = {0x48, 0x8B, 0x11, 0x48, 0x85, 0xD2};
    if (!game || std::memcmp(game + kParseModelRva, kParseModelCode, sizeof kParseModelCode)
              || std::memcmp(game + kReadSceneModelRva, kReadSceneModelCode, sizeof kReadSceneModelCode)
              || std::memcmp(game + kReadSceneRva, kReadSceneCode, sizeof kReadSceneCode)
              || std::memcmp(game + kModelStatusEmptyRva, kModelStatusEmptyCode, sizeof kModelStatusEmptyCode)) {
        platform->log(mod, "AutovistaLayer does not recognize this FH3 game build.");
        return 0;
    }
    modelParseSucceeded = reinterpret_cast<StatusEmpty>(game + kModelStatusEmptyRva);
    if (!platform->hook_function(mod, "Promote model LOD meshes", game + kParseModelRva,
                                 reinterpret_cast<void*>(parseModel), reinterpret_cast<void**>(&originalParseModel)) ||
        !platform->hook_function(mod, "Promote car-scene LOD masks", game + kReadSceneModelRva,
                                 reinterpret_cast<void*>(readSceneModel), reinterpret_cast<void**>(&originalReadSceneModel)) ||
        !platform->hook_function(mod, "Pair showroom models with stock car-scene models", game + kReadSceneRva,
                                 reinterpret_cast<void*>(readScene), reinterpret_cast<void**>(&originalReadScene)) ||
        !platform->enable_hooks(mod, nullptr)) {
        platform->disable_hooks(mod, nullptr);
        platform->log(mod, "Could not enable AutovistaLayer hooks.");
        return 0;
    }
    platform->log(mod, "Runtime showroom LOD promotion active.");
    return 0;
}
}

FH3MOD_EXPORT int fh3mod_init(const FH3ModApi* api) {
    if (!api || api->api_version < FH3MOD_API_VERSION)
        return 1;
    FH3ModInfo info{};
    info.size = sizeof info;
    info.name = "FH3_AutovistaLayer";
    info.version = "1.0.0";
    info.github_repo = "suderek22/FH3_AutovistaLayer";
    info.check_updates = 1;
    info.platform_version = "1.4.2";
    info.description = "Includes showroom geometry when the game renders a car at LOD0.";
    mod = api->register_mod(&info);
    if (!mod)
        return 1;
    platform = api;
    HANDLE worker = CreateThread(nullptr, 0, install, nullptr, 0, nullptr);
    if (!worker)
        return 1;
    CloseHandle(worker);
    return 0;
}
