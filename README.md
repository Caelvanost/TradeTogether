# TradeTogether v0.8.0

Synchronized trade-offer interface with automatic instance-aware item and gold transfer for Skyrim Together Reborn.

## Keyboard shortcut

TradeTogether uses **T** to start a trade with the targeted player and to validate your offer during the trade.

In Skyrim, **T is normally bound to Wait**. Before using TradeTogether, unbind or reassign **T** from the **Wait** command to avoid conflicts.

## Trade flow

1. Both players install the same version of TradeTogether and connect to Skyrim Together Reborn.
2. The requester targets the other character and presses **T**.
3. The targeted player receives an **Accept / Refuse** prompt.
4. After acceptance, the personal inventory opens for both players in offer-composition mode.
5. Each player composes their offer:

   - **Insert**: add one unit of the selected item;
   - **Delete**: remove one unit of the selected item;
   - **Numpad + / -**: add or remove **1 septim**;
   - **Shift + Numpad + / -**: add or remove **10 septims**;
   - **Ctrl + Numpad + / -**: add or remove **100 septims**;
   - **T**: display both offers and mark yourself as ready;
   - **Tab**: cancel the entire trade.

   TradeTogether deliberately avoids **E** because Skyrim uses it to consume or activate inventory items, and avoids **A** because Skyrim uses it for inventory favorites by default.

6. Any item or gold change automatically clears the ready state.
7. When both players are ready, each player sees both baskets and confirms one final time.
8. After both confirmations, TradeTogether performs an invisible preflight and automatically transfers the offered items and septims.

Quest items are rejected. Gold is managed separately from normal item lines, so the Gold inventory entry cannot be added with Insert. An offer can contain up to 24 item lines and may contain only gold or even be empty, allowing one-way gifts.

The initial request expires after 30 seconds, and an inactive session expires after 5 minutes.

## Gold trading

Gold is synchronized as a dedicated `gold` field in the trade state instead of being encoded as a normal `OfferLine`.

Before final confirmation, TradeTogether verifies that the local player still owns at least the promised amount. The offered septims are then transferred through Skyrim's native container-transfer path to the other player's STR proxy, just like normal stackable items.

The amount is clamped between **0** and the player's currently available gold, so the offer cannot go negative or exceed the player's balance.

## Native instance-aware item transfer

TradeTogether transfers the exact local inventory stack to Skyrim Together Reborn's proxy of the other player using Skyrim's native container-transfer path.

When an item has an `ExtraDataList`, that exact list is supplied to `RemoveItem`. This is intended to preserve:

- smithing / tempering improvements;
- custom or extra enchantments;
- enchantment charge;
- custom item names;
- other per-instance data carried by Skyrim's native extra-data list.

Standard stackable objects without per-instance data are transferred as normal base stacks.

TradeTogether identifies the active peer from the most recent trade-network packet, resolves the corresponding high-process actor by character name, and uses that STR proxy as the native transfer destination.

### Current limitation

If two different modified instances have the same base FormID **and the exact same display name**, Skyrim's inventory representation may not expose enough information to distinguish them perfectly. TradeTogether prefers matching `ExtraDataList` stacks before falling back to an unmodified base stack.

## Network

TradeTogether uses an independent UDP channel on port **27993**. Discovery is automatic on a local network. Both players must allow Skyrim through their private-network firewall.

For a connection without LAN discovery, set `AutoDiscovery=0`, `PeerHost`, and `PeerPort` in `Data/SKSE/Plugins/TradeTogether.ini`. UDP port forwarding may be required depending on the router.

Players are associated by their character name. The initial target may be any `Actor` other than the local player because STR does not expose a stable public SKSE API for formally identifying its remote actors.

The log is located at:
`Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log`.

## Build

```powershell
.\build_release.bat
```

The script builds the DLL, copies it together with the INI into the package, and creates:

```text
dist/TradeTogether-v0.8.0-Vortex.zip
```
