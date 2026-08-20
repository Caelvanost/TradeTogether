# TradeTogether v0.9.7-strpm

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

## Connected-player target validation

Starting with **v0.9.6-strpm**, TradeTogether uses STRPM's **Proxy Resolver** instead of trying to target players by display name alone.

STRPM reports mappings between each connected player's `ConnectionID` and the local Skyrim FormID of that player's STR proxy. TradeTogether caches these mappings and resolves the displayed proxy name only from those mapped FormIDs.

This provides two important guarantees:

- normal NPCs are not valid TradeTogether targets because their FormIDs are not registered as STR player proxies;
- real STR proxies are sent through `TargetKind::kPlayer` with the non-zero `ConnectionID` required by the STR bridge.

## Safe message-box dispatch

Starting with **v0.9.7-strpm**, TradeTogether no longer creates or queues Skyrim `MessageBoxData` directly from the gameplay task that processes incoming STRPM packets.

All TradeTogether message boxes are now scheduled through `SKSE::TaskInterface::AddUITask`. Their Accept / Decline / Confirm callbacks are then dispatched back through the normal SKSE gameplay task queue before modifying TradeTogether state or sending network packets.

This is intended to prevent the receiver freeze observed immediately after an incoming trade request reached `SafeMessageBox::QueueMessage()`.

Additional diagnostics now distinguish the stages:

```text
SafeMessageBox scheduled on UI task with 2 button(s)
SafeMessageBox UI task started
SafeMessageBox queueing on UI thread with 2 button(s)
SafeMessageBox UI queue completed
SafeMessageBox dispatching callback to gameplay task
```

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
TradeTogether v0.9.7-strpm loading
TradeTogether STRPM proxy mapped: connection=... form=...
TradeTogether STRPM transport started: channel=tradetogether.offer.v1 ... mappedProxies=1
TradeTogether STRPM status startup: backend=STRBridge ... mappedProxies=1
TradeTogether v0.9.7-strpm ready ... network=ready
```

When a targeted proxy is resolved successfully:

```text
TradeTogether STRPM resolved player proxy: name="..." connection=... form=...
TradeTogether STRPM packet sent: target="..." connection=... bytes=...
```

A normal NPC or an unavailable proxy instead produces:

```text
TradeTogether STRPM target is not a mapped player proxy: name="..." mappedProxies=...
```

If the Proxy Resolver is missing, TradeTogether refuses to start its network layer and reports:

```text
TradeTogether STRPM proxy resolver unavailable: install a current STRPluginMessagingAPI build
```

## Release-build safeguards

Starting with **v0.9.3-strpm**, `build_release.bat` forces a clean TradeTogether target rebuild, deletes any previously packaged DLL, explicitly copies the freshly built DLL into `package/Data/SKSE/Plugins`, and validates that the DLL contains the expected STRPM version marker before an archive can be created.

Starting with **v0.9.4-strpm**, archive creation explicitly verifies that the ZIP exists after `Compress-Archive`.

Starting with **v0.9.5-strpm**, the STRPM archive contains **only `package/Data`**, so stale UDP/FOMOD directories left by another branch cannot enter the archive.

## Current status

`v0.9.7-strpm` is an early STRPM test build. It keeps the Proxy Resolver targeting from v0.9.6 and routes message-box creation through the SKSE UI task queue to address receiver freezes.

## Build

```powershell
.\build_release.bat
```

Expected archive:

```text
dist/TradeTogether-v0.9.7-strpm-Vortex.zip
```

The archive root must contain only:

```text
Data/
```
