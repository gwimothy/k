# Console, cvars and commands

**Code:** `engine/core/cvar.*`, `engine/core/cmdbuffer.*`, `engine/ui/console_ui.*`
**Reference:** [every cvar and command](../reference/cvars.md)

Almost everything in the engine is controlled through the console, the same way as Source:
settings are **cvars** (console variables), actions are **commands**, and text from the
console, config files, key bindings and the command line all runs through one **command
buffer**.

## Using it

- Open with `` ` `` (backtick). Tab completes; up/down walks history; suggestions show as you type.
- `name value` sets a cvar; `name` alone prints its value, default and help.
- `find <text>` searches names and help, `help <name>` shows one, `cvarlist [prefix]` lists them.
- Statements are separated by `;` or newlines; `"quotes"` group words; `//` starts a comment.

## Config files (`base/cfg/`)

Run on startup in this order:

| File | Purpose |
|---|---|
| `default.cfg` | Shipped default key bindings |
| `config.cfg` | Written on exit: your bindings and every `archive` cvar. Don't edit by hand |
| `autoexec.cfg` | Yours, never overwritten (copy `autoexec.cfg.example`) |
| `+commands` | From the command line, e.g. `./k +sv_cheats 1 +map devroom` |

`exec <file>` runs a config at any time.

## Built-in commands

| Command | Does |
|---|---|
| `echo <text>` | Print |
| `alias <name> "<commands>"` | Define a new command |
| `wait [n]` | Pause the buffer for n frames (default 1). Used heavily by scripted tests |
| `toggle <cvar> [values...]` | Flip 0/1 or cycle through values |
| `incrementvar <cvar> <min> <max> <step>` | Step with wrap-around |
| `exec <file>` | Run `cfg/<file>` |

## Adding a cvar or command (C++)

Declare it at file scope; it registers itself before `main` runs:

```cpp
#include "core/cvar.h"

static k::ConVar sv_example("sv_example", "10", k::FCVAR_ARCHIVE | k::FCVAR_NOTIFY,
                            "What it does.", /*min*/ 0.0f, /*max*/ 100.0f);

static k::ConCommand cmd_hello("hello", [](const k::CmdArgs& args) {
    k::log::info("hello {}", args.arg(1));
}, "Say hello.");
```

Read with `getFloat()`, `getInt()`, `getBool()`, `getString()`. Values are clamped to the
min/max. `setChangeCallback` runs code when the value changes.

| Flag | Meaning |
|---|---|
| `FCVAR_ARCHIVE` | Saved to `config.cfg` |
| `FCVAR_CHEAT` | Only changeable when `sv_cheats 1` (enforced through `console::setCheatGate`) |
| `FCVAR_REPLICATED` | The server's value is sent to clients on join and on change (movement settings, so prediction matches) |
| `FCVAR_NOTIFY` | Changes are announced in the console |

Lookups are case-insensitive. Names use a subsystem prefix: `sv_` server, `cl_` client,
`r_` renderer, `mat_` materials, `snd_` sound, `net_` network, `host_` main loop.

> **Linking gotcha.** Static registration only works if the object file gets linked. Put
> cvars in a file that's otherwise used, or add a link anchor like the entity files do
> (see [entities](entities.md)).

## How the buffer runs

`cmd::append` queues text; `cmd::insert` puts it at the front (that's how `exec` runs a file
before whatever came after it). The host calls `cmd::execute()` once per frame; it runs until
the queue is empty or a `wait` is hit. A statement resolves to a command, then a cvar, then an
alias. More than 8192 statements in one frame (an alias loop) clears the buffer with an error.

## Regenerating the reference

```sh
cd build/bin && ./k -dedicated +cvar_markdown ../../docs/reference/cvars.md +quit
```
