# Entities and entity I/O

**Code:** `game/` (the `k_game` library). The engine talks to it only through
`game/game_server.h` (`IGameServer`).

## How an entity works

```cpp
class MyEntity : public BaseEntity {
public:
    Output onSomething;
    MyEntity() { declareOutput("OnSomething", &onSomething); }

    bool keyValue(const std::string& key, const std::string& value) override; // map keys
    void spawn() override;      // after all keys are set
    void activate() override;   // after every map entity has spawned
    void think() override;      // when setNextThink(time) comes due
    void startTouch(BaseEntity* other) override; // triggers
    void use(BaseEntity* activator) override;    // +use
    bool acceptInput(const std::string& input, BaseEntity* activator, BaseEntity* caller,
                     const std::string& param) override;
};
LINK_ENTITY_TO_CLASS(my_entity, MyEntity)
```

Return `true` from `keyValue`/`acceptInput` when you handle something, and call the base
class otherwise (it handles `targetname`, `origin`, `angles`, `model`, outputs and `Kill`).

> **New entity file?** Add a `void linkEntities_<file>() {}` function at the end and call it
> from the `GameWorld` constructor. Without that reference the linker drops the file from the
> static library and its `LINK_ENTITY_TO_CLASS` never runs.

Useful state: `origin`, `angles`, `brushModel` (> 0 for brush entities), `modelPath`, `solid`
(`Not`, `Bsp` blocks movement, `Trigger` reports touches), `effects` (`EF_NODRAW`),
`networked` (sent to clients), `physicsBody`. Through `world()`: `time()`, `findByName`,
`players()`, `emitSound`, `tryPush`, `damagePlayer`, `physics()`.

## Entity I/O

Outputs connect to inputs on other entities, written as map keys:

```
"OnPressed"   "door1,Open,,0,-1"
"OnPressed#2" "hud,ShowHudMessage,Door opening,0.5,1"
```

Fields: target name, input, parameter, delay (seconds), times to fire (-1 = forever). Use
`#2`, `#3`... for more connections on one output (TrenchBroom keys must be unique). Source's
ESC separator also works.

Targets: a name, a wildcard (`door*`), `!activator` (who started the chain), `!caller` /
`!self` (the entity firing), `!player`.

Debug: `developer_io 1` logs every event; `ent_fire <target> <input> [param]` sends one (cheat).

## Entity reference

### Brush entities

| Class | Keys | Inputs | Outputs |
|---|---|---|---|
| `func_door` | `movedir` (pitch yaw roll; `-90 0 0` = up), `speed` 100, `wait` 4 (-1 stays open), `lip`, `locked`, `spawnpos`, `noise1`/`noise2` sounds, spawnflag 256 = +use opens | Open, Close, Toggle, Lock, Unlock, SetSpeed | OnOpen, OnClose, OnFullyOpen, OnFullyClosed, OnLockedUse, OnBlocked |
| `func_button` | `movedir`, `wait` 1, `locked`, `sound` | Press, Lock, Unlock | OnPressed, OnIn, OnOut, OnUseLocked |
| `trigger_multiple` | `wait` (between OnTrigger), `StartDisabled` | Enable, Disable, Toggle | OnStartTouch, OnEndTouch, OnTrigger |
| `trigger_once` | | Enable, Disable | OnTrigger (then removed) |
| `trigger_teleport` | `target` (an `info_teleport_destination`) | Enable, Disable | |
| `trigger_hurt` | `damage` per second | Enable, Disable | |
| `func_brush` / `func_wall` | `StartDisabled`, `Solidity` (0 when visible, 1 never, 2 always) | Enable, Disable, Toggle | |
| `func_illusionary` | | Enable, Disable | |
| `func_detail` | Compile-time only: merged into the world | | |

`func_door` covers doors, platforms and elevators. A negative `lip` travels further than the
brush's own size (the devroom lift uses `-192`). Movers carry players standing on them and
stop if a player blocks them.

### Point entities

| Class | Keys | Inputs | Outputs |
|---|---|---|---|
| `logic_relay` | `StartDisabled`, spawnflag 1 = once | Trigger, Enable, Disable, Toggle | OnTrigger, OnSpawn |
| `logic_timer` | `RefireTime`, `StartDisabled` | Enable, Disable, Toggle, FireTimer, RefireTime | OnTimer |
| `logic_auto` | | | OnMapSpawn |
| `math_counter` | `startvalue`, `min`, `max` | Add, Subtract, SetValue | OnHitMax, OnHitMin, OutValue |
| `point_message` / `game_text` | `message` | ShowMessage (console), ShowHudMessage (screen) | |
| `ambient_generic` | `message` (sound script), `health` volume 0-10, spawnflag 16 = start silent | PlaySound, StopSound, ToggleSound | |
| `prop_physics` | `model`, `massScale` | Wake, ApplyImpulse | |
| `prop_dynamic` | `model` | TurnOn, TurnOff | |
| `prop_static` | `model`, `angles`, `modelscale` (drawn straight from the map) | | |
| `info_player_start`, `info_target`, `info_teleport_destination` | `angles` | | |
| `light`, `light_environment` | See the [map compiler](map-compiler.md) | | |

Every entity accepts `Kill`. The TrenchBroom FGD (`tools/trenchbroom/k.fgd`) mirrors this table.

## Tick order (server)

For each client command: movement → prop pushing → footstep sounds → fall damage → +use / fire
→ held prop → triggers. Then each tick: physics (kinematic movers follow entities, simulate,
props follow bodies) → think functions → I/O event queue → delete killed entities → record
lag-compensation history.
