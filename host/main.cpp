// Fleet Combo host: runs Ship of Harkinian: Unbound (OoT) and 2 Ship 2 Harkinian: Unbound (MM) in one window.
//
// The games implement skijer's Fleet contract (libultraship include/fleet/FleetGameApi.h). This program is the
// "Fleet" side of that contract: it owns the Context (window, input, console, audio), loads both game modules,
// and runs each game's loop on its own fiber. A game asks to leave through "RequestSwitch"; at its next
// "FrameEnd" the host parks it and resumes (or starts) the other game.
//
// Layout of an install:
//   FleetCombo(.exe)  libultraship + runtime libraries
//   oot/  soh module, soh.o2r, oot.o2r (extracted from your ROM), mods/
//   mm/   2ship module, 2ship.o2r, mm.o2r (extracted from your ROM), mods/

#include "Fiber.h"
#include "Session.h"
#include "TestScript.h"

#include <fleet/FleetGameApi.h>
#include <ship/Context.h>
#include <ship/GameRegistry.h>
#include <ship/config/Config.h>
#include <ship/window/Window.h>
#include <fast/Fast3dWindow.h>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <spdlog/spdlog.h>
#include <SDL2/SDL.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using FleetHost::Fiber;

// Both games define these identically (soh/OTRGlobals.h, mm/2s2h/BenPort.h).
#define BTN_CUSTOM_MODIFIER1 0x0040
#define BTN_CUSTOM_MODIFIER2 0x0080
#define BTN_CUSTOM_OCARINA_NOTE_D4 ((CONTROLLERBUTTONS_T)0x00010000)
#define BTN_CUSTOM_OCARINA_NOTE_F4 ((CONTROLLERBUTTONS_T)0x00020000)
#define BTN_CUSTOM_OCARINA_NOTE_A4 ((CONTROLLERBUTTONS_T)0x00040000)
#define BTN_CUSTOM_OCARINA_NOTE_B4 ((CONTROLLERBUTTONS_T)0x00080000)
#define BTN_CUSTOM_OCARINA_NOTE_D5 ((CONTROLLERBUTTONS_T)0x00100000)
#define BTN_CUSTOM_OCARINA_DISABLE_SONGS ((CONTROLLERBUTTONS_T)0x00200000)
#define BTN_CUSTOM_OCARINA_PITCH_UP ((CONTROLLERBUTTONS_T)0x00400000)
#define BTN_CUSTOM_OCARINA_PITCH_DOWN ((CONTROLLERBUTTONS_T)0x00800000)

namespace {

// A game's stack: big, because the decomp code keeps large structs on it.
constexpr size_t kGameStackSize = 64ull * 1024 * 1024;

struct Game {
    std::string folder;     // "oot" / "mm"
    std::string moduleFile; // soh.dll / libsoh.so ...
    std::string name;       // what the module calls itself: "oot" / "mm"
    void* module = nullptr;

    FleetNameFn Name = nullptr;
    FleetAttachFn Attach = nullptr;
    FleetRunFn Run = nullptr;
    FleetArriveFn Arrive = nullptr;
    FleetDetachFn Detach = nullptr;
    FleetResetFn Reset = nullptr;
    FleetDrawItemFn DrawItem = nullptr;
    FleetGetFunctionFn GetFunction = nullptr;

    std::unique_ptr<Fiber> fiber;
    bool attached = false;
    bool started = false;

