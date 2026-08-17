# TradeTogether v0.6.0

Synchronized trade-offer interface with automatic item transfer for Skyrim Together Reborn.

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
7. When both players are ready, each player sees both baskets and confirms one
   final time.
8. After both confirmations, TradeTogether checks the local offer again and
   automatically performs the exchange. No direct target inventory is opened.

There are no extra confirmation steps for the automatic transfer. The final
preflight is internal and only interrupts the exchange if an offered item is no
longer available.

Quest items are rejected. An offer can contain up to 24 lines and may be empty,
allowing one-way gifts. The initial request expires after 30 seconds, and an
inactive session expires after 5 minutes.

## Automatic transfer

Each client modifies only its own real `PlayerCharacter` after both users have
confirmed:

- the items in the local offer are removed from the local player;
- the items in the remote offer are added to the local player.

This avoids depending on Skyrim Together Reborn's remote actor proxy for the
actual inventory mutation.

### Current limitation

v0.6.0 transfers items by **FormID and quantity**. This is suitable for normal
stackable items and standard unmodified equipment.

Custom instance data is not serialized yet. Renamed, tempered, uniquely
custom-enchanted, or otherwise instance-modified equipment may therefore lose
its per-instance properties if traded in this version. Support for preserving
that extra data is a separate follow-up feature.

## Network

TradeTogether uses an independent UDP channel on port **27993**. Discovery is
automatic on a local network. Both players must allow Skyrim through their
private-network firewall.

For a connection without LAN discovery, set `AutoDiscovery=0`, `PeerHost`, and
`PeerPort` in `Data/SKSE/Plugins/TradeTogether.ini`. UDP port forwarding may be
required depending on the router.

Players are associated by their character name. The initial target may be any
`Actor` other than the local player because STR does not expose a stable public
SKSE API for formally identifying its remote actors.

The log is located at:
`Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log`.

## Build

```powershell
.\build_release.bat
```

The script builds the DLL, copies it together with the INI into the package,
and creates:

```text
dist/TradeTogether-v0.6.0-Vortex.zip
```
