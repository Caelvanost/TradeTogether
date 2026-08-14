# TradeTogether v0.8.1-strpm5 branch

Synchronized trade-offer interface for Skyrim Together Reborn.

## Vortex installation

The Vortex archive uses a FOMOD with two profiles. On this branch, both
profiles use `Transport=STRPlugin`, so TradeTogether no longer requires port
forwarding when `STRPluginMessagingAPI.dll` provides a real bridge through the
Skyrim Together Reborn connection.

- **Client / Local**: recommended for remote clients, LAN play, or an
  installation without an Internet relay.
- **Host**: reserved for the host machine in case a role distinction is needed
  later. In pure STRPlugin mode, it no longer requires port forwarding.

For remote play without port forwarding, also install
`STRPluginMessagingAPI.dll` and select **Client / Local** on client machines.
The channel used is `chaos.trade_together.offer.v1`.

If the STRPM plugin is not installed, `Transport=STRPlugin` refuses to start
the TradeTogether network instead of silently falling back to UDP. If STRPM is
installed but its STR bridge is not active yet, the interface can still
register and sends may return `not connected`, with diagnostic status written
to `TradeTogether.log`. To test the legacy behavior, use `Transport=Auto` or
`Transport=UDP`.

## Trade flow

1. Both players install the same version of TradeTogether and connect to
   Skyrim Together Reborn.
2. The requester targets the other character and presses **F6**.
3. The targeted player receives an **Accept / Refuse** prompt.
4. After acceptance, the personal inventory opens for both players in offer
   composition mode.
5. Each player selects the items they want to offer:

   - **E**: add one unit of the selected item;
   - **Delete**: remove one unit;
   - **F6**: display both offers and mark yourself as ready;
   - **Tab**: cancel the entire trade.

   TradeTogether does not use F8, F9, or F10 to avoid conflicts with Heart of
   Magic, quick load, and OStim Together.

6. Any offer change automatically clears the ready state.
7. When both players are ready, each player sees both baskets and must confirm
   one final time.
8. After both players confirm, the target's native inventory opens for the
   requester so the validated transfer can be completed manually.

Quest items are rejected. An offer can contain up to 24 lines and may be empty,
allowing one-way gifts. The initial request expires after 30 seconds, and an
inactive session expires after 5 minutes.

## STR Plugin Messaging transport

This branch follows the newer STRPM v5 workspace implementation.
TradeTogether:

- loads `STRPluginMessagingAPI.dll`;
- registers the `chaos.trade_together.offer.v1` channel;
- sends trade payloads as reliable/ordered messages;
- keeps the current `TTNET|v1|...` format to reduce regression risk;
- reads STRPM v2 diagnostics (`RuntimeStatus`);
- rejects STRPM if it is active on its development UDP backend;
- creates no TradeTogether UDP socket when `Transport=STRPlugin`.

The newer STRPM runtime is designed as two layers:

- `STR_QueryPluginMessagingInterface` for SKSE mods such as TradeTogether;
- `STRPM_QueryTransportInterface` for the private STR-side bridge.

The default packaged STRPM build does not open a UDP socket. If the bridge at
`Data\SkyrimTogetherReborn\STRPluginMessagingBridge.dll` is missing or has not
started yet, STRPM keeps its active backend set to `None`, and sends may return
`not connected`.

## Automatic remote connection through STR using legacy UDP

The historical UDP transport remains available with `Transport=UDP` or
`Transport=Auto`. In this mode, TradeTogether uses its own UDP channel on port
**27993**. It does not send data through the STR port and does not modify the
STR protocol.

Version 0.7.1 used the MorphSyncTogether approach: when an STR client connected
directly to a server, STR stored the last address used in its Chromium storage:

```text
Data\SkyrimTogetherReborn\cache\Default\Local Storage\leveldb
```

TradeTogether reads this address, strips the STR port if present, then contacts
the same host using `AutoRemotePort`, which defaults to **27993**.

