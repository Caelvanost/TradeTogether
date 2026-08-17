# TradeTogether v0.8.1

Remote UDP edition of TradeTogether for Skyrim Together Reborn, with automatic instance-aware item transfer and automatic client discovery of the STR server address.

## Trade flow

1. Both players install the same TradeTogether version and connect through Skyrim Together Reborn.
2. The requester targets the other player and presses **T**.
3. The other player chooses **Accept / Refuse**.
4. Both players compose their offers in their own inventory:
   - **Insert**: add one unit;
   - **Delete**: remove one unit;
   - **T**: review the offers and mark ready;
   - **Tab**: cancel.
5. Both players receive one final **Confirm / Modify** prompt.
6. After both confirmations, TradeTogether transfers the items automatically.

There is no direct-inventory step and no additional user confirmation. TradeTogether deliberately does not intercept **E** or **A**.

## Instance-aware transfer

TradeTogether transfers the actual local Skyrim inventory instance through the native container transfer path. When an item has an `ExtraDataList`, that exact list is passed to `RemoveItem` with the other player's STR proxy as the destination.

This is intended to preserve smithing/tempering improvements, enchantments and charge, custom item names, and other per-instance extra data. Standard stackable items are transferred normally as base stacks.

The functional core was validated in game with both an improved forged dagger and a standard amethyst before being integrated into this remote branch.

## Remote UDP mode

TradeTogether uses **UDP port 27993** independently of Skyrim Together Reborn.

### Remote Host

Choose **Remote Host** in the FOMOD.

The host configuration uses:

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

The client profile enables:

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

`PeerHost` remains available only as a manual fallback. If automatic STR detection does not work on a particular STR installation, set `PeerHost` to the reachable host address and keep `PeerPort=27993`.

The client normally does **not** need a router port-forward rule.

## LAN mode

For LAN use, the regular `main` branch remains simpler and uses automatic broadcast discovery. The `udp` branch is intended for remote Internet play.

## Current limitations

- Both players must use matching TradeTogether builds.
- The target player must be represented by a loaded/high-process STR actor proxy for the native item transfer.
- If two modified instances have the same base FormID and exactly the same display name, Skyrim may not expose enough information to distinguish them perfectly.
- The host still needs reachable UDP 27993. CGNAT or restrictive firewalls can prevent conventional port forwarding.
- Automatic client discovery depends on STR continuing to store `last_connected_address` in its Chromium localStorage cache. `PeerHost` remains the fallback if this changes.

## Log

```text
Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log
```

Useful network entries include `automatic STR remote configured`, discovered peer addresses, sent packet counts, and native instance-transfer entries.

## Build

```powershell
.\build_release.bat
```

Expected archive:

```text
dist/TradeTogether-v0.8.1-Vortex.zip
```

The archive contains the FOMOD installer with **Remote Client** and **Remote Host** profiles.
