# TradeTogether v0.3.2 — Direct Inventory branch

Direct native inventory access for Skyrim Together Reborn.

This branch restores the simple pre-offer TradeTogether behavior: target another Actor and press **T** to open that Actor's native inventory immediately.

## Keyboard shortcut

TradeTogether uses **T** for direct inventory access.

In Skyrim, **T is normally bound to Wait**. Before using this branch, open the game controls and unbind or reassign **T** from the **Wait** command to avoid conflicts.

## How it works

1. Connect to Skyrim Together Reborn.
2. Aim at the other player's local Actor proxy with the crosshair.
3. Press **T**.
4. TradeTogether resolves the targeted Actor and calls Papyrus `Actor.OpenInventory(true)` on it.
5. Skyrim Together Reborn remains responsible for synchronizing the inventory and item-instance data.

There is no trade request, no **Accept / Refuse** prompt, no synchronized offer composition, no ready state, and no final double confirmation on this branch.

The previous offer controls are not used by the direct-inventory input path:

- **F6** is not a TradeTogether trade hotkey on this branch;
- **E** does not add an item to an offer;
- **Delete** does not remove an item from an offer;
- **Tab** does not cancel an offer session.

## Targeting behavior

The direct action accepts any targeted `Actor` other than the local player. Skyrim Together Reborn does not expose a stable public SKSE API that lets TradeTogether formally distinguish remote-player proxies from ordinary NPCs, so targeting an NPC can also open that NPC's inventory.

If no valid Actor is under the crosshair, TradeTogether displays a notification and does nothing.

## Networking

The **T direct-inventory action itself does not use the synchronized offer protocol or require a TradeTogether network confirmation**. It directly opens the local proxy Actor's inventory and relies on Skyrim Together Reborn for inventory synchronization.

Some legacy synchronized-offer and remote-network code is still present in the source tree and the Trade subsystem is still initialized at startup. That inherited code is not triggered by the direct-inventory keyboard path.

## Version

The canonical version of this branch is **0.3.2**, as defined by `CMakeLists.txt`.

Some inherited packaging and log strings still report versions from the later 0.7.x development line. Those strings are legacy leftovers and do not represent the canonical version or behavior of this branch.

## Log

The log is located at:

```text
Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log
```

## Build

From PowerShell in the project directory:

```powershell
.\build_release.bat
```

The DLL is copied to:

```text
package\Data\SKSE\Plugins\TradeTogether.dll
```

Note: the inherited archive/FOMOD scripts still contain 0.7.x version labels and should be updated separately if a correctly versioned v0.3.2 Vortex package is required.
