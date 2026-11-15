#include "Session.h"

#include <ship/GameRegistry.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace FleetCombo {
namespace {

// -----------------------------------------------------------------------------------------------------------------
// Games

struct GameHandle {
    std::string name;
    FleetGetFunctionFn getFunction = nullptr;
    const bool* started = nullptr;

    template <class F> F Fn(const char* service) const {
        return getFunction != nullptr ? reinterpret_cast<F>(getFunction(service)) : nullptr;
    }

    // Calls a JSON-returning service and frees the game's string.
    std::optional<json> TakeJson(char* raw) const {
        if (raw == nullptr) {
            return std::nullopt;
        }
        json parsed = json::parse(raw, nullptr, false);
        if (auto release = Fn<FleetFreeStringFn>("FreeString")) {
            release(raw);
        }
        if (parsed.is_discarded()) {
            return std::nullopt;
        }
        return parsed;
    }

    std::optional<json> Request(const char* op, const json& input = json::object()) const {
        auto request = Fn<FleetRandoRequestFn>("RandoRequest");
        if (request == nullptr) {
            return std::nullopt;
        }
        const std::string text = input.dump();
        return TakeJson(request(op, text.c_str()));
    }
};

std::vector<GameHandle> sGames;
fs::path sSessionDir;

const GameHandle* FindGame(const std::string& name) {
    for (const auto& game : sGames) {
        if (game.name == name) {
            return &game;
        }
    }
    return nullptr;
}

std::string OtherGame(const std::string& name) {
    return name == "oot" ? "mm" : "oot";
}

// -----------------------------------------------------------------------------------------------------------------
// Shared progress

// Upgrade families both games report in ReadSharedTiers: the better of the two wins.
const std::vector<std::string> kMaxKeys = { "bows",        "bombBags",   "bombchuBags", "magic",     "wallets",
                                            "fireArrows",  "iceArrows",  "lightArrows", "eponasSong", "songOfStorms",
                                            "hookshot",    "lens",       "goronMask",   "zoraMask",  "keatonMask",
                                            "bunnyHood",   "maskOfTruth" };
// Additive families: what is found in either world adds up (quarter hearts).
const std::vector<std::string> kSumKeys = { "hearts" };
constexpr int kStartingQuarterHearts = 12;
constexpr int kMaxQuarterHearts = 80;

// Item each game grants for a one-tier family (the tiered ones the games step through themselves).
const std::map<std::string, std::string> kTierItem = {
    { "fireArrows", "Fire Arrows" }, { "iceArrows", "Ice Arrows" },       { "lightArrows", "Light Arrows" },
    { "eponasSong", "Epona's Song" }, { "songOfStorms", "Song of Storms" }, { "lens", "Lens of Truth" },
    { "goronMask", "Goron Mask" },    { "zoraMask", "Zora Mask" },          { "keatonMask", "Keaton Mask" },
    { "bunnyHood", "Bunny Hood" },    { "maskOfTruth", "Mask of Truth" },   { "bombchuBags", "Bombchu Bag" },
};

struct Session {
    int slot = -1;
    json seed;
    // key -> shared value; game -> key -> last value that game reported or was given
    std::map<std::string, int> shared;
    std::map<std::string, std::map<std::string, int>> seen;
    // game -> deliveries waiting for it ({ id, item }); the game remembers which ids it already received
    std::map<std::string, json> deliveries;
    std::set<std::string> obtained; // "game:check"
};

std::optional<Session> sSession;
std::string sSessionText; // what GetSession handed out (valid until the next call)

fs::path SessionPath(int slot) {
    return sSessionDir / ("slot" + std::to_string(slot) + ".json");
}

json SessionToJson(const Session& session) {
    json deliveries = json::object();
    for (const auto& [game, list] : session.deliveries) {
        deliveries[game] = list;
    }
    return { { "slot", session.slot },     { "seed", session.seed },         { "shared", session.shared },
             { "seen", session.seen },     { "deliveries", deliveries },     { "obtained", session.obtained } };
}

void SaveSession() {
    if (!sSession) {
        return;
    }
    std::error_code error;
    fs::create_directories(sSessionDir, error);
    const fs::path path = SessionPath(sSession->slot);
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << SessionToJson(*sSession).dump();
    }
    fs::rename(temp, path, error);
    if (error) {
        SPDLOG_ERROR("[FleetCombo] Could not save {}: {}", path.string(), error.message());
    }
}

