# Overnight log

What changed, what to test, what's waiting on you. Newest first.

## ✅ Latest build: green (all stages so far)

**Download:** https://github.com/zygpav/fleet-combo/actions/runs/43760985154 → Artifacts → `FleetCombo-windows`

* Both games built from the `fleet-combo` branches of your forks (shared hearts, vanilla data, era lock,
  warp songs back to Hyrule).
* All 82 OoT mods and all 78 MM mods built, including `mm_collectables` with the Song of Soaring → Termina change.
* No compiler warnings in any file I changed.

**Test order that tells me the most:**
1. Load your existing file → walk into the Mask Shop. If MM doesn't load, a popup now says why. Send me the log.
2. New file → Randomizer → Generate → Start. Check your hearts, then go to Termina via the Mask Shop.
3. In Termina, pick up a Piece of Heart, return through the Clock Tower door, and check your hearts in Hyrule.

## How to get a build

Actions → **Build Fleet Combo (Windows)** → the newest green run → download `FleetCombo-windows`.
Unzip it over your existing `Downloads\FleetCombo-windows` folder: your extracted ROMs
(`oot\oot.o2r`, `mm\mm.o2r`) and saves stay.

If something goes wrong, the log is `logs\Zelda Fleet Combo.log` next to `FleetCombo.exe`
(plus `oot\logs` and `mm\logs`). Tell me and I'll read it from your Downloads folder.

## What's in the code now

### 1. Travel (stage 1)
* **Mask Shop → Termina, Clock Tower door → Hyrule.** It didn't fire in your first test. The new build
  logs every travel attempt and pops up a message if Majora's Mask can't start, so we'll see why.

### 2. Shared save (stage 2)
To get one, start a **new file in OoT** and pick the **Randomizer** quest type, then **Generate**
(this builds the combined seed for both worlds), then **Start** and name your file.
* **Hearts are shared.** A Heart Container or Piece of Heart found in either world counts in both (max 20 hearts).
* **Upgrades are shared.** Bows/quivers, bomb bags, bombchus, magic, wallets, hookshot, fire/ice/light
  arrows, lens, Epona's Song, Song of Storms, Goron/Zora/Keaton masks, Bunny Hood, Mask of Truth: the better of
  the two worlds.
* **Items follow you.** When you find an item that also exists in the other world (OoT items in Termina, MM
  masks in Hyrule, every NEI item), the other world gets its copy too.
* A normal (non-Randomizer) OoT file still plays, just without the shared save.

### 3. Vanilla items + shuffled NEI items (stage 3)
* Every chest, heart piece, song, etc. holds its **normal item**.
* Each NEI item takes the place of a **junk item** (rupees, recovery hearts, ammo refills) somewhere in Hyrule
  or Termina. It's only placed at spots your **vanilla items already reach**, so NEI items never block
  progress and you never need one to get another.
* Tip: in OoT's Randomizer settings menu, set things like forest/Door of Time/bridge to their vanilla values
  for the most vanilla-feeling game. (A one-click "vanilla" preset is on the to-do list.)

### 4. Era freedom (stage 4)
* OoT menu → **Enhancements → Quality of Life → Fleet Combo → World Era**:
  *Follow Link's age* (normal), *Child era (past)*, *Adult era (future)*.
* Lock an era, then change Link's age (NEI **Time Gate**, Door of Time) to bring **adult Link into the
  past** or **child Link into the future**. Takes effect at the next loading zone.
* **Majora's Mask's clock only runs in Termina.** MM is fully paused while you're in Hyrule.

### 5. Travel songs (stage 6)
* **In Hyrule:** Song of Soaring (NEI MM songs) → the owl menu has a last page **"Termina (Clock Town)"**.
* **In Termina:** play an **OoT warp song** (Minuet, Bolero, Serenade, Requiem, Nocturne, Prelude) to land
  on its warp pad in Hyrule. With the shared save, learning a warp song in Hyrule teaches it in Termina too.

## Not done / known gaps
* **Nothing above has been played yet.** It compiles and the shared-save logic passes a test run against
  mock games, but the first real run will find bugs. Please report what happens.
* Erasing an OoT file doesn't erase the matching MM file yet; a combined seed in a slot that already had an
  old MM file will refuse to load MM. Use a fresh slot, or delete `mm\Save\file<N>.sav` for that slot.
* Stage 5 (polishing forms and items across worlds: Zora swimming, item behaviour in the "wrong" world)
  needs your play-testing to know what's broken.
