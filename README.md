# TradeTogether v0.10.0-strpm

STR Plugin Messaging edition of TradeTogether for Skyrim Together Reborn.

This branch keeps the current TradeTogether gameplay and transfer system while replacing TradeTogether's dedicated UDP transport with **STR Plugin Messaging (STRPM)**.

## Requirements

- Skyrim Special Edition / Anniversary Edition
- SKSE
- Skyrim Together Reborn
- A current `STRPluginMessagingAPI.dll` build exposing the STR bridge and Proxy Resolver
- The same TradeTogether build on every player

TradeTogether itself opens **no UDP socket** on this branch and requires no TradeTogether-specific port forwarding.

## Trade flow

1. Both players connect through Skyrim Together Reborn.
2. Target the other player's character and press **T**.
3. The target receives an **Accept / Decline** MessageBox.
4. Both players compose their offer in their personal inventory.
5. **Insert** adds one selected item.
6. **Delete** removes one selected item from the offer.
7. **Numpad +/-** changes gold by 1 septim.
8. **Shift + Numpad +/-** changes gold by 10 septims.
9. **Ctrl + Numpad +/-** changes gold by 100 septims.
10. **T** reviews the offers and opens the **Ready / Modify** summary MessageBox.
11. **Tab** cancels the trade while composing the offer.
12. After both players are ready, both receive the final **Confirm / Modify** MessageBox.
13. After both confirmations, TradeTogether performs the automatic native transfer.

Quest items are rejected. Item-only, gold-only, mixed item/gold trades and one-way gifts are supported.

## Connected-player target validation

Starting with **v0.9.6-strpm**, TradeTogether uses STRPM's **Proxy Resolver**.

STRPM reports mappings between each connected player's `ConnectionID` and the local Skyrim FormID of that player's STR proxy. TradeTogether caches these mappings and resolves the displayed proxy name only from those mapped FormIDs.

This means normal NPCs are rejected, while real STR proxies are sent through `TargetKind::kPlayer` with the non-zero `ConnectionID` required by the STR bridge.

## Deferred STRPM receive path

Starting with **v0.9.8-strpm**, TradeTogether does not perform normal gameplay work directly from STRPM's receive callback.

The callback only copies the incoming message into a preallocated ring buffer and returns. A dedicated dispatch thread performs parsing, logging and normal SKSE task scheduling afterwards.

## v0.10.0 — Skyrim Souls RE compatible MessageBoxes

`v0.10.0-strpm` restores the complete MessageBox-based trade UI while changing how TradeTogether constructs and interacts with those menus.

### Native MessageBoxData defaults are preserved

Older STRPM test builds manually overwrote internal `MessageBoxData` fields after creating the data object. In particular, TradeTogether forced `unk48=0`, while current CommonLibSSE-NG defines the normal default as `10`.

v0.10.0 no longer writes any of those internal fields. TradeTogether only supplies:

- translated body text;
- button labels;
- the callback.

The `MessageBoxData` object keeps the defaults initialized by Skyrim/CommonLib and is then queued normally.

This is important for compatibility with mods that alter `MessageBoxMenu` behavior, including **Skyrim Souls RE - Unpaused Menus**.

### Skyrim Souls controls whether MessageBoxMenu pauses the game

TradeTogether does not set or clear any `MessageBoxMenu` pause flags.

If Skyrim Souls RE is installed with:

```ini
[UNPAUSED_MENUS]
bMessageBoxMenu = true
```

TradeTogether still uses its normal MessageBoxes, while Skyrim Souls remains responsible for making `MessageBoxMenu` unpaused.

TradeTogether detects `SkyrimSoulsRE.dll` at `kDataLoaded` only for diagnostics; the mod does not patch or configure Skyrim Souls.

### Trade hotkeys are suspended while MessageBoxMenu is open

An unpaused MessageBox means the global TradeTogether input sink continues receiving keyboard events while the menu is on screen. v0.10.0 therefore ignores TradeTogether hotkeys whenever `MessageBoxMenu` is open.

This prevents keys intended for the MessageBox from also triggering background trade actions such as a new request, validation, cancellation, item editing or gold changes.

The MessageBox itself retains normal control of its buttons.

## Native instance-aware transfer

The STRPM branch uses the same automatic transfer code as the current main branch.

TradeTogether transfers the actual local Skyrim inventory stack to the loaded STR proxy of the other player. When a selected item has an `ExtraDataList`, the original list is supplied to Skyrim's native `RemoveItem` transfer path.

This is intended to preserve smithing improvements, enchantments, enchantment charge, custom names and other per-instance data stored by Skyrim. Gold uses Skyrim's native Gold form and follows the same native transfer path.

## STR Plugin Messaging transport

Channel:

```text
tradetogether.offer.v1
```

Trade packets keep the existing `TTNET|v1|...` payload format. STRPM messages are sent with **reliable + ordered** flags.

Incoming packets cache the sender name and `ConnectionID`, while Proxy Resolver events maintain the authoritative local proxy FormID mappings.

## Diagnostics

The log is located at:

```text
Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log
```

Expected startup entries include:

```text
TradeTogether v0.10.0-strpm loading
MessageBox compatibility: SkyrimSoulsRE=detected pauseState=delegated-to-MessageBoxMenu creator dataDefaults=native
TradeTogether STRPM deferred receive dispatcher started
TradeTogether STRPM proxy mapped: connection=... form=...
TradeTogether STRPM transport started: channel=tradetogether.offer.v1 ... mappedProxies=1 deferredReceive=1
TradeTogether v0.10.0-strpm ready ... network=ready
```

When a MessageBox is created:

```text
SafeMessageBox queued with 2 button(s); native MessageBoxData defaults preserved
```

When an unpaused MessageBox is active and input events continue arriving:

```text
MessageBoxMenu opened; TradeTogether hotkeys suspended until it closes
```

After it closes:

```text
MessageBoxMenu closed; TradeTogether hotkeys resumed
```

A normal NPC or unavailable proxy instead produces:

```text
TradeTogether STRPM target is not a mapped player proxy: name="..." mappedProxies=...
```

## Release-build safeguards

Starting with **v0.9.3-strpm**, `build_release.bat` forces a clean target rebuild, deletes the previously packaged DLL, explicitly copies the fresh DLL into `package/Data/SKSE/Plugins`, and verifies its version marker.

Starting with **v0.9.4-strpm**, archive creation explicitly verifies that the ZIP exists after `Compress-Archive`.

Starting with **v0.9.5-strpm**, the STRPM archive contains **only `package/Data`**, preventing stale UDP/FOMOD folders from entering the archive.

## Current status

`v0.10.0-strpm` is validated in a complete two-client STR trade flow, including the initial request MessageBox, offer composition, Ready / Modify summary, final Confirm / Modify prompt, and automatic item transfer. The Skyrim Souls RE compatibility fix was validated with `bMessageBoxMenu=true`, with the MessageBox remaining unpaused and no freeze observed.

## Build

```powershell
.\build_release.bat
```

Expected archive:

```text
dist/TradeTogether-v0.10.0-strpm-Vortex.zip
```

The archive root must contain only:

```text
Data/
```
