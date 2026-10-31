# Networking

**Code:** `engine/net/` (sockets, channels), `engine/shared/protocol.*` (messages, deltas),
`engine/server/server.*`, `engine/client/client.*`

Client/server everywhere, like Source: single-player is a local server with a loopback
client, and every `map` game is also a listen server others can join.

## Playing

```sh
./k +map devroom                           # host (listens on UDP 27015; sv_listen 0 to disable)
./k +connect 192.168.1.20[:port] [password]
./k -dedicated +hostport 27015 +map devroom +sv_password secret +sv_maxplayers 8
```

`status` lists players with ping and loss. `net_graph 1` shows ping, loss, bandwidth,
snapshots per second and prediction error. Simulate bad networks with `net_fakelag <ms>` and
`net_fakeloss <percent>` (cheats).

## Net channel (`net/udp_channel.*`)

Every packet: `sequence` (top bit = has reliable data), `ack` of the peer's latest sequence,
`reliable ack`, then optionally the reliable block, then unreliable messages (`u16 length` + bytes).

- **Unreliable** messages ride in one packet and are lost with it. Out-of-order and duplicate packets are dropped.
- **Reliable** messages are grouped into one block that's resent in every packet until the
  peer acks it; only one block is in flight at a time, so they arrive once and in order.
- Ping comes from acks; loss from sequence gaps; a channel times out after 10 s of silence.
- Packets are capped at 1400 bytes.

The loopback channel (`net/channel.h`) has the same interface and never drops anything.

## Connecting

Connectionless packets (sequence `0xFFFFFFFF` + text) handle the handshake:
client → `connect <protocol> "<name>" ["password"]` (resent every 0.5 s for up to 10 s);
server → `accept <slot>` or `reject "<reason>"`. Then the server sends `ServerInfo` (map name,
model table, replicated cvars, your slot) reliably and the client loads the map.

## Snapshots

Every tick the server sends each client its own player state plus the entities it can see,
**delta-encoded** against the newest snapshot that client acknowledged (each `Move` carries
the last tick it received). Per entity, only changed fields are sent (model, origin, angles,
effects); new entities are sent in full and removed ones are flagged. An unchanged world costs
2 bytes. If the client doesn't have the baseline, it ignores the snapshot and the server falls
back to an older ack.

## Client prediction

The client keeps every command it has sent but the server hasn't acknowledged. On each
snapshot: start from the server's player state and re-run those commands through the same
`playerMove`, colliding with the map and the brush entities in the snapshot. Your movement
responds instantly at any ping. When the server disagrees with what was predicted, the
difference becomes a view offset that fades over `cl_smoothtime` (teleports snap instead).
`cl_predict 0` turns it off; `cl_showerror 1` logs corrections.

## Interpolation

Other entities are drawn `cl_interp` seconds (default 0.1) in the past, blended between the
two snapshots around that time, so they move smoothly despite jitter and lost packets. The
client clock is slewed toward the server's tick time.

## Lag compensation

The server records every player's position each tick for one second. Each command carries
the tick the client was rendering others at (`UserCmd::tickCount`). A hitscan shot rewinds
the other players to that tick before testing hits, so you hit what you saw.
`sv_lagcompensation 0` turns it off.

## Messages

Client → server: `Connect` (loopback), `Move` (ack + last 3 commands, so one lost packet
doesn't lose input), `StringCmd` (`noclip`, `kill`, `setpos`, `ent_fire`), `Disconnect`.
Server → client: `ServerInfo`, `Snapshot`, `Print`, `CenterPrint`, `SetAngle`, `Sound`,
`SetCvar`, `TempEntity`, `KillFeed`, `HitConfirm`, `Disconnect`. Bump
`proto::kProtocolVersion` whenever a message changes.