std::optional<Session> LoadSession(int slot) {
    std::ifstream in(SessionPath(slot), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const json data = json::parse(in, nullptr, false);
    if (!data.is_object() || !data.contains("seed")) {
        return std::nullopt;
    }
    Session session;
    session.slot = slot;
    session.seed = data.at("seed");
    session.shared = data.value("shared", std::map<std::string, int>{});
    session.seen = data.value("seen", std::map<std::string, std::map<std::string, int>>{});
    const json deliveries = data.value("deliveries", json::object());
    for (const auto& [game, list] : deliveries.items()) {
        session.deliveries[game] = list;
    }
    session.obtained = data.value("obtained", std::set<std::string>{});
    return session;
}

void Activate(Session session) {
    sSession = std::move(session);
    SPDLOG_WARN("[FleetCombo] Session for slot {} is active (seed {})", sSession->slot,
                sSession->seed.value("masterSeed", 0u));
}

// -----------------------------------------------------------------------------------------------------------------
// Seed generation: vanilla placements + shuffled NEI items

enum GenerationState { kIdle = 0, kGenerating = 1, kDone = 2, kFailed = 3 };
int sGenerationState = kIdle;
std::map<int, json> sGenerated; // slot -> seed not yet turned into a save

bool IsReplicaOrForeign(const std::string& key) {
    return key.rfind("oot.", 0) == 0 || key.rfind("mm.", 0) == 0 || key.rfind("fleet:", 0) == 0 ||
           key.rfind("fleet.", 0) == 0;
}

// The key that pairs an item with its twin in the other world.
std::string PairKeyOf(const json& item) {
    if (item.contains("pairKey")) {
        return item.at("pairKey").get<std::string>();
    }
    if (item.contains("textKey")) {
        return item.at("textKey").get<std::string>();
    }
    return {};
}

struct GameDump {
    json settings;
    json dump;
    json placements = json::object();
    std::set<std::string> vanillaReachable;
    std::vector<std::string> junkChecks;
};

// Runs a game's randomizer services as that game, then hands the thread back.
template <class F> auto AsGame(const GameHandle& game, F&& body) {
    Ship::GameScope scope(game.name);
    return body();
}

void PrepareGame(const GameHandle& game, uint32_t seed, GameDump& out) {
    AsGame(game, [&] {
        if (game.started == nullptr || !*game.started) {
            // Not booted yet (Termina before the first visit): bring up its randomizer data without a window.
            game.Request("InitRandoHeadless");
        }
        game.Request("SetSeed", { { "seed", seed } });
        auto dumpStatic = game.Fn<FleetDumpRandoStaticDataFn>("DumpRandoStaticData");
        if (dumpStatic == nullptr) {
            throw std::runtime_error(game.name + " has no DumpRandoStaticData");
        }
        auto dump = game.TakeJson(dumpStatic());
        if (!dump) {
            throw std::runtime_error(game.name + " could not prepare its randomizer data");
        }
        out.dump = std::move(*dump);
        auto settings = game.Request("DumpSettings");
        if (!settings) {
            throw std::runtime_error(game.name + " could not dump its settings");
        }
        out.settings = std::move(*settings);
        return 0;
    });

    // Every open check gets its vanilla item. A check whose vanilla item has no randomizer name keeps a junk item.
    std::string junkFallback;
    for (const auto& entry : out.dump.at("pool")) {
        if (entry.value("category", "") == "junk") {
            junkFallback = entry.at("name");
            break;
        }
    }
    for (const auto& check : out.dump.at("checks")) {
        const std::string name = check.at("name");
        std::string vanilla = check.value("vanilla", "");
        if (vanilla.empty()) {
            if (junkFallback.empty()) {
                throw std::runtime_error(game.name + " check " + name + " has no vanilla item (old engine build?)");
            }
            vanilla = junkFallback;
        }
        out.placements[name] = vanilla;
    }
}

// The checks this world reaches with its vanilla items alone.
void SearchVanilla(const GameHandle& game, GameDump& out) {
    AsGame(game, [&] {
        auto reset = game.Fn<FleetRandoResetFn>("RandoReset");
        auto own = game.Fn<FleetRandoSetOwnedItemsFn>("RandoSetOwnedItems");
        auto reach = game.Fn<FleetRandoGetReachableChecksFn>("RandoGetReachableChecks");
        if (reset == nullptr || own == nullptr || reach == nullptr) {
            throw std::runtime_error(game.name + " has no logic services");
        }
        std::map<std::string, std::string> itemAt;
        for (const auto& [check, item] : out.placements.items()) {
            itemAt[check] = item.get<std::string>();
        }
        for (const auto& fixed : out.dump.at("fixed")) {
            itemAt[fixed.at("check").get<std::string>()] = fixed.at("item").get<std::string>();
        }
        reset();
        std::set<std::string> reached;
        for (int sphere = 0; sphere < 200; sphere++) {
            auto found = game.TakeJson(reach());
            if (!found || !found->is_array()) {
                throw std::runtime_error(game.name + " logic search failed");
            }
            json newItems = json::array();
            for (const auto& check : *found) {
                const std::string name = check.get<std::string>();
                if (reached.insert(name).second) {
                    if (auto item = itemAt.find(name); item != itemAt.end()) {
                        newItems.push_back(item->second);
                    }
                }
            }
            if (newItems.empty()) {
                break;
            }
            const std::string text = newItems.dump();
            own(text.c_str());
        }
        out.vanillaReachable = std::move(reached);
        return 0;
    });

    std::set<std::string> priced;
    const json prices = out.dump.value("prices", json::object());
    for (const auto& [name, price] : prices.items()) {
        priced.insert(name);
    }
    for (const auto& check : out.dump.at("checks")) {
        const std::string name = check.at("name");
        if (check.value("vanillaCategory", "") == "junk" && out.vanillaReachable.contains(name) &&
            !priced.contains(name)) {
            out.junkChecks.push_back(name);
        }
    }
}

json GenerateSeed() {
    std::random_device device;
    const uint32_t masterSeed = (static_cast<uint32_t>(device()) & 0x7FFFFFFF) | 1u;
    std::mt19937 rng(masterSeed);
    SPDLOG_WARN("[FleetCombo] Generating seed {}", masterSeed);

    std::map<std::string, GameDump> dumps;
    for (const auto& game : sGames) {
        PrepareGame(game, masterSeed, dumps[game.name]);
        SearchVanilla(game, dumps[game.name]);
        SPDLOG_WARN("[FleetCombo] {}: {} checks, {} reachable with vanilla items, {} junk spots for NEI items",
                    game.name, dumps[game.name].placements.size(), dumps[game.name].vanillaReachable.size(),
                    dumps[game.name].junkChecks.size());
    }

    // Item pairing tables: game -> item name -> key, and game -> key -> item name.
    json pairs = json::object();
    json byKey = json::object();
    // NEI items: key -> game -> pool item name. Each is placed once; the pairing hands the twin to the other world.
    std::map<std::string, std::map<std::string, std::string>> nei;
    for (auto& [name, data] : dumps) {
        pairs[name] = json::object();
        byKey[name] = json::object();
        std::map<std::string, std::string> keyOfItem;
        for (const auto& item : data.dump.at("items")) {
            const std::string key = PairKeyOf(item);
            if (!key.empty()) {
                pairs[name][item.at("name").get<std::string>()] = key;
                byKey[name][key] = item.at("name");
                keyOfItem[item.at("name").get<std::string>()] = key;
            }
        }
        for (const auto& entry : data.dump.at("pool")) {
            const std::string item = entry.at("name");
            auto key = keyOfItem.find(item);
            if (key != keyOfItem.end() && item.size() > 0 && entry.contains("name") && !IsReplicaOrForeign(key->second) &&
                data.dump.at("items").is_array()) {
                // Only mod items carry a textKey; vanilla items only carry pairKeys (which are replicas).
                bool isMod = false;
                for (const auto& def : data.dump.at("items")) {
                    if (def.at("name") == item && def.contains("textKey")) {
                        isMod = true;
                        break;
                    }
                }
                if (isMod) {
                    nei[key->second][name] = item;
                }
            }
        }
    }

    std::map<std::string, std::vector<std::string>> free;
    for (auto& [name, data] : dumps) {
        free[name] = data.junkChecks;
        std::shuffle(free[name].begin(), free[name].end(), rng);
    }
    json neiPlaced = json::array();
    for (const auto& [key, owners] : nei) {
        std::vector<std::string> candidates;
        for (const auto& [game, item] : owners) {
            if (!free[game].empty()) {
                candidates.push_back(game);
            }
        }
        if (candidates.empty()) {
            SPDLOG_WARN("[FleetCombo] No junk spot left for NEI item {}", key);
            continue;
        }
        const std::string game = candidates[std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(rng)];
        const std::string check = free[game].back();
        free[game].pop_back();
        dumps[game].placements[check] = owners.at(game);
        neiPlaced.push_back({ { "key", key }, { "game", game }, { "check", check }, { "item", owners.at(game) } });
    }
    SPDLOG_WARN("[FleetCombo] Placed {} NEI items", neiPlaced.size());

    json sharedItems = json::array();
    for (const auto& key : kMaxKeys) {
        sharedItems.push_back(key);
    }
    for (const auto& key : kSumKeys) {
        sharedItems.push_back(key);
    }

    json seed = { { "masterSeed", masterSeed },
                  { "seed", masterSeed },
                  { "goal", { { "type", "bosses" }, { "requiredPieces", 0 }, { "totalPieces", 0 } } },
                  { "startingGame", "OOT" },
                  { "sharedItems", sharedItems },
                  { "foreign", json::array() },
                  { "entrances", json::object() },
                  { "fleetCombo", { { "mode", "vanilla+nei" }, { "pairs", pairs }, { "byKey", byKey },
                                    { "nei", neiPlaced } } } };
    for (auto& [name, data] : dumps) {
        json game = data.settings;
        seed["entrances"][name] = game.value("entrances", json::array());
        game.erase("entrances");
        game["placements"] = data.placements;
        game["prices"] = data.dump.value("prices", json::object());
        if (!game.contains("masterQuestDungeons")) {
            game["masterQuestDungeons"] = json::array();
        }
        seed[name] = std::move(game);
    }
    return seed;
}

// -----------------------------------------------------------------------------------------------------------------
// Host services

const char* GetSession() {
    if (!sSession) {
        return nullptr;
    }
    sSessionText = json{ { "seed", sSession->seed }, { "slot", sSession->slot } }.dump();
    return sSessionText.c_str();
}

bool SelectFile(int slot) {
    // A file that was created as a combined save brings its session back; plain files play without one.
    if (auto loaded = LoadSession(slot)) {
        Activate(std::move(*loaded));
    } else {
        sSession.reset();
    }
    return false; // the game's own file select continues
}

// The games keep their own files; the session file of a slot follows its OoT save.
bool ManageSave(const char* operation, int from, int to) {
    if (operation == nullptr) {
        return false;
    }
    std::error_code error;
    const std::string op = operation;
    if (op == "erase") {
        fs::remove(SessionPath(from), error);
        if (sSession && sSession->slot == from) {
            sSession.reset();
        }
    } else if (op == "copy" && to >= 0) {
        if (fs::exists(SessionPath(from))) {
            fs::copy_file(SessionPath(from), SessionPath(to), fs::copy_options::overwrite_existing, error);
        }
    }
    return false; // the game still erases or copies its own file
}

bool HasGeneratedSeed(int slot) {
    return sGenerated.contains(slot);
}

bool GenerateRandomizer(int slot) {
    sGenerationState = kGenerating;
    try {
        sGenerated[slot] = GenerateSeed();
        sGenerationState = kDone;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[FleetCombo] Seed generation failed: {}", e.what());
        sGenerationState = kFailed;
    }
    // Majora's Mask prepares its seed on a scratch save context: "Finish" puts the live one back.
    if (const GameHandle* mm = FindGame("mm")) {
        AsGame(*mm, [&] {
            mm->Request("Finish");
            return 0;
        });
    }
    // The file select was running as OoT: give the thread back to it.
    Ship::GameRegistry::SetActive("oot");
    return true;
}

int RandomizerGenerationState() {
    const int state = sGenerationState;
    if (state == kDone || state == kFailed) {
        sGenerationState = kIdle; // reported once, like a native generation finishing
    }
    return state;
}

bool BeginCreatedSave(int slot) {
    auto generated = sGenerated.find(slot);
    if (generated == sGenerated.end()) {
        return false;
    }
    Session session;
    session.slot = slot;
    session.seed = std::move(generated->second);
    sGenerated.erase(generated);
    Activate(std::move(session));
    return true;
}

void FinishCreatedSave() {
    SaveSession();
}

void CommitSave() {
    SaveSession();
}

bool ReportObtained(const char* game, const char* check) {
    if (!sSession || game == nullptr || check == nullptr) {
        return false;
    }
    const std::string from = game;
    const std::string id = from + ":" + check;
    if (!sSession->obtained.insert(id).second) {
        return true;
    }
    const json& seed = sSession->seed;
    const json placements = seed.at(from).value("placements", json::object());
    if (!placements.contains(check)) {
        return true;
    }
    const std::string item = placements.at(check);
    const json combo = seed.value("fleetCombo", json::object());
    const std::string key = combo.value("pairs", json::object()).value(from, json::object()).value(item, "");
    const std::string to = OtherGame(from);
    if (!key.empty()) {
        const json twins = combo.value("byKey", json::object()).value(to, json::object());
        if (twins.contains(key)) {
            sSession->deliveries[to].push_back({ { "id", id }, { "item", twins.at(key) } });
            SPDLOG_WARN("[FleetCombo] {} found {} at {}: {} also gets {}", from, item, check, to,
                        twins.at(key).get<std::string>());
        }
    }
    SaveSession();
    return true;
}

const char* GetDeliveries(const char* game) {
    static std::string text;
    if (!sSession || game == nullptr) {
        return nullptr;
    }
    auto found = sSession->deliveries.find(game);
    text = (found != sSession->deliveries.end() ? found->second : json::array()).dump();
    return text.c_str();
}

bool ReportSharedTiers(const char* game, const char* tiersJson) {
    if (!sSession || game == nullptr || tiersJson == nullptr) {
        return false;
    }
    const json tiers = json::parse(tiersJson, nullptr, false);
    if (!tiers.is_object()) {
        return false;
    }
    auto& seen = sSession->seen[game];
    bool changed = false;
    for (const auto& key : kMaxKeys) {
        const int value = tiers.value(key, 0);
        seen[key] = value;
        if (value > sSession->shared[key]) {
            sSession->shared[key] = value;
            changed = true;
        }
    }
    for (const auto& key : kSumKeys) {
        if (!tiers.contains(key)) {
            continue;
        }
        const int value = tiers.at(key);
        auto last = seen.find(key);
        const int before = last != seen.end() ? last->second : kStartingQuarterHearts;
        auto total = sSession->shared.find(key);
        if (total == sSession->shared.end()) {
            total = sSession->shared.emplace(key, kStartingQuarterHearts).first;
        }
        if (value != before) {
            // Found (or lost, e.g. a save reload) quarters in this world: the difference counts for both.
            total->second = std::clamp(total->second + (value - before), kStartingQuarterHearts, kMaxQuarterHearts);
            changed = true;
        }
        seen[key] = value;
    }
    if (changed) {
        SaveSession();
    }
    return true;
}

const char* GetSharedTiers(const char* game) {
    static std::string text;
    if (!sSession || game == nullptr) {
        return nullptr;
    }
    auto& seen = sSession->seen[game];
    json out = json::array();
    for (const auto& key : kMaxKeys) {
        const int shared = sSession->shared[key];
        if (shared > seen[key]) {
            json entry = { { "key", key }, { "tier", shared } };
            auto item = kTierItem.find(key);
            entry["item"] = item != kTierItem.end() ? item->second : "";
            out.push_back(std::move(entry));
        }
    }
    for (const auto& key : kSumKeys) {
        auto total = sSession->shared.find(key);
        if (total != sSession->shared.end() && seen.contains(key) && seen[key] != total->second) {
            out.push_back({ { "key", key }, { "tier", total->second }, { "item", "" } });
            seen[key] = total->second; // the game applies it now; its next report must not count it again
        }
    }
    text = out.dump();
    return text.c_str();
}

bool IsRuntimeTest() {
    return false;
}

} // namespace

