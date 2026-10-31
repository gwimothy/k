# Gameplay: health, combat, HUD and menu

**Code:** health and combat in `game/game_world.cpp`; HUD and menu in `engine/host/host.cpp`

## Health and damage

Players have 100 health. `GameWorld::damagePlayer(victim, amount, attacker, how)` is the one
place damage happens. At 0 the player dies: no movement, hidden from others, can't be hit
or block. They respawn after `sv_respawntime` (3 s) at the spawn point with full health.

| Source of damage | |
|---|---|
| Hitscan (`+attack` when not holding a prop) | `sv_hitscan_damage` (25), lag compensated |
| Falling | Source's curve: none below 526.5 u/s, lethal at 922.5 u/s (`sv_falldamage`) |
| `trigger_hurt` | `damage` per second, applied every half second |
| `kill` | Suicide |

Deaths go to everyone's kill feed.

## HUD

Drawn with ImGui over the scene (`hud_draw 0` hides it): health (bottom left, green to red),
crosshair (`crosshair`), a hit marker when your shot connects, a red flash when you take
damage, the kill feed (top right), center messages (`point_message` → ShowHudMessage) and the
death screen.

## Menu

Escape opens it (in game it pauses input; the console has moved to `` ` ``). Pages:
Resume, New game (lists compiled maps), Join game (address), Options (name, sensitivity,
FOV, volume, vsync, FPS counter, net graph), Disconnect, Quit. It also shows when you're not
in a game. `menu` toggles it from the console.
