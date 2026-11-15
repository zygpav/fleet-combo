#pragma once
// The combined playthrough ("session"): one seed shared by both worlds, kept per save slot.
//
//  * Seed: every check keeps its vanilla item; each NEI item replaces a junk item (rupees, recovery hearts,
//    ammo refills) at a check the vanilla items already reach in that world, so NEI items never gate anything.
//  * Shared hearts: quarter hearts found in either world add up in both.
//  * Shared upgrades: bows, bomb bags, magic, wallets, hookshot, arrows, lens, songs, masks: the best of both.
//  * Paired items: an item found in one world is delivered to the other when the other world has the same item
//    (skijer's replica keys: oot.* items in Termina, mm.* masks in Hyrule, NEI items in both).

#include <filesystem>

#include <fleet/FleetGameApi.h>

namespace FleetCombo {

struct SessionGame {
    const char* name;              // "oot" / "mm"
    FleetGetFunctionFn getFunction; // the module's Fleet_GetFunction
    const bool* started;           // true once its loop runs
};

void SessionInit(const std::filesystem::path& root);
void SessionAddGame(const SessionGame& game);
void SessionShutdown();
// The host service called `name`, or nullptr when the session does not offer it.
void* SessionFunction(const char* name);

} // namespace FleetCombo