Example:

```text
STR direct connect : 82.65.51.103:10578
TradeTogether      : 82.65.51.103:27993
```

Default client configuration:

```ini
[Network]
Disabled=0
AutoDiscovery=1
RelayMode=0
LocalPort=27993
AutoRemoteFromSTR=1
AutoRemotePort=27993
AutoSharedSecretFromSTR=0
RemotePeers=
SharedSecret=
```

In practice, the remote client first connects to the STR server. TradeTogether
then automatically retrieves the STR host at startup or during subsequent
periodic attempts. If STR has not stored a usable address yet, `RemotePeers`
remains available as a manual fallback.

## Legacy UDP Internet setup: relay

Choose the machine hosting the Skyrim Together session, or whichever machine
has the easiest router access. Open/forward **UDP port 27993** to this PC and
allow Skyrim through the Windows Firewall.

Relay machine:

```ini
[Network]
Transport=UDP
Disabled=0
AutoDiscovery=0
RelayMode=1
LocalPort=27993
AutoRemoteFromSTR=0
AutoRemotePort=27993
AutoSharedSecretFromSTR=0
RemotePeers=
SharedSecret=replace-with-the-same-private-value-everywhere
```

The legacy FOMOD automatically installed this profile when **Host** was
selected. The `TradeTogether_RelayHost.ini` file remains in the repository as a
manual template if you prefer using the separate override.

Each remote client can remain on the automatic configuration:

```ini
[Network]
Transport=UDP
Disabled=0
AutoDiscovery=1
RelayMode=0
LocalPort=27993
AutoRemoteFromSTR=1
AutoRemotePort=27993
RemotePeers=
SharedSecret=replace-with-the-same-private-value-everywhere
```

Clients periodically send discovery packets to the relay. This opens their
outbound NAT mapping, so they normally do not need their own port forwarding.
The relay learns their observed public addresses and forwards trade packets to
other active peers. Relayed packets are marked to prevent forwarding loops.

## Shared secret

`SharedSecret` enables HMAC-SHA256 signing for discovery and trade packets. The
value must be identical for every player and is never transmitted over the
network.

Advanced option, following MorphSyncTogether: if `AutoSharedSecretFromSTR=1`
and `SharedSecret` is empty, TradeTogether attempts to reuse the STR password:

- in relay mode, it reads `sPassword` from
  `Data\SkyrimTogetherReborn\config\STServer.ini`;
- in client mode, it reads the direct-connect password saved by STR.

If no STR password can be found, configure `SharedSecret` manually.

## Direct Internet peers

Without a relay, leave `RelayMode=0` and list all other public endpoints. Every
listed player must expose their own UDP port:

```ini
RemotePeers=player-2.example:27993,203.0.113.8:27994
```

`RemotePeers` accepts IPv4 addresses and DNS names separated by commas or
semicolons. The legacy `PeerHost` / `PeerPort` pair is still supported and is
added to `RemotePeers`.

## About the STR server

You can reuse the STR machine's address, but not the STR port as the
TradeTogether channel. The STR server machine can also host the TradeTogether
relay if UDP port 27993 is exposed, but STR does not automatically relay
TradeTogether packets.

## Current limitation

The interface synchronizes and locks both players' intent, but it does not move
items automatically. Skyrim Together Reborn does not expose a stable public API
that guarantees transfer of enchanted, tempered, or renamed item instances.
The final transfer therefore remains manual through the native inventory
interface already validated with STR.

Players are associated by their character name. As in previous versions, the
initial target may be any `Actor` other than the local player because STR does
not expose a stable public SKSE API for formally identifying its remote actors.

The log is located at:
`Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log`.

## Build

```powershell
.\build_release.bat
```

The script builds the DLL, prepares the Host / Client Local FOMOD profiles, and
creates:

```text
dist/TradeTogether-v0.8.1-strpm5-Vortex.zip
```
