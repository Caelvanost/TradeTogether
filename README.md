# TradeTogether v0.9.0-strpm

STR Plugin Messaging edition of TradeTogether for Skyrim Together Reborn.

This branch keeps the current TradeTogether gameplay and transfer system while replacing TradeTogether's dedicated UDP transport with **STR Plugin Messaging (STRPM)**.

## Requirements

- Skyrim Special Edition / Anniversary Edition
- SKSE
- Skyrim Together Reborn
- `STRPluginMessagingAPI.dll` with a working STR bridge/backend
- The same TradeTogether build on every player

TradeTogether itself opens **no UDP socket** on this branch and requires no TradeTogether-specific port forwarding.

## Trade flow

1. Both players connect through Skyrim Together Reborn.
2. Target the other player's character and press **T**.
3. The target receives an **Accept / Decline** prompt.
4. Both players compose their offer in their personal inventory.
5. **Insert** adds one selected item.
6. **Delete** removes one selected item from the offer.
7. **Numpad +/-** changes gold by 1 septim.
8. **Shift + Numpad +/-** changes gold by 10 septims.
9. **Ctrl + Numpad +/-** changes gold by 100 septims.
10. **T** reviews the offers and marks the local player ready.
11. **Tab** cancels the trade.
12. After both players are ready, both receive the final **Confirm / Modify** prompt.
13. After both confirmations, TradeTogether performs the automatic native transfer.

Quest items are rejected. Item-only, gold-only, mixed item/gold trades and one-way gifts are supported.

## Native instance-aware transfer

The STRPM branch uses the same automatic transfer code as the current main branch.

TradeTogether transfers the actual local Skyrim inventory stack to the loaded STR proxy of the other player. When a selected item has an `ExtraDataList`, the original list is supplied to Skyrim's native `RemoveItem` transfer path.

This is intended to preserve:

- smithing / tempering improvements;
- vanilla and custom enchantments;
- enchantment charge;
- custom item names;
- other per-instance data stored by Skyrim.

Gold uses Skyrim's native Gold form and follows the same native transfer path.

## STR Plugin Messaging transport

Channel:

```text
chaos.trade_together.offer.v1
```

Trade packets keep the existing `TTNET|v1|...` payload format. STRPM messages are sent with **reliable + ordered** flags.

TradeTogether first targets the remote player by display name. If STRPM cannot resolve that player target yet, TradeTogether uses an all-players fallback; the normal TradeTogether `to` field still filters the packet on receipt.

The transport refreshes the local display name before sending, avoiding the startup `Prisoner` name becoming permanently cached after a save loads.

Incoming packets update TradeTogether's most-recent peer identity, which is then used by the native item/gold transfer layer to resolve the corresponding loaded STR actor proxy.

## Diagnostics

The log is located at:

```text
Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log
```

Expected startup entries include:

```text
TradeTogether v0.9.0-strpm loading
TradeTogether STRPM transport started: channel=chaos.trade_together.offer.v1 ...
TradeTogether STRPM status startup: backend=STRBridge ...
TradeTogether v0.9.0-strpm ready ... network=ready
```

If STRPM is missing or incompatible, the log reports:

```text
TradeTogether STRPM API unavailable: STRPluginMessagingAPI.dll not found or incompatible
```

If sending fails, TradeTogether logs the STRPM result and the current bridge/backend diagnostics when available.

## Current status

`v0.9.0-strpm` is the first port of the current TradeTogether feature set to STR Plugin Messaging. It must still be compiled and tested with two connected STR clients before being considered stable.

## Build

```powershell
.\build_release.bat
```

Expected archive:

```text
dist/TradeTogether-v0.9.0-strpm-Vortex.zip
```
