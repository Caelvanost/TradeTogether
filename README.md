# TradeTogether v0.8.0

Remote UDP edition of TradeTogether for Skyrim Together Reborn, with the same automatic instance-aware trade flow validated on the main branch.

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
6. After both confirmations, TradeTogether performs the transfer automatically. There is no direct-inventory step and no additional user confirmation.

TradeTogether deliberately does not intercept **E** (normal inventory action) or **A** (favorites).

## Instance-aware transfer

TradeTogether transfers the actual local Skyrim inventory instance through the native container transfer path. When an item has an `ExtraDataList`, that exact list is passed to `RemoveItem` with the other player's STR proxy as the destination.

This is designed to preserve instance data such as:

- smithing / tempering improvements;
- enchantments and enchantment charge;
- custom item names;
- other per-instance extra data.

Standard stackable items are transferred normally as base stacks.

The v0.7.1 core was validated in game with both a forged/improved dagger and a standard amethyst before being integrated into this remote branch.

## Remote UDP mode

TradeTogether uses **UDP port 27993** independently of Skyrim Together Reborn.

### Remote Host

Choose **Remote Host** in the FOMOD.

The host configuration uses:

```ini
AutoDiscovery=0
LocalPort=27993
PeerHost=
PeerPort=27993
```

On the host network:

1. Forward **UDP 27993** on the router to the host PC's local IPv4 address.
2. Allow UDP 27993 through Windows Firewall for Skyrim / TradeTogether.
3. Leave `PeerHost` empty.

The host does not need to know the client's public IP. When the first gameplay packet arrives, TradeTogether records the client's real UDP source endpoint and uses it for subsequent replies.

### Remote Client

Choose **Remote Client** in the FOMOD.

After installation, edit:

```text
Data/SKSE/Plugins/TradeTogether.ini
```

and set:

```ini
AutoDiscovery=0
PeerHost=<HOST_PUBLIC_IPV4>
PeerPort=27993
```

Example:

```ini
PeerHost=203.0.113.42
PeerPort=27993
```

The client normally does **not** need a router port-forward rule. Its initial outbound UDP packet creates the NAT mapping; the host replies to the source endpoint it learned from that packet.

`PeerHost` currently expects an IPv4 address, not a DNS hostname.

## LAN mode

For LAN use, the regular `main` branch remains simpler. Its default configuration uses automatic broadcast discovery. The `udp` branch is intended for remote Internet testing.

## Current limitations

- Both players must use matching TradeTogether builds.
- The target player must be represented by a loaded/high-process STR actor proxy so the native item transfer has a destination.
- If two modified instances have the same base FormID and exactly the same display name, Skyrim's inventory representation may not expose enough information to distinguish them perfectly.
- Remote connectivity can be blocked by CGNAT or restrictive firewalls even when the local configuration is correct. In that case a conventional router port forward may not be possible without a public IPv4 address or VPN/tunnel solution.

## Log

```text
Documents/My Games/Skyrim Special Edition/SKSE/TradeTogether.log
```

Useful remote-network log entries include the UDP startup line, learned peer addresses, sent packet counts and native instance-transfer entries.

## Build

```powershell
.\build_release.bat
```

Expected archive:

```text
dist/TradeTogether-v0.8.0-Vortex.zip
```

The archive contains the FOMOD installer with **Remote Client** and **Remote Host** profiles.
