# TradeTogether v0.8.2-udp

Remote UDP edition of TradeTogether for Skyrim Together Reborn. It contains the same validated item and gold trading core as `main` v0.8.2, plus automatic remote-client discovery of the STR server address and Host/Client FOMOD profiles.

## Trade flow

1. Both players install the same TradeTogether build and connect through Skyrim Together Reborn.
2. The requester targets the other player and presses **T**.
3. The other player chooses **Accept / Refuse**.
4. Both players compose their offers in their own inventory:
   - **Insert**: add one unit of the selected item;
   - **Delete**: remove one unit of the selected item;
   - **Numpad + / -**: add or remove **1 septim**;
   - **Shift + Numpad + / -**: add or remove **10 septims**;
   - **Ctrl + Numpad + / -**: add or remove **100 septims**;
   - **T**: review the offers and mark ready;
   - **Tab**: cancel.
5. Both players receive one final **Confirm / Modify** prompt.
6. After both confirmations, TradeTogether transfers the items and gold automatically.

Any item or gold change clears the Ready state. There is no direct-inventory step and no additional user confirmation. TradeTogether deliberately does not intercept **E** or **A**.

## English UI

Starting with **v0.8.2-udp**, all user-facing TradeTogether notifications, trade summaries, validation errors, prompts and MessageBox buttons are displayed in English for the public Nexus Mods release.

## Gold trading

Gold is synchronized as a dedicated `gold` field in the trade state rather than as a normal item line. The offered amount is clamped between 0 and the player's current balance and is verified again immediately before final confirmation.

Gold uses Skyrim's native Gold form (`0000000F`) and is transferred to the other player's STR proxy through the same native container-transfer path used for normal stackable items. Gold cannot be added with Insert, preventing it from being counted twice.

An offer may contain items + gold, only gold, only items, or be empty for a one-way gift.

## Instance-aware item transfer

TradeTogether transfers the actual local Skyrim inventory instance through the native container transfer path. When an item has an `ExtraDataList`, that exact list is passed to `RemoveItem` with the other player's STR proxy as the destination.

This is intended to preserve smithing/tempering improvements, enchantments and charge, custom item names, and other per-instance extra data. Standard stackable items are transferred normally as base stacks.

## Remote UDP mode

TradeTogether uses **UDP port 27993** independently of Skyrim Together Reborn.

### Remote Host

Choose **Remote Host** in the FOMOD.

```ini
AutoDiscovery=0
AutoRemoteFromSTR=0
LocalPort=27993
PeerHost=
PeerPort=27993
```

On the host network:

1. Forward **UDP 27993** on the router to the host PC's local IPv4 address.
2. Allow UDP 27993 through Windows Firewall for Skyrim / TradeTogether.
3. Leave `PeerHost` empty.

The host learns the client's real NAT endpoint from the automatic UDP handshake and can then send trade packets back to that client.

### Remote Client — no IP entry required

Choose **Remote Client** in the FOMOD. No manual IP configuration is normally required.

```ini
AutoDiscovery=0
AutoRemoteFromSTR=1
LocalPort=27993
PeerHost=
PeerPort=27993
```

TradeTogether periodically reads Skyrim Together Reborn's Chromium/localStorage cache and looks for STR's `last_connected_address`. Once the player connects to a STR server, TradeTogether extracts the host address and automatically uses the same host on **UDP 27993**.

For example, if STR connects to:

```text
203.0.113.42:10578
```

TradeTogether automatically contacts:

```text
203.0.113.42:27993
```

It sends a `HELLO` to the host, which lets the host learn the client's NAT source endpoint before either player starts a trade. Either side can therefore initiate the first trade.

If the player changes STR servers, the address is re-read periodically and TradeTogether updates the remote endpoint automatically.

`PeerHost` remains available only as a manual fallback. The client normally does **not** need a router port-forward rule.

## LAN mode

For LAN use, the regular `main` branch remains simpler and uses automatic broadcast discovery. The `udp` branch is intended for remote Internet play.

## Current limitations

- Both players must use matching TradeTogether builds.
- The target player must be represented by a loaded/high-process STR actor proxy for the native transfer.
- If two modified instances have the same base FormID and exactly the same display name, Skyrim may not expose enough information to distinguish them perfectly.
- The host still needs reachable UDP 27993. CGNAT or restrictive firewalls can prevent conventional port forwarding.
- Automatic client discovery depends on STR continuing to store `last_connected_address` in its Chromium localStorage cache. `PeerHost` remains the fallback if this changes.

## Versioning

The functional version follows `main`. The remote branch appends `-udp` to make its build unambiguous:

```text
main: v0.8.2
udp:  v0.8.2-udp
```

CMake's `project(... VERSION ...)` field must remain numeric, so it stays `0.8.2`; `TRADETOGETHER_RELEASE_VERSION`, runtime logs, FOMOD metadata and deployment archive use `0.8.2-udp`.

## Log

```text
Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log
```

Useful entries include gold key presses, `automatic STR remote configured`, discovered peer addresses, sent packet counts, gold transfers and native instance-transfer entries.

## Build

```powershell
.\build_release.bat
```

Expected archive:

```text
dist/TradeTogether-v0.8.2-udp-Vortex.zip
```

The archive contains the FOMOD installer with **Remote Client** and **Remote Host** profiles.
