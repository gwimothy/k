# Physics

**Code:** `game/physics.*` (Jolt Physics), `game/entities/props.cpp`

Server-side only; props are simulated on the server and their transforms are networked.

- **Units:** the engine uses inches, Jolt uses meters. Everything converts at the
  `PhysicsWorld` boundary (1 unit = 0.0254 m). Physics gravity is 600 u/s² (HL2's value).
- **World:** one static compound shape made of a convex hull per world brush (vertices
  rebuilt from the brush planes).
- **Brush entities:** solid ones (doors, lifts) get kinematic bodies that follow the entity
  every tick, so props ride lifts and get pushed by doors.
- **Props:** `prop_physics` builds a convex hull from its model's vertices. Mass = volume ×
  60 kg/m³ × `massScale` (a 32-unit crate weighs about 32 kg).

## Player interaction

- Props block the player like boxes (their world bounds). A prop overlapping the player at
  the start of a move doesn't block, so a prop landing on you can't trap you.
- Walking into a prop nudges it along (lighter props move faster).
- `+use` on a prop up to 60 kg picks it up (it floats in front of you); `+use` again drops it,
  `+attack` throws it.
- Hitscan shots push props.

## API

```cpp
PhysicsBody b = physics->addDynamicHull(points, origin, angles, massScale);
physics->setLinearVelocity(b, v);
physics->addImpulse(b, impulse);      // mass × units/s
physics->getTransform(b, origin, angles);
physics->bounds(b, mins, maxs);
```

Angles follow the engine convention (the renderer's `Rz(yaw)·Ry(pitch)·Rx(roll)`).