void SessionInit(const fs::path& root) {
    sSessionDir = root / "sessions";
}

void SessionAddGame(const SessionGame& game) {
    sGames.push_back({ game.name, game.getFunction, game.started });
}

void SessionShutdown() {
    SaveSession();
}

void* SessionFunction(const char* name) {
    static const std::map<std::string, void*> services = {
        { "GetSession", reinterpret_cast<void*>(GetSession) },
        { "SelectFile", reinterpret_cast<void*>(SelectFile) },
        { "ManageSave", reinterpret_cast<void*>(ManageSave) },
        { "HasGeneratedSeed", reinterpret_cast<void*>(HasGeneratedSeed) },
        { "GenerateRandomizer", reinterpret_cast<void*>(GenerateRandomizer) },
        { "RandomizerGenerationState", reinterpret_cast<void*>(RandomizerGenerationState) },
        { "BeginCreatedSave", reinterpret_cast<void*>(BeginCreatedSave) },
        { "FinishCreatedSave", reinterpret_cast<void*>(FinishCreatedSave) },
        { "CommitSave", reinterpret_cast<void*>(CommitSave) },
        { "ReportObtained", reinterpret_cast<void*>(ReportObtained) },
        { "GetDeliveries", reinterpret_cast<void*>(GetDeliveries) },
        { "ReportSharedTiers", reinterpret_cast<void*>(ReportSharedTiers) },
        { "GetSharedTiers", reinterpret_cast<void*>(GetSharedTiers) },
        { "IsRuntimeTest", reinterpret_cast<void*>(IsRuntimeTest) },
    };
    if (name == nullptr) {
        return nullptr;
    }
    auto found = services.find(name);
    return found != services.end() ? found->second : nullptr;
}

} // namespace FleetCombo
