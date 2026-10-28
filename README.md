# TradeTogether v0.11.1-strpm

STR Plugin Messaging edition of TradeTogether for Skyrim Together Reborn.

## Requirements

- Skyrim Special Edition / Anniversary Edition
- SKSE
- Skyrim Together Reborn
- STR Plugin Messaging API with Proxy Resolver support
- The same TradeTogether build on every player
- SkyUI for the optional MCM

TradeTogether itself opens no UDP socket and requires no TradeTogether-specific port forwarding.

## Trade flow

1. Both players connect through Skyrim Together Reborn.
2. Target the other player's character and press the configured **Trade / Validate** key (default: T).
3. The target receives an **Accept / Decline** MessageBox.
4. Both players compose their offer in their personal inventory.
5. Add or remove items and gold using the configured controls.
6. Press **Trade / Validate** to review the offers.
7. Both players receive the Ready / Modify and final Confirm / Modify prompts.
8. After both confirmations, TradeTogether performs the automatic native transfer.

Quest items are rejected. Item-only, gold-only, mixed item/gold trades and one-way gifts are supported.

## v0.11.1 — STR 1.8.4 transfer hotfix

STR 1.8.4 no longer reliably propagates inventory mutations made by moving items into the local remote-player proxy. v0.11.1 therefore stops using the STR proxy as the transfer destination.

After both players confirm, each client now applies its own committed half of the exchange locally:

- outgoing items/gold are removed from the local player;
- incoming items/gold are added directly to the local player;
- all incoming FormIDs are validated before any inventory mutation;
- STRPM remains responsible for synchronized offers and double confirmation.

This restores normal item and gold delivery without depending on proxy inventory side effects.

Important: the current wire offer only carries FormID, display name and quantity. The local-receipt path therefore recreates incoming items from their base FormID; per-instance ExtraData such as tempering, custom enchantments, charge or custom names is not yet serialized by this hotfix. Do not rely on v0.11.1 to preserve those per-instance modifications until explicit instance-state transport is added.

## v0.11.0 — configurable controls and SkyUI MCM

v0.11.0 moves all TradeTogether controls out of hard-coded input constants.

The following actions are configurable:

- Trade / Validate
- Add selected item
- Remove selected item
- Add gold
- Remove gold
- Cancel trade

Defaults remain:

```text
Trade / Validate     T
Add selected item    Insert
Remove selected item Delete
Add gold             Numpad +
Remove gold          Numpad -
Cancel trade         Tab
```

Users without a numpad can map **Add Gold** and **Remove Gold** to normal keyboard keys.

The current Shift/Ctrl amount modifiers are preserved:

```text
Shift + Gold key = x10
Ctrl + Gold key  = x100
```

### INI storage

Bindings are persisted in:

```text
Data\SKSE\Plugins\TradeTogether.ini
```

```ini
[Controls]
TradeKey=20
AddItemKey=210
RemoveItemKey=211
GoldAddKey=78
GoldRemoveKey=74
CancelKey=15
```

The values are DirectInput / SkyUI keymap scan codes.

### Runtime MCM bridge

The SKSE plugin exports a small Papyrus API through `TradeTogetherNative`.

When a key is changed in the MCM:

1. the new scan code is written to `TradeTogether.ini`;
2. `InputEventSink` reloads the bindings immediately;
3. no Skyrim restart or save reload is required.

A **Reset all key bindings** MCM action restores the original defaults.

MCM Helper is not required. The menu uses SkyUI's normal `SKI_ConfigBase` API.

Papyrus sources:

```text
Source\Scripts\TradeTogetherNative.psc
Source\Scripts\TradeTogetherMCM.psc
```

Build/setup documentation:

```text
docs\MCM_SETUP.md
```

## Numpad compatibility

The historical numpad + / - bindings need a special Win32 input path because SkyUI's InventoryMenu does not consistently expose those keys as normal `ButtonEvent`s.

v0.11.0 keeps that proven fallback only when the gold actions are still mapped to their default numpad keys.

If the user selects any other key in the MCM, TradeTogether uses the normal Skyrim/SKSE keyboard event stream instead.

## Connected-player target validation

TradeTogether uses STRPM's Proxy Resolver to map connected STR `ConnectionID`s to local Skyrim proxy FormIDs. Normal NPCs are rejected, while real STR proxies are sent through `TargetKind::kPlayer`.

## Deferred STRPM receive path

Incoming STRPM callbacks only copy messages into a preallocated queue. Parsing, logging and gameplay work happen afterwards outside the transport callback.

## Skyrim Souls RE compatible MessageBoxes

TradeTogether preserves native `MessageBoxData` defaults and does not force `MessageBoxMenu` pause flags.

With Skyrim Souls RE, this configuration remains supported:

```ini
[UNPAUSED_MENUS]
bMessageBoxMenu = true
```

TradeTogether hotkeys are suspended while `MessageBoxMenu` is open so an input intended for an unpaused MessageBox cannot also trigger a trade action in the background.

The complete v0.10.0 trade flow was validated with Skyrim Souls RE and unpaused MessageBoxes.

## Transfer model

TradeTogether v0.11.1 uses local authoritative inventory mutation on both clients. This avoids the STR 1.8.4 regression where moving an item into the remote player proxy removes it from the giver but does not deliver it to the actual remote player.

Outgoing local stacks are still selected precisely for removal, but incoming items are currently recreated from their base FormID. Per-instance ExtraData preservation is therefore temporarily unsupported by the v0.11.1 hotfix.

## STR Plugin Messaging transport

Channel:

```text
tradetogether.offer.v1
```

Messages use reliable + ordered delivery.

## Diagnostics

Log:

```text
Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log
```

Expected v0.11.1 startup entries include:

```text
TradeTogether v0.11.1-strpm loading
TradeTogether Papyrus MCM bridge registered
TradeTogether controls loaded: trade=... add=... remove=... goldAdd=... goldRemove=... cancel=...
TradeTogether STRPM deferred receive dispatcher started
TradeTogether v0.11.1-strpm ready ... network=ready
```

Changing a key through the MCM should log:

```text
MCM key mapping updated: action=GoldAdd key=0x...
TradeTogether controls loaded: ...
```

## Current status

`v0.11.1-strpm` is the STR 1.8.4 transfer hotfix. It keeps the v0.11.0 configurable controls/MCM and replaces proxy-targeted inventory transfer with local authoritative debit/receipt on each client.

The existing v0.10.0 STR trade flow and Skyrim Souls RE compatibility remain the validated gameplay baseline carried forward by this release.

## Build

C++ plugin:

```powershell
.\build_release.bat
```

MCM scripts:

```powershell
set "SKYUI_SOURCE=C:\path\to\SkyUI\scripts\source"
.\build_mcm.bat
```

Expected archive name:

```text
dist/TradeTogether-0.11.1.zip
```