    // The destination this game was started or resumed with. Strings owned here, not by the caller.
    std::string arrivalPayload;
    uint32_t arrivalEntrance = 0;
};

std::array<Game, 2> sGames;
fs::path sRoot;
Game* sActive = nullptr;
bool sQuit = false;

struct PendingSwitch {
    Game* to;
    uint32_t entrance;
    std::string payload;
};
std::optional<PendingSwitch> sPending;

void ShowProblem(const std::string& text) {
    SPDLOG_ERROR("[FleetCombo] {}", text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Zelda Fleet Combo", text.c_str(), nullptr);
}

// Game messages that arrive through "Log" before the window exists, e.g. why Attach refused.
std::string sLastGameError;


Game* FindGame(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (auto& game : sGames) {
        if (game.name == name) {
            return &game;
        }
    }
    return nullptr;
}

Game* OtherGame(const Game* game) {
    return game == &sGames[0] ? &sGames[1] : &sGames[0];
}

// ---------------------------------------------------------------------------------------------------------------
// Module loading

void* LoadModule(const fs::path& path) {
#ifdef _WIN32
    // Altered search path: the module's own dependencies resolve from its folder first; libultraship.dll is
    // already loaded by this executable and is reused by name, so both games share one Context.
    HMODULE module = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        SPDLOG_ERROR("[FleetCombo] Cannot load {} (error {})", path.string(), GetLastError());
    }
    return module;
#else
    // RTLD_GLOBAL: mods link against the game module's symbols.
    void* module = dlopen(path.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (module == nullptr) {
        SPDLOG_ERROR("[FleetCombo] Cannot load {}: {}", path.string(), dlerror());
    }
    return module;
#endif
}

void* ModuleSymbol(void* module, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    return dlsym(module, name);
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Host services (what the games look up through FleetAttachInfo::hostFunction)

void HostLog(const char* game, FleetLogLevel level, const char* message) {
    const char* who = game != nullptr ? game : "?";
    const char* text = message != nullptr ? message : "";
    switch (level) {
        case FLEET_LOG_TRACE:
            SPDLOG_TRACE("[{}] {}", who, text);
            break;
        case FLEET_LOG_DEBUG:
            SPDLOG_DEBUG("[{}] {}", who, text);
            break;
        case FLEET_LOG_INFO:
            SPDLOG_INFO("[{}] {}", who, text);
            break;
        case FLEET_LOG_WARN:
            SPDLOG_WARN("[{}] {}", who, text);
            break;
        default:
            SPDLOG_ERROR("[{}] {}", who, text);
            sLastGameError = text;
            break;
    }
}

void HostRequestSwitch(const char* fromGame, const FleetDestination* destination) {
    Game* from = FindGame(fromGame);
    if (from == nullptr) {
        SPDLOG_ERROR("[FleetCombo] RequestSwitch from unknown game {}", fromGame ? fromGame : "(null)");
        return;
    }
    Game* to = destination != nullptr ? FindGame(destination->game) : nullptr;
    if (to == nullptr) {
        to = OtherGame(from); // "game" left empty means "the other world"
    }
    if (!to->attached) {
        ShowProblem(from->name + " asked to travel to " + to->name + ", but " + to->name +
                    " did not start (see the message at launch and logs/).");
        return;
    }
    sPending = PendingSwitch{ to, destination != nullptr ? destination->entrance : 0,
                              destination != nullptr && destination->payloadJson != nullptr
                                  ? destination->payloadJson
                                  : std::string{} };
    SPDLOG_WARN("[FleetCombo] Travel requested: {} -> {} ({})", from->name, to->name, sPending->payload);
}

void Activate(Game* game) {
    sActive = game;
    Ship::GameRegistry::SetActive(game->name);
}

FleetDestination ArrivalOf(Game& game) {
    return FleetDestination{ game.name.c_str(), game.arrivalEntrance,
                             game.arrivalPayload.empty() ? nullptr : game.arrivalPayload.c_str() };
}

void HostFrameEnd(const char* gameName) {
    Game* self = FindGame(gameName);
    if (self != nullptr && self == sActive) {
        FleetCombo::TestTick(self->name.c_str());
    }
    static bool sSeen[2] = { false, false };
    if (self != nullptr && !sSeen[self - sGames.data()]) {
        sSeen[self - sGames.data()] = true;
        SPDLOG_WARN("[FleetCombo] {} is running inside Fleet Combo (first frame)", self->name);
    }
    if (self == nullptr || self != sActive || !sPending) {
        return;
    }
    PendingSwitch pending = std::move(*sPending);
    sPending.reset();
    Game* target = pending.to;
    if (target == self) {
        return;
    }
    SPDLOG_WARN("[FleetCombo] Switching {} -> {} ({})", self->name, target->name,
                target->started ? "resume" : "first start");
    target->arrivalEntrance = pending.entrance;
    target->arrivalPayload = pending.payload;

    Activate(target);
    if (target->started) {
        const FleetDestination arrival = ArrivalOf(*target);
        target->Arrive(&arrival);
    }
    // Starts the target on its first visit (its fiber calls Run) or continues it inside its own FrameEnd.
    Fiber::SwitchTo(target->fiber.get());

    // Back here: someone switched to us again (the Arrive above was delivered to us by them).
    Activate(self);
}

void HostResetActiveGame() {
    if (sActive != nullptr && sActive->Reset != nullptr) {
        sActive->Reset();
    }
}

bool HostDrawForeignItem(FleetItemDraw* request) {
    if (request == nullptr) {
        return false;
    }
    Game* owner = FindGame(request->owner);
    if (owner == nullptr || owner->DrawItem == nullptr || !owner->started) {
        return false;
    }
    // Draw with the owner's code and archives, then hand the frame back to the running game.
    Ship::GameScope scope(owner->name);
    return owner->DrawItem(request);
}

void* HostFunction(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    const std::string_view key = name;
    if (key == "Log") {
        return reinterpret_cast<void*>(HostLog);
    }
    if (key == "RequestSwitch") {
        return reinterpret_cast<void*>(HostRequestSwitch);
    }
    if (key == "FrameEnd") {
        return reinterpret_cast<void*>(HostFrameEnd);
    }
    if (key == "ResetActiveGame") {
        return reinterpret_cast<void*>(HostResetActiveGame);
    }
    if (key == "DrawForeignItem") {
        return reinterpret_cast<void*>(HostDrawForeignItem);
    }
    // Session services (shared hearts and inventory, deliveries, seed): Session.cpp.
    if (void* service = FleetCombo::TestFunction(name)) {
        return service;
    }
    if (void* service = FleetCombo::SessionFunction(name)) {
        return service;
    }
    return nullptr; // not offered: the game does without it
}

// ---------------------------------------------------------------------------------------------------------------

bool LoadGame(Game& game) {
    const fs::path folder = sRoot / game.folder;
    const fs::path modulePath = folder / game.moduleFile;
    if (!fs::exists(modulePath)) {
        ShowProblem("Missing " + modulePath.string());
        return false;
    }
    game.module = LoadModule(modulePath);
    if (game.module == nullptr) {
        return false;
    }
    game.GetFunction = reinterpret_cast<FleetGetFunctionFn>(ModuleSymbol(game.module, FLEET_GET_FUNCTION_SYMBOL));
    if (game.GetFunction == nullptr) {
        SPDLOG_ERROR("[FleetCombo] {} is not a Fleet game module", modulePath.string());
        return false;
    }
    auto get = [&](const char* fn) { return game.GetFunction(fn); };
    game.Name = reinterpret_cast<FleetNameFn>(get("Name"));
    game.Attach = reinterpret_cast<FleetAttachFn>(get("Attach"));
    game.Run = reinterpret_cast<FleetRunFn>(get("Run"));
    game.Arrive = reinterpret_cast<FleetArriveFn>(get("Arrive"));
    game.Detach = reinterpret_cast<FleetDetachFn>(get("Detach"));
    game.Reset = reinterpret_cast<FleetResetFn>(get("Reset"));
    game.DrawItem = reinterpret_cast<FleetDrawItemFn>(get("DrawItem"));
    if (game.Name == nullptr || game.Attach == nullptr || game.Run == nullptr || game.Arrive == nullptr) {
        SPDLOG_ERROR("[FleetCombo] {} lacks the core Fleet services", modulePath.string());
        return false;
    }
    game.name = game.Name();

    const std::string install = folder.string();
    const FleetAttachInfo info = { HostFunction, install.c_str() };
    // Attach runs with the game's own folder as working directory, so relative lookups land in its install.
    game.attached = game.Attach(&info);
    if (!game.attached) {
        ShowProblem(game.name + " cannot start from " + install + ":\n" +
                    (sLastGameError.empty() ? std::string("run its own launcher once to extract your ROM.")
                                            : sLastGameError));
        sLastGameError.clear();
        return false;
    }
    Game* self = &game;
    game.fiber = std::make_unique<Fiber>(
        [self] {
            self->started = true;
            const FleetDestination arrival = ArrivalOf(*self);
            self->Run(self->arrivalPayload.empty() && self->arrivalEntrance == 0 ? nullptr : &arrival);
            // The loop returns when the window closes: the whole program ends.
            sQuit = true;
        },
        kGameStackSize);
    FleetCombo::SessionAddGame({ game.name.c_str(), game.GetFunction, &game.started });
    SPDLOG_WARN("[FleetCombo] Attached {} from {}", game.name, install);
    return true;
}

Ship::Context* CreateHostContext() {
    Ship::Context* context = Ship::Context::CreateUninitializedInstance("Zelda Fleet Combo", "fleetcombo",
                                                                        "fleetcombo.json");
    context->InitLogging(spdlog::level::info, spdlog::level::info);
    // spdlog is header-only: this executable has its own default logger. Point it at the Context's file logger,
    // or everything the host logs (and every message the games send through "Log") is lost.
    if (auto logger = context->GetLogger()) {
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }
    context->InitConfiguration();
    context->InitConsoleVariables();
    auto controlDeck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>({
        BTN_CUSTOM_MODIFIER1,
        BTN_CUSTOM_MODIFIER2,
        BTN_CUSTOM_OCARINA_NOTE_D4,
        BTN_CUSTOM_OCARINA_NOTE_F4,
        BTN_CUSTOM_OCARINA_NOTE_A4,
        BTN_CUSTOM_OCARINA_NOTE_B4,
        BTN_CUSTOM_OCARINA_NOTE_D5,
        BTN_CUSTOM_OCARINA_DISABLE_SONGS,
        BTN_CUSTOM_OCARINA_PITCH_UP,
        BTN_CUSTOM_OCARINA_PITCH_DOWN,
    }));
    context->InitControlDeck(controlDeck);
    context->InitCrashHandler();
    SPDLOG_WARN("[FleetCombo] Starting (controls ready)");
    // The host mounts nothing itself: each game registers its own ResourceManager in the GameRegistry.
    // An empty list would make libultraship mount the whole install folder (both games) as one archive,
    // so name an archive that does not exist instead.
    context->InitResourceManager({ (sRoot / "fleetcombo.o2r").string() }, {}, 3, true);
    SPDLOG_WARN("[FleetCombo] Resource manager ready");
    context->InitConsole();
    context->InitWindow(std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>{}));
    SPDLOG_WARN("[FleetCombo] Window ready");
    return context;
}

int HostMain() {
#ifdef _WIN32
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    sRoot = fs::path(exe).parent_path();
#else
    sRoot = fs::canonical("/proc/self/exe").parent_path();
#endif
    fs::current_path(sRoot);

#ifdef _WIN32
    sGames[0] = Game{ "oot", "soh.dll" };
    sGames[1] = Game{ "mm", "2ship.dll" };
#elif defined(__APPLE__)
    sGames[0] = Game{ "oot", "libsoh.dylib" };
    sGames[1] = Game{ "mm", "lib2ship.dylib" };
#else
    sGames[0] = Game{ "oot", "libsoh.so" };
    sGames[1] = Game{ "mm", "lib2ship.so" };
#endif

    Fiber::InitMainFiber();
    CreateHostContext();
    FleetCombo::SessionInit(sRoot);
    FleetCombo::TestInit(sRoot);

    bool any = false;
    for (auto& game : sGames) {
        SPDLOG_WARN("[FleetCombo] Loading {}/{}", game.folder, game.moduleFile);
        any |= LoadGame(game);
    }
    SPDLOG_WARN("[FleetCombo] Starting Ocarina of Time");
    if (!sGames[0].attached) {
        SPDLOG_CRITICAL("[FleetCombo] Ocarina of Time is required: the adventure starts in Hyrule.");
        return 1;
    }

    // Start in Hyrule, on OoT's own title screen and file select.
    Activate(&sGames[0]);
    while (!sQuit) {
        Fiber::SwitchTo(sActive->fiber.get());
        // Control comes back here only when a game's loop finished (window closed) or a fiber ended.
        if (sActive->fiber->Finished()) {
            sQuit = true;
        }
    }

    FleetCombo::SessionShutdown();
    // The other game is parked mid-frame and cannot unwind; its progress was saved when it left.
    // Save the shared settings and leave without running destructors over a parked game.
    if (auto* context = Ship::Context::GetRawInstance(); context != nullptr && context->GetConfig() != nullptr) {
        context->GetConfig()->Save();
    }
    std::fflush(nullptr);
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 0);
#endif
    std::_Exit(0);
}

} // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return HostMain();
}
#else
int main() {
    return HostMain();
}
#endif
