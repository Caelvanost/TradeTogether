# TradeTogether v0.7.1

Synchronized trade-offer interface with automatic instance-aware item transfer for Skyrim Together Reborn.

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

6. Any offer change automatically clears the ready state.
7. When both players are ready, each player sees both baskets and confirms one
   final time.
8. After both confirmations, TradeTogether performs an invisible preflight and
   automatically transfers the offered items. No direct inventory is opened and
   there are no additional confirmation steps.

Quest items are rejected. An offer can contain up to 24 lines and may be empty,
allowing one-way gifts. The initial request expires after 30 seconds, and an
inactive session expires after 5 minutes.

## Native instance-aware transfer

v0.7.x no longer recreates received equipment from only its base FormID.
Instead, each client transfers the exact local inventory stack to Skyrim
Together Reborn's proxy of the other player using Skyrim's native container
transfer path.

When an item has an `ExtraDataList`, that exact list is supplied to
`RemoveItem`. The instance data therefore travels with the object rather than
being discarded and rebuilt from the base form. This is intended to preserve,
in particular:

- smithing / tempering improvements;
- custom or extra enchantments;
- enchantment charge;
- custom item names;
- other per-instance data carried by Skyrim's native extra-data list.

Standard stackable objects without per-instance data are still transferred as
normal base stacks.

TradeTogether identifies the active peer from the most recent trade-network
packet, resolves the corresponding high-process actor by character name, and
uses that STR proxy as the native transfer destination. The synchronized offer
payload still carries FormID, quantity and display name for validation and UI;
it is no longer used to reconstruct modified equipment on the receiving side.

### v0.7.1

Fixes compatibility with CommonLibSSE-NG 3.5.2 by using the `Actor&` callback
signature required by `ProcessLists::ForEachHighActor`.

### Current limitation

The transfer depends on Skyrim Together Reborn synchronizing the same native
container operation used by direct inventory transfers. This architecture is
specifically intended to preserve instance data instead of attempting to clone
Skyrim's temporary enchantment forms ourselves.

If two different instances have the same base FormID **and the exact same
display name**, Skyrim's inventory UI may not give TradeTogether enough visible
information to distinguish which one was intended. The mod prefers matching
`ExtraDataList` stacks before falling back to an unmodified base stack.

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
dist/TradeTogether-v0.7.1-Vortex.zip
```
