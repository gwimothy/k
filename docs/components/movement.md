# Player movement and collision

**Code:** `engine/shared/gamemovement.*`, collision in `engine/shared/bsp_trace.*`

Modelled on Source's `CGameMovement`. It's a pure function, so the server and the client's
prediction run exactly the same code:

```cpp
void playerMove(proto::PlayerState& state, const UserCmd& cmd, float dt,
                const MoveParams& params, const TraceWorld* world);
```

`TraceWorld` is what movement collides with. `BspTraceWorld` is just the map; the game's
`GameWorld` adds doors, lifts, props and other players; the client's prediction adds the
brush entities from the latest snapshot.

## What it does each command

1. Crouch/stand (`IN_DUCK`). In the air, crouching pulls the legs up. Standing up needs room.
2. Ground check: a short trace down; ground must be flatter than ~45° (normal z ≥ 0.7).
3. Jump on a fresh press (`IN_JUMP`; hold-to-rejump with `sv_autobunnyhopping 1`).
4. On the ground: friction, acceleration, then try to move; if blocked, also try **stepping up**
   (`sv_stepsize`, 18) and keep whichever went further; then stick to the ground (stairs, slopes).
5. In the air: half gravity, air acceleration, slide, half gravity.

Air acceleration caps the speed *added* along your wish direction at 30 u/s, not your total
speed. Turning while strafing keeps adding speed: that's air strafing, the same as Source.

## Numbers (Source defaults)

| | |
|---|---|
| Hull | 32×32×72 standing, 32×32×36 crouched; origin at the feet |
| Eye height | 64 standing, 28 crouched |
| Max speed | `sv_maxspeed` 320; ×⅓ crouched, ×0.52 walking (`+walk`) |
| Jump | √(2 × 800 × 57) ≈ 302 u/s: 57 units high |
| Accel / air accel | `sv_accelerate` 10 / `sv_airaccelerate` 10 |
| Friction | `sv_friction` 4, `sv_stopspeed` 100 |
| Gravity | `sv_gravity` 800 |

These cvars are replicated, so remote clients predict with the server's values.

## Noclip

`noclip` (needs `sv_cheats 1`) flies through walls. Space/ctrl move up/down, shift doubles speed;
`sv_noclipspeed` scales it.

## Tests

`tests/test_movement.cpp` compiles a small room and checks landing, max speed, walls,
sliding, jump height, a 16-unit step vs a 64-unit block, crouching, air strafing and noclip.
