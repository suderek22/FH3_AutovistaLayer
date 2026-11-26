#include <windows.h>
#include <xinput.h>
#include <cstdint>
#include <cstring>

#include "fh3mod.h"
#include "AutoshowLayout.h"

namespace {
// CarColorSelect owns A_CarDetails. The colour chooser itself is a popup.
constexpr std::uintptr_t kDetailsShown = 0x13E5AD0;
constexpr std::uintptr_t kDetailsHidden = 0x13E8090;
constexpr std::uintptr_t kDetailsUpdate = 0x13E87E0;
constexpr std::uintptr_t kGetUICar = 0x137B040;
constexpr std::uintptr_t kSetAnimationSpeed = 0x18AB0F0;
constexpr std::uintptr_t kPlayState = 0x1889B40;
constexpr std::uintptr_t kSetCarInForzavista = 0x18AC280;
constexpr std::uintptr_t kFindGraphEvent = 0x1D08E60;
constexpr unsigned kPreviewCarSlot = 0x55;

using Lifecycle = void (*)(void*);
Lifecycle originalShown = nullptr;
Lifecycle originalHidden = nullptr;
Lifecycle originalUpdate = nullptr;
const FH3ModApi* api = nullptr;
FH3Mod* owner = nullptr;
unsigned char* game = nullptr;

// The native lookup returns a reference owned by the active preview page.
void* activePage = nullptr;
unsigned char* previewCar = nullptr;
bool exploded = false;
bool yDown = false;
bool enabledForzavista = false;

bool controllerYDown() {
    DWORD foregroundProcess = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
    if (foregroundProcess != GetCurrentProcessId())
        return false;
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state{};
        if (XInputGetState(index, &state) == ERROR_SUCCESS && (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y))
            return true;
    }
    return false;
}

bool hasGraphEvent(const char* event) {
    auto* animation = *reinterpret_cast<unsigned char**>(previewCar + 0x73A8);
    auto* graph = animation ? *reinterpret_cast<unsigned char**>(animation + 0x1E0) : nullptr;
    if (!graph)
        return false;

    // Read-only string argument matching the native animation-event lookup.
    struct NativeString {
        union { char text[16]; const char* pointer; };
        std::size_t size;
        std::size_t capacity;
    } name{};
    name.size = std::strlen(event);
    name.capacity = name.size < 16 ? 15 : name.size;
    if (name.size < 16)
        std::memcpy(name.text, event, name.size + 1);
    else
        name.pointer = event;

    auto* events = graph + 0x268;
    void* match = nullptr;
    reinterpret_cast<void* (*)(void*, void**, const NativeString*)>(game + kFindGraphEvent)(events, &match, &name);
    return match && match != *reinterpret_cast<void**>(events + 8);
}

void setForzavista(bool enabled) {
    reinterpret_cast<void (*)(void*, bool)>(game + kSetCarInForzavista)(previewCar, enabled);
}

bool playState(bool open) {
    if (!previewCar || !*reinterpret_cast<void**>(previewCar + 0x73A8))
        return false;
    if (!previewCar[0x7569]) {
        setForzavista(true);
        enabledForzavista = true;
    }
    const char* events[] = {
        open ? "CarEvents:Explode" : "CarEvents:Implode",
        open ? "Explode" : "Implode",
        open ? "explode" : "implode",
    };
    const char* event = nullptr;
    for (const char* candidate : events) {
        if (hasGraphEvent(candidate)) {
            event = candidate;
            break;
        }
    }
    if (!event)
        return false;
    reinterpret_cast<void (*)(void*, float)>(game + kSetAnimationSpeed)(previewCar, 1.0f);
    reinterpret_cast<void (*)(void*, const char*)>(game + kPlayState)(previewCar, event);
    api->log(owner, event);
    return true;
}

void leavePreview() {
    if (previewCar) {
        if (exploded)
            playState(false);
        if (enabledForzavista)
            setForzavista(false);
        auto** vtable = *reinterpret_cast<void***>(previewCar);
        reinterpret_cast<void (*)(void*)>(vtable[3])(previewCar);
        previewCar = nullptr;
    }
    activePage = nullptr;
    exploded = false;
    yDown = false;
    enabledForzavista = false;
}

