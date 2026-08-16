# TradeTogether v0.5.4

Synchronized trade-offer interface for Skyrim Together Reborn.

## Keyboard shortcut

TradeTogether uses **T** to start a trade with the targeted player and to
validate your offer during the trade.

In Skyrim, **T is normally bound to Wait**. Before using TradeTogether, open
the game controls and unbind or reassign **T** from the **Wait** command to
avoid conflicts.

## Trade flow

1. Both players install the same version of TradeTogether and connect to
   Skyrim Together Reborn.
2. The requester targets the other character and presses **T**.
3. The targeted player receives an **Accept / Refuse** prompt.
4. After acceptance, the personal inventory opens for both players in offer
   composition mode.
5. Each player selects the items they want to offer:

   - **Insert**: add one unit of the selected item;
   - **Delete**: remove one unit;
   - **T**: display both offers and mark yourself as ready;
   - **Tab**: cancel the entire trade.

   TradeTogether deliberately avoids **E** because Skyrim uses it to consume or
   activate inventory items, and avoids **A** because Skyrim uses it for
   inventory favorites by default.

   TradeTogether does not use F8, F9, or F10 to avoid conflicts with Heart of
   Magic, quick load, and OStim Together.

6. Any offer change automatically clears the ready state.
7. When both players are ready, each player sees both baskets and must confirm
   one final time.
8. After both players confirm, the target's native inventory opens for the
   requester so the validated transfer can be completed manually.

Quest items are rejected. An offer can contain up to 24 lines and may be empty,
allowing one-way gifts. The initial request expires after 30 seconds, and an
inactive session expires after 5 minutes.

## Current limitation

The interface synchronizes and locks both players' intent, but it does not move
items automatically. Skyrim Together Reborn does not expose a stable public API
that guarantees transfer of enchanted, tempered, or renamed item instances.
The final transfer therefore remains manual through the native inventory
interface already validated with STR.

## Network

TradeTogether uses an independent UDP channel on port **27993**. Discovery is
automatic on a local network. Both players must allow Skyrim through their
private-network firewall.

For a connection without LAN discovery, set `AutoDiscovery=0`, `PeerHost`, and
`PeerPort` in `Data/SKSE/Plugins/TradeTogether.ini`. UDP port forwarding may be
required depending on the router.

Players are associated by their character name. As in previous versions, the
initial target may be any `Actor` other than the local player because STR does
not expose a stable public SKSE API for formally identifying its remote actors.

The log is located at:
`Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log`.

## Build

```powershell
.\build_release.bat
```

The script builds the DLL, copies it together with the INI into the package,
and creates:

```text
dist/TradeTogether-v0.5.4-Vortex.zip
```
