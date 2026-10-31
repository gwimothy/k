# Input and key bindings

**Code:** `engine/client/bindings.*`, `engine/client/kbutton.*`, `engine/platform/keys.*`

## Bindings

```
bind w +forward
bind mouse1 +attack
bind f5 screenshot
bind x "say hi; jump"   // only the first statement of a + binding is a button
unbind x
unbindall
key_listboundkeys
```

Key names are lowercase: letters and digits, `space`, `enter`, `escape`, `tab`, `shift`,
`ctrl`, `alt`, arrow keys (`uparrow`...), `f1`-`f12`, `mouse1`-`mouse5`, `mwheelup`,
`mwheeldown`, keypad keys (`kp_*`), `` ` ``, `semicolon`. Keys are matched by physical
position (US layout), so WASD stays WASD on AZERTY.

**Escape is reserved:** it closes the console if open, otherwise opens/closes the menu.

## Buttons (`+` commands)

A binding that starts with `+` runs `+cmd <key>` when pressed and `-cmd <key>` when released.
A `KButton` remembers which keys hold it, so two keys bound to `+forward` behave correctly
and releasing one doesn't stop you. Opening the console or menu, or losing window focus,
releases every held button.

Buttons: `+forward +back +moveleft +moveright +moveup +movedown +jump +duck +speed +walk
+attack +attack2 +use +reload +left +right +lookup +lookdown`.

Typed in the console without a key (`+forward`), a button stays held until `-forward`. Scripted
tests rely on this: `./k ++forward +wait 60 +-forward` walks for 60 frames.

## From input to the server

Each client tick (66/s by default) the client samples the buttons into a **UserCmd**: view
angles, `forwardMove`/`sideMove`/`upMove` (from `cl_forwardspeed` etc.), and button bits
(`IN_JUMP`, `IN_DUCK`, `IN_USE`, ...). It's sent to the server and also used for
[prediction](networking.md). Mouse look applies every frame, so it adds no latency.

| Cvar | Default | |
|---|---|---|
| `sensitivity` | 3 | Mouse sensitivity |
| `m_pitch` / `m_yaw` | 0.022 | Degrees per mouse count (negative `m_pitch` inverts) |
| `cl_yawspeed` / `cl_pitchspeed` | 210 / 225 | Keyboard turning speed (deg/s) |