void shown(void* page) {
    originalShown(page);
    leavePreview();
    activePage = page;
    yDown = controllerYDown();
    reinterpret_cast<void* (*)(void*, unsigned char**, unsigned)>(game + kGetUICar)(
        page, &previewCar, kPreviewCarSlot);
    if (!previewCar)
        api->log(owner, "Autoshow details opened without a preview car.");
    else
        api->log(owner, "Autoshow details opened with a preview car.");
}

void hidden(void* page) {
    if (page == activePage)
        leavePreview();
    originalHidden(page);
}

void update(void* page) {
    originalUpdate(page);
    if (page != activePage || !previewCar)
        return;
    const bool pressed = controllerYDown();
    if (pressed && !yDown) {
        if (playState(!exploded)) {
            exploded = !exploded;
            api->log(owner, exploded ? "Autoshow Y: requested Explode." : "Autoshow Y: requested Implode.");
        } else {
            api->log(owner, "Autoshow Y: no matching event in the preview car animation graph.");
        }
    }
    yDown = pressed;
}
}

bool registerAutoshowPrompt(const FH3ModApi* platform, FH3Mod* mod) {
    return platform->set_file && platform->set_file(mod, "UI/Scenes/Anthem/A_CarDetails.xaml",
        kAutoshowLayout, sizeof kAutoshowLayout - 1);
}

void installAutoshowPreview(const FH3ModApi* platform, FH3Mod* mod, unsigned char* base) {
    constexpr unsigned char shownCode[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8B, 0xEC};
    constexpr unsigned char hiddenCode[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
    constexpr unsigned char updateCode[] = {0x40, 0x55, 0x56, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB9};
    constexpr unsigned char getCarCode[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x0D};
    constexpr unsigned char playCode[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x89, 0xA8, 0x73};
    constexpr unsigned char speedCode[] = {0x48, 0x8B, 0x81, 0xA8, 0x73, 0x00, 0x00, 0x48, 0x85, 0xC0};
    constexpr unsigned char forzavistaCode[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
    constexpr unsigned char findEventCode[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
    if (std::memcmp(base + kDetailsShown, shownCode, sizeof shownCode) ||
        std::memcmp(base + kDetailsHidden, hiddenCode, sizeof hiddenCode) ||
        std::memcmp(base + kDetailsUpdate, updateCode, sizeof updateCode) ||
        std::memcmp(base + kGetUICar, getCarCode, sizeof getCarCode) ||
        std::memcmp(base + kPlayState, playCode, sizeof playCode) ||
        std::memcmp(base + kSetAnimationSpeed, speedCode, sizeof speedCode) ||
        std::memcmp(base + kSetCarInForzavista, forzavistaCode, sizeof forzavistaCode) ||
        std::memcmp(base + kFindGraphEvent, findEventCode, sizeof findEventCode)) {
        platform->log(mod, "Autoshow preview does not recognize this FH3 game build.");
        return;
    }

    game = base;
    api = platform;
    owner = mod;
    FH3Hook* hooks[] = {
        platform->hook_function(mod, "Autoshow details shown", base + kDetailsShown,
            reinterpret_cast<void*>(shown), reinterpret_cast<void**>(&originalShown)),
        platform->hook_function(mod, "Autoshow details hidden", base + kDetailsHidden,
            reinterpret_cast<void*>(hidden), reinterpret_cast<void**>(&originalHidden)),
        platform->hook_function(mod, "Autoshow details update", base + kDetailsUpdate,
            reinterpret_cast<void*>(update), reinterpret_cast<void**>(&originalUpdate)),
    };
    bool installed = true;
    for (FH3Hook* hook : hooks)
        installed = installed && hook && platform->enable_hooks(mod, hook);
    if (!installed) {
        for (FH3Hook* hook : hooks)
            if (hook)
                platform->disable_hooks(mod, hook);
        platform->log(mod, "Could not enable the Autoshow preview hooks.");
        return;
    }
    platform->log(mod, "Autoshow preview enabled. Y toggles Explode and Implode.");
}

