# TradeTogether v0.9.0-udp

TradeTogether adds synchronized player-to-player item and gold trading to Skyrim Together Reborn. The UDP edition now ships as one FOMOD for both **LAN** and **remote Internet** play.

## Trade flow

1. Both players install the same TradeTogether build and connect through Skyrim Together Reborn.
2. Target the other player and press **T**.
3. The target chooses **Accept / Decline**.
4. Both players compose their offers in their own inventory:
   - **Insert**: add one unit of the selected item;
   - **Delete**: remove one unit of the selected item;
   - **Numpad + / -**: add or remove **1 septim**;
   - **Shift + Numpad + / -**: add or remove **10 septims**;
   - **Ctrl + Numpad + / -**: add or remove **100 septims**;
   - **T**: review the offers and mark ready;
   - **Tab**: cancel.
5. Both players receive one final **Confirm / Modify** prompt.
6. After both confirmations, TradeTogether transfers the offered items and gold automatically.

All user-facing notifications, prompts, summaries and validation errors are displayed in English.

## FOMOD network modes

The installer offers exactly one network mode per PC.

### LAN

Choose **LAN** on every PC when all players are on the same local network.

```ini
AutoDiscovery=1
AutoRemoteFromSTR=0
LocalPort=27993
PeerHost=
PeerPort=27993
```

TradeTogether discovers peers automatically by UDP broadcast. No router port forwarding and no manual IP address are required. Windows Firewall must still allow the game/plugin to communicate on the private network.

### Remote Host

Choose **Remote Host** on the PC hosting remote players over the Internet.

```ini
AutoDiscovery=0
AutoRemoteFromSTR=0
LocalPort=27993
PeerHost=
PeerPort=27993
```

Forward **UDP 27993** on the router to the Host PC and allow UDP 27993 through Windows Firewall. Leave `PeerHost` empty. The Host learns the Client's NAT endpoint when the Client contacts it.

### Remote Client

Choose **Remote Client** on the remote player's PC.

```ini
AutoDiscovery=0
AutoRemoteFromSTR=1
LocalPort=27993
PeerHost=
PeerPort=27993
```

TradeTogether attempts to read Skyrim Together Reborn's last connected server address and contact the same host on UDP 27993.

Automatic STR host discovery is still experimental. If the log shows `remoteConfigured=0`, set `PeerHost` manually to the Host's public IPv4 address and set `AutoRemoteFromSTR=0`. Keep `AutoDiscovery=0` for a true remote/manual test; enabling `AutoDiscovery` makes LAN broadcast discovery take over when both PCs are on the same local network.

## Gold trading

Gold is synchronized separately from normal item lines. The offered amount is clamped to the player's current balance and validated again immediately before transfer. Gold-only trades, item-only trades, mixed trades and one-way gifts are supported.

## Instance-aware item transfer

TradeTogether transfers the actual local Skyrim inventory instance through Skyrim's native container-transfer path. When an item has an `ExtraDataList`, the exact list is supplied to `RemoveItem` with the other player's STR proxy as the destination.

This is intended to preserve smithing/tempering improvements, enchantments and charge, custom item names and other per-instance data. Standard stackable items are transferred normally.

## Current limitations

- Both players must use the exact same TradeTogether build.
- The target player must have a loaded/high-process STR actor proxy for native transfer.
- Two modified instances with the same base FormID and identical displayed names may not always be distinguishable.
- Remote Host requires reachable UDP 27993; CGNAT or restrictive networks can prevent conventional port forwarding.
- Automatic Remote Client discovery from STR is experimental; manual `PeerHost` is the current fallback.

## Versioning

This release is:

```text
v0.9.0-udp
```

CMake keeps the numeric project version `0.9.0`; runtime logs, FOMOD metadata and the deployment archive use the `-udp` release suffix.

## Log

```text
Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log
```

Useful networking entries include `Trade UDP started`, `manual remote configured`, `automatic STR remote configured`, `Trade peer discovered` and packet-routing diagnostics.

## Build

```powershell
.\build_release.bat
```

Expected archive:

```text
dist/TradeTogether-v0.9.0-udp-Vortex.zip
```

The archive contains the FOMOD profiles **LAN**, **Remote Host** and **Remote Client**.
