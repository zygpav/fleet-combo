#pragma once
// The combined playthrough: one seed shared by both worlds (vanilla placements + shuffled NEI items),
// shared hearts and inventory tiers, and cross-world deliveries. Increment 1 offers none of these services, so
// each game runs its own unlinked save; the portal still carries the save slot across.

#include <filesystem>

namespace FleetCombo {
void SessionInit(const std::filesystem::path& root);
void SessionShutdown();
// The host service called `name`, or nullptr when the session does not offer it (yet).
void* SessionFunction(const char* name);
} // namespace FleetCombo
