# Zelda Fleet Combo

Ocarina of Time and Majora's Mask in one window, built on skijer's **Unbound** ports and his **NEI**
(Not Enough Items) mods. Any Link (child, adult, or MM Link) can travel between Hyrule and Termina, and the NEI items
and MM transformation masks work in both worlds.

This is a personal project. All game data comes from **your own ROMs**: nothing from Nintendo is included.

## What's in a build

```
FleetCombo/
  FleetCombo.exe        start here: both games in one window
  oot/                  Ship of Harkinian: Unbound (+ NEI mods in oot/mods)
  mm/                   2 Ship 2 Harkinian: Unbound (+ NEI mods in mm/mods)
```

## First run

1. Download the latest `FleetCombo-windows` artifact from **Actions → Build Fleet Combo** and unzip it.
2. Run `oot/soh.exe` once and point it at your **Ocarina of Time** ROM. It creates `oot.o2r` and quits or
   shows the title screen. Close it.
3. Run `mm/2ship.exe` once and point it at your **Majora's Mask** ROM. It creates `mm.o2r`. Close it.
4. Run `FleetCombo.exe`.

## Travelling between worlds

* **Hyrule → Termina**: walk into the **Happy Mask Shop** in Castle Town Market.
* **Termina → Hyrule**: go back through the **Clock Tower** door (inside the tower, where you first met the Mask
  Salesman).

## Roadmap

| Stage | What | Status |
|---|---|---|
| 1 | One window, both games, Mask Shop travel, NEI mods loaded in both worlds | in progress |
| 2 | Shared save: one file, shared hearts and shared inventory | planned |
| 3 | Vanilla items in vanilla places; NEI items shuffled, always reachable with vanilla items | planned |
| 4 | Era freedom: adult in the child era and vice versa; MM clock only runs in Termina | planned |
| 5 | Polishing forms and items across worlds (Zora swimming in Lake Hylia etc.) | planned |
| 6 | A travel song | planned |

## Credits

* [skijer](https://github.com/skijer): SoH Unbound, 2Ship Unbound, NEI mods and the Fleet game contract
* [HarbourMasters](https://github.com/HarbourMasters): Ship of Harkinian, 2 Ship 2 Harkinian, libultraship
* ComboShip by Varuuna, for showing that OoT + MM in one Ship works
