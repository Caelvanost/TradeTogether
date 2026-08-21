# TradeTogether v0.9.10-strpm

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
3. The target receives a non-modal notification: **T = Accept**, **Tab = Decline**.
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

## Connected-player target validation

Starting with **v0.9.6-strpm**, TradeTogether uses STRPM's **Proxy Resolver**.

STRPM reports mappings between each connected player's `ConnectionID` and the local Skyrim FormID of that player's STR proxy. TradeTogether caches these mappings and resolves the displayed proxy name only from those mapped FormIDs.

This means normal NPCs are rejected, while real STR proxies are sent through `TargetKind::kPlayer` with the non-zero `ConnectionID` required by the STR bridge.

## Deferred STRPM receive path

Starting with **v0.9.8-strpm**, TradeTogether no longer performs normal gameplay work directly from STRPM's receive callback.

The callback only copies the incoming message into a preallocated 32-slot ring buffer and returns. A dedicated dispatch thread performs parsing, logging and normal SKSE task scheduling afterwards.

Expected startup diagnostics include:

```text
TradeTogether STRPM deferred receive dispatcher started
TradeTogether STRPM transport started: ... deferredReceive=1
```

## Incoming request freeze workaround

Runtime tests with STRPM showed that the transport remains healthy after a TradeTogether request is delivered, while the receiving game freezes when TradeTogether queues the initial `MessageBoxData` Accept/Decline dialog.

**v0.9.10-strpm** therefore removes only that initial network request from Skyrim's modal message-box pipeline.

Incoming requests now use a non-modal notification:

```text
<Player> wants to trade with you. Accept and compose your offer? [T] Accept | [Tab] Decline
```

The input sink consumes T/Tab before the normal trade hotkeys while this request is pending and invokes the existing `TradePromptCallback` directly. The request timeout and normal `RESPONSE` protocol remain unchanged.

Offer-summary and final-confirmation dialogs are intentionally unchanged in this build so the initial receiver freeze can be isolated without redesigning the complete trade UI.

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
TradeTogether v0.9.10-strpm loading
TradeTogether STRPM deferred receive dispatcher started
TradeTogether STRPM proxy mapped: connection=... form=...
TradeTogether STRPM transport started: channel=tradetogether.offer.v1 ... mappedProxies=1 deferredReceive=1
TradeTogether STRPM status startup: backend=STRBridge ... mappedProxies=1
TradeTogether v0.9.10-strpm ready ... network=ready
```

When a targeted proxy is resolved successfully:

```text
TradeTogether STRPM resolved player proxy: name="..." connection=... form=...
TradeTogether STRPM packet sent: target="..." connection=... bytes=...
```

On the receiver, the initial request should now log:

```text
SafeMessageBox incoming trade prompt displayed non-modally: T=accept Tab=decline
Trade confirmation displayed: ...
```

Accepting or declining should then log:

```text
SafeMessageBox non-modal incoming trade response: accept
```

or:

```text
SafeMessageBox non-modal incoming trade response: decline
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

`v0.9.10-strpm` is an STRPM test build focused on isolating the receiver freeze. Proxy Resolver targeting and deferred STRPM reception remain unchanged; only the initial incoming Accept/Decline dialog is non-modal.

## Build

```powershell
.\build_release.bat
```

Expected archive:

```text
dist/TradeTogether-v0.9.10-strpm-Vortex.zip
```

The archive root must contain only:

```text
Data/
```
