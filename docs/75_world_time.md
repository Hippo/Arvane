# Time, Day/Night Cycle, and Sky Rendering

## Intent

Arvane separates **monotonic simulation time** from **cyclical celestial time**.

The world simulation needs a tick counter that always moves forward and is never affected by commands, debug tools, sleeping, or visual time changes. This counter is used for deterministic scheduling, delayed actions, liquid flow cadence, crop updates, entity AI cadence, profiling windows, save intervals, and other tick-based systems.

The day/night cycle is a separate visual/gameplay clock. It wraps over a configurable day length and may be changed by commands, debug UI, sleeping, plugins, or world rules.

The renderer derives sky visuals from the cyclical day time, not from the monotonic world tick.

Core rule:

```text
World tick is monotonic simulation time.
Day time is cyclical celestial time.
Commands and debug tools may modify day time.
Commands and debug tools must not modify world tick.
```

---

## Time Concepts

### World Tick

The **world tick** is the authoritative monotonic simulation counter.

It increments once per fixed simulation tick.

```cpp
using WorldTick = std::uint64_t;
```

Properties:

```text
monotonic
never wraps during normal play
never reset by /time commands
never changed by debug time sliders
saved with the world
used for simulation scheduling
```

Examples of systems that should use `world_tick`:

```text
liquid flow cadence
crop growth scheduling
random block ticks
entity AI update cadence
cooldowns
delayed block updates
autosaves
profiling windows
server maintenance tasks
scheduled world events
```

Bad:

```cpp
if (clock.day_time_tick % 30 == 0) {
  update_fluids();
}
```

Good:

```cpp
if (clock.world_tick % 30 == 0) {
  update_fluids();
}
```

`day_time_tick` is mutable and cyclic. It is not suitable for simulation scheduling.

---

### Day Time

The **day time** is the current position inside the day/night cycle.

It wraps in the range:

```text
[0, day_length_ticks)
```

It is used for:

```text
sun angle
moon angle
sky color
fog color
ambient light color
sky light multiplier
star visibility
mob spawning rules
sleep checks
ambient audio
weather lighting
```

Unlike `world_tick`, this value may be:

```text
set directly
advanced
frozen
sped up
slowed down
wrapped
changed by sleeping
changed by commands
changed by debug UI
changed by world rules
```

---

## Clock State

Recommended representation:

```cpp
struct WorldClock {
  std::uint64_t world_tick = 0;

  std::uint32_t day_time_tick = 0;
  std::uint32_t day_length_ticks = 0;

  float time_scale = 1.0f;
  bool daylight_cycle = true;
};
```

Where:

```text
world_tick
  Monotonic simulation counter. Always increments once per fixed simulation tick.

day_time_tick
  Current cyclical position in the day/night cycle.

day_length_ticks
  Number of fixed simulation ticks in one full day/night cycle.

time_scale
  Multiplier applied to the day/night cycle only.

daylight_cycle
  Whether day_time_tick advances automatically.
```

`time_scale` must not affect `world_tick`.

---

## Configuration

The authored configuration should use real-world duration, not raw tick counts.

Recommended config:

```yaml
time:
  enabled: true
  day_length_seconds: 1200
  start_day_fraction: 0.25
  daylight_cycle: true
  time_scale: 1.0
```

Meaning:

```text
day_length_seconds
  Real-world duration of a full day/night cycle.

start_day_fraction
  Initial position within the day/night cycle.
  0.00 = midnight
  0.25 = sunrise/morning depending on authored sky curve
  0.50 = noon
  0.75 = sunset/evening depending on authored sky curve

daylight_cycle
  Whether the day/night cycle advances automatically.

time_scale
  Day/night speed multiplier.
```

The engine derives:

```cpp
day_length_ticks = round(day_length_seconds * fixed_tick_hz);
day_time_tick = round(start_day_fraction * day_length_ticks);
```

Example at 60 TPS:

```text
day_length_seconds = 1200
fixed_tick_hz = 60

day_length_ticks = 72000
```

Do not configure both `day_length_seconds` and `day_length_ticks` in authored config. That creates conflicting sources of truth.

---

## Time Advancement

Each fixed world tick:

```cpp
void tick_world_clock(WorldClock& clock) {
  clock.world_tick += 1;

  if (!clock.daylight_cycle) {
    return;
  }

  if (clock.day_length_ticks == 0) {
    return;
  }

  // Simplest version for time_scale == 1.0.
  clock.day_time_tick = (clock.day_time_tick + 1) % clock.day_length_ticks;
}
```

For fractional or accelerated `time_scale`, use an accumulator.

```cpp
struct WorldClock {
  std::uint64_t world_tick = 0;

  std::uint32_t day_time_tick = 0;
  std::uint32_t day_length_ticks = 0;

  double day_time_accumulator = 0.0;

  float time_scale = 1.0f;
  bool daylight_cycle = true;
};
```

```cpp
void tick_world_clock(WorldClock& clock) {
  clock.world_tick += 1;

  if (!clock.daylight_cycle || clock.day_length_ticks == 0) {
    return;
  }

  clock.day_time_accumulator += double(clock.time_scale);

  while (clock.day_time_accumulator >= 1.0) {
    clock.day_time_tick = (clock.day_time_tick + 1) % clock.day_length_ticks;
    clock.day_time_accumulator -= 1.0;
  }

  while (clock.day_time_accumulator <= -1.0) {
    clock.day_time_tick =
      (clock.day_time_tick + clock.day_length_ticks - 1) % clock.day_length_ticks;
    clock.day_time_accumulator += 1.0;
  }
}
```

This allows:

```text
time_scale = 0.0   frozen celestial time
time_scale = 0.5   half-speed day/night cycle
time_scale = 1.0   normal day/night cycle
time_scale = 2.0   double-speed day/night cycle
time_scale < 0.0   reverse day/night cycle, if allowed
```

Negative `time_scale` is optional. If unsupported, clamp it to `>= 0.0`.

---

## Day Fraction

Most systems should not consume `day_time_tick` directly.

They should consume normalized day fraction:

```cpp
float day_fraction(const WorldClock& clock) {
  if (clock.day_length_ticks == 0) {
    return 0.0f;
  }

  return float(clock.day_time_tick) / float(clock.day_length_ticks);
}
```

Range:

```text
[0.0, 1.0)
```

This is the canonical input for:

```text
sky rendering
sun direction
moon direction
fog color
ambient color
daylight strength
star visibility
weather tinting
sleep checks
night spawning checks
```

---

## Human-Facing Time Display

The engine should not expose an arbitrary Minecraft-style time scale.

For player/debug UI, use a derived 24-hour display.

```cpp
struct ClockTime24 {
  std::uint32_t hour;   // 0..23
  std::uint32_t minute; // 0..59
};
```

```cpp
ClockTime24 to_clock_time_24(float day_fraction) {
  float total_minutes_f = day_fraction * 24.0f * 60.0f;
  std::uint32_t total_minutes =
    std::uint32_t(std::floor(total_minutes_f)) % (24u * 60u);

  return ClockTime24 {
    .hour = total_minutes / 60u,
    .minute = total_minutes % 60u,
  };
}
```

Example display:

```text
00:00
06:30
12:00
18:45
23:59
```

This display is only a view over `day_time_tick`.

It is not a scheduling unit.

---

## Time Commands

Time commands should operate on cyclical day time only.

They must not mutate `world_tick`.

Examples:

```text
/time set 06:00
/time set 12:00
/time set 18:30
/time set midnight
/time set noon
/time add 01:00
/time freeze
/time resume
/time scale 2.0
```

Command behavior:

```text
/time set ...
  Sets day_time_tick from parsed day fraction.

/time add ...
  Adds to day_time_tick and wraps inside day_length_ticks.

/time freeze
  Sets daylight_cycle = false.

/time resume
  Sets daylight_cycle = true.

/time scale ...
  Sets time_scale.
```

Example conversion:

```cpp
std::uint32_t day_tick_from_hour_minute(
  std::uint32_t hour,
  std::uint32_t minute,
  std::uint32_t day_length_ticks
) {
  hour %= 24u;
  minute %= 60u;

  std::uint32_t total_minutes = hour * 60u + minute;
  double fraction = double(total_minutes) / double(24u * 60u);

  return std::uint32_t(std::round(fraction * double(day_length_ticks))) %
         day_length_ticks;
}
```

---

## Debug UI

The debug menu should show both simulation time and celestial time.

Suggested fields:

```text
World Tick:          18420391
Day Time:            14:37
Day Fraction:        0.609
Day Tick:            43848 / 72000
Day Length Seconds:  1200
Daylight Cycle:      true
Time Scale:          1.0
Sun Direction:       x y z
Daylight Strength:   0.82
Star Visibility:     0.00
```

Suggested controls:

```text
time slider
24-hour time input
freeze/resume checkbox
time scale input
set sunrise
set noon
set sunset
set midnight
```

The debug UI must not expose controls that modify `world_tick`.

---

## Saving

The world save should include:

```text
world_tick
day_time_tick
day_length_seconds or resolved day_length_ticks policy
daylight_cycle
time_scale
```

Recommended save structure:

```json
{
  "time": {
    "world_tick": 18420391,
    "day_time_tick": 43848,
    "day_length_seconds": 1200,
    "daylight_cycle": true,
    "time_scale": 1.0
  }
}
```

On load:

```text
restore world_tick
recompute day_length_ticks from day_length_seconds and fixed_tick_hz
clamp/wrap day_time_tick into [0, day_length_ticks)
restore daylight_cycle
restore time_scale
```

If `fixed_tick_hz` changes between versions, using `day_length_seconds` as the authored save/config value preserves intended real-world duration.

---

## Networking

The server owns both `world_tick` and `day_time_tick`.

Clients may interpolate/render locally, but the server remains authoritative.

Time snapshots should be sent:

```text
on join
after dimension/world transfer
periodically at low frequency
when time is changed by command
when sleeping skips time
when daylight_cycle changes
when time_scale changes
```

Do not replicate time every frame.

Example packet:

```cpp
struct WorldTimeSnapshot {
  std::uint64_t server_world_tick;

  std::uint32_t day_time_tick;
  std::uint32_t day_length_ticks;

  float time_scale;
  bool daylight_cycle;
};
```

Client-side estimate:

```cpp
std::uint32_t estimate_client_day_time(
  const WorldTimeSnapshot& snapshot,
  std::uint64_t client_ticks_since_snapshot
) {
  if (!snapshot.daylight_cycle || snapshot.day_length_ticks == 0) {
    return snapshot.day_time_tick;
  }

  double advanced = double(client_ticks_since_snapshot) * double(snapshot.time_scale);
  std::uint64_t whole_ticks = std::uint64_t(std::floor(advanced));

  return std::uint32_t(
    (std::uint64_t(snapshot.day_time_tick) + whole_ticks) %
    std::uint64_t(snapshot.day_length_ticks)
  );
}
```

For corrections, clients should smoothly blend visual time where possible.

Hard snap is acceptable for explicit commands such as:

```text
/time set noon
sleep skip
admin time change
dimension transfer
```

For ordinary periodic sync, prefer smoothing.

---

# Celestial State

The game should convert `day_fraction` into a render/gameplay-facing celestial state.

```cpp
struct CelestialState {
  float day_fraction;

  arv::Vec3 sun_direction;
  arv::Vec3 moon_direction;

  float sun_height;
  float moon_height;

  float daylight_strength;
  float moonlight_strength;
  float star_visibility;

  arv::Vec3 daylight_color;
  arv::Vec3 ambient_color;
  arv::Vec3 fog_color;
};
```

`CelestialState` is derived. It is not saved directly unless needed for debugging.

---

## Sun and Moon Direction

Start with a simple circular orbit.

```cpp
CelestialState build_celestial_state(float day_fraction) {
  CelestialState out {};
  out.day_fraction = day_fraction;

  float angle = day_fraction * arv::TAU;

  // Define 0.00 as midnight, 0.25 as sunrise, 0.50 as noon, 0.75 as sunset.
  float sun_angle = angle - arv::PI;

  arv::Vec3 sun_dir = normalize(arv::Vec3 {
    0.0f,
    std::sin(sun_angle),
    std::cos(sun_angle)
  });

  out.sun_direction = sun_dir;
  out.moon_direction = -sun_dir;

  out.sun_height = sun_dir.y;
  out.moon_height = out.moon_direction.y;

  return out;
}
```

The exact phase mapping may be adjusted by art direction. The important part is that the mapping is centralized and documented.

---

## Daylight Strength

Use smooth curves rather than abrupt thresholds.

Example:

```cpp
float smooth01(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

float daylight_from_sun_height(float sun_height) {
  // Below horizon is night. Above horizon ramps into day.
  return smooth01((sun_height + 0.08f) / 0.24f);
}
```

Suggested behavior:

```text
sun well below horizon: low night light
sun near horizon: dawn/dusk transition
sun above horizon: daylight
```

Keep a small minimum light floor unless the intended art direction is extremely dark nights.

```cpp
float final_daylight_strength(float sun_height) {
  float day = daylight_from_sun_height(sun_height);
  return std::lerp(0.06f, 1.0f, day);
}
```

---

## Star Visibility

Stars should fade in as the sun falls below the horizon.

```cpp
float star_visibility_from_sun_height(float sun_height) {
  return 1.0f - smooth01((sun_height + 0.18f) / 0.20f);
}
```

Suggested behavior:

```text
day:      0.0
dusk:     fades in
night:    1.0
dawn:     fades out
```

---

# Lighting Integration

Chunk lighting should not be recomputed every tick just because the sun moves.

Chunk lighting should store **sky access**, not current sunlight brightness.

For example, stored per-vertex/per-sample lighting may include:

```text
sky_light: 0..15
block_light_r: 0..15
block_light_g: 0..15
block_light_b: 0..15
```

Then the shader applies the global daylight multiplier:

```glsl
float sky = float(v_sky_light) / 15.0;
vec3 sky_contribution = sky * u_daylight_color * u_daylight_strength;
```

This means:

```text
placing/removing blocks may trigger lighting updates
sun moving through the sky does not trigger chunk relighting
time changes update global uniforms only
```

This is important for performance.

---

## Renderer Inputs

The renderer should receive a compact sky/time uniform block.

Example:

```cpp
struct SkyUniforms {
  arv::Vec3 sun_direction;
  float daylight_strength;

  arv::Vec3 moon_direction;
  float moonlight_strength;

  arv::Vec3 ambient_color;
  float star_visibility;

  arv::Vec3 fog_color;
  float day_fraction;

  float cloud_time;
  float cirrus_amount;
  float cumulus_amount;
  float exposure;
};
```

These uniforms are derived from `WorldClock` and weather state.

The renderer should not query or mutate `WorldClock` directly.

---

# Sky Rendering

## Intent

The initial sky renderer should be:

```text
visually strong
cheap enough for older hardware
compatible with OpenGL 3.3
procedural
not dependent on large skybox textures
driven by world time uniforms
compatible with later weather/cloud improvements
```

A good baseline is a fullscreen procedural sky pass using a simplified atmospheric scattering approximation.

The model should approximate:

```text
Rayleigh scattering for blue sky
Mie scattering for bright sun glow and haze
extinction near horizon
night fade
procedural cloud layers
subtle dithering to reduce banding
```

This is not intended to be a physically exact atmosphere simulation. It is an art-directed, game-oriented shader.

---

## Render Pass

Render the sky before opaque world geometry.

Recommended approach:

```text
disable depth write
depth test disabled or configured to always pass
draw fullscreen triangle or fullscreen quad
reconstruct view ray from inverse projection/view
evaluate procedural sky color
write color
```

OpenGL 3.3-compatible fullscreen pass:

```glsl
const vec2 kFullscreenQuad[4] = vec2[](
  vec2(-1.0,  1.0),
  vec2(-1.0, -1.0),
  vec2( 1.0,  1.0),
  vec2( 1.0, -1.0)
);
```

The vertex shader can use `gl_VertexID` and avoid a vertex buffer.

---

## Sky Vertex Shader

Example shape:

```glsl
#version 330 core

out vec3 v_view_ray;

uniform mat4 u_inv_projection;
uniform mat4 u_inv_view_rotation;

const vec2 kQuad[4] = vec2[](
  vec2(-1.0,  1.0),
  vec2(-1.0, -1.0),
  vec2( 1.0,  1.0),
  vec2( 1.0, -1.0)
);

void main() {
  vec2 p = kQuad[gl_VertexID];
  gl_Position = vec4(p, 0.0, 1.0);

  vec4 clip = vec4(p, 1.0, 1.0);
  vec4 view = u_inv_projection * clip;
  view.xyz /= max(view.w, 0.0001);

  v_view_ray = mat3(u_inv_view_rotation) * normalize(view.xyz);
}
```

The view rotation should exclude camera translation so the sky appears infinitely far away.

---

## Sky Fragment Shader Structure

The fragment shader should be organized into small functions:

```text
hash/noise
fbm
rayleigh phase
mie phase
extinction approximation
cloud layers
star field
tone mapping / exposure handoff
```

Recommended uniforms:

```glsl
uniform vec3  u_sun_direction;
uniform vec3  u_moon_direction;

uniform float u_daylight_strength;
uniform float u_moonlight_strength;
uniform float u_star_visibility;

uniform float u_time_seconds;
uniform float u_day_fraction;

uniform float u_cirrus_amount;
uniform float u_cumulus_amount;

uniform float u_exposure;
```

---

## Scattering Approximation

Use a simplified scattering model.

Conceptually:

```glsl
vec3 view_dir = normalize(v_view_ray);
vec3 sun_dir = normalize(u_sun_direction);

float mu = dot(view_dir, sun_dir);

float rayleigh_phase = 3.0 / (8.0 * PI) * (1.0 + mu * mu);
float mie_phase = ...;
```

Rayleigh gives the broad blue sky response.

Mie gives the bright forward-scattering glow around the sun.

Useful constants should be exposed as shader constants or renderer tuning values:

```glsl
const float kRayleighStrength = 0.0025;
const float kMieStrength = 0.0003;
const float kMieG = 0.98;
```

The shader may use wavelength-like coefficients to bias scattering color.

Example:

```glsl
const vec3 kWavelength = vec3(0.650, 0.570, 0.475);
const vec3 kRayleigh = kRayleighStrength / pow(kWavelength, vec3(4.0));
const vec3 kMie = kMieStrength / pow(kWavelength, vec3(0.84));
```

These values are art controls. They should be tuned visually.

---

## Horizon and Night Handling

The shader should treat view rays below the horizon carefully.

Options:

```text
discard below horizon
clamp to horizon color
blend into fog color
```

For a voxel world, clamping/blending is usually preferable to discard because terrain and fog should meet cleanly at the horizon.

Recommended:

```glsl
float horizon = max(view_dir.y, 0.0);
```

Use sun height to blend day and night extinction:

```glsl
float sun_height = u_sun_direction.y;
float day_amount = smoothstep(-0.08, 0.16, sun_height);
float night_amount = 1.0 - day_amount;
```

The night sky should not simply be black. It should retain a subtle blue/purple ambient tone and moon contribution.

---

## Procedural Clouds

The first implementation can include two cheap procedural cloud layers:

```text
cirrus: thin high cloud layer
cumulus: denser lower cloud layer
```

Use value noise or simple FBM.

Recommended controls:

```glsl
uniform float u_cirrus_amount;  // 0..1
uniform float u_cumulus_amount; // 0..1
uniform float u_cloud_time;
```

Clouds should move using world/client visual time, not `world_tick` directly.

Example behavior:

```text
cirrus moves slowly
cumulus moves faster
cloud density fades with horizon/view height
cloud brightness follows daylight/extinction
```

Clouds should be optional and easy to disable for low-end settings.

Suggested settings:

```yaml
sky:
  clouds:
    enabled: true
    cirrus_amount: 0.35
    cumulus_amount: 0.65
    animation_speed: 1.0
```

---

## Dithering

Procedural sky gradients can band, especially on older hardware or low-precision framebuffers.

Add subtle noise:

```glsl
color.rgb += noise(view_dir * 1000.0) * 0.01;
```

This should be small and configurable.

```yaml
sky:
  dithering:
    enabled: true
    strength: 0.01
```

---

## Stars

Stars should be procedural or texture-based.

Initial implementation may use procedural hash stars:

```text
stable in world/view direction
visible only at night
faded by clouds/weather
faded near horizon
```

Star brightness:

```glsl
float stars = procedural_star_field(view_dir);
stars *= u_star_visibility;
stars *= smoothstep(0.0, 0.25, view_dir.y);
```

Stars should not swim with the camera translation.

They should be based on view direction, not world position.

---

## Sun and Moon Discs

The sky shader should render simple procedural discs.

Sun disc:

```glsl
float sun_dot = dot(view_dir, normalize(u_sun_direction));
float sun_disc = smoothstep(0.9992, 0.9998, sun_dot);
float sun_glow = pow(max(sun_dot, 0.0), 512.0);
```

Moon disc:

```glsl
float moon_dot = dot(view_dir, normalize(u_moon_direction));
float moon_disc = smoothstep(0.9990, 0.9997, moon_dot);
```

The sun should be hidden or heavily faded when below the horizon.

The moon should be visible primarily at night, with optional phase support later.

---

## Fog Integration

Sky color and fog color must be coherent.

The fog system should receive derived atmospheric colors from the same celestial state used by the sky.

```cpp
struct AtmosphereState {
  arv::Vec3 sky_zenith_color;
  arv::Vec3 sky_horizon_color;
  arv::Vec3 fog_color;
  arv::Vec3 ambient_color;
  arv::Vec3 daylight_color;
  float daylight_strength;
};
```

The world renderer should use:

```text
fog_color from atmosphere state
daylight_strength from celestial state
ambient_color from celestial state
```

Do not maintain unrelated hardcoded fog colors in the chunk shader.

---

## Tone Mapping

The sky shader may produce HDR-ish values.

If the renderer uses an HDR pipeline, leave tone mapping to the post-processing pass.

If the renderer is initially LDR-only, apply a simple exposure curve before output.

Example:

```glsl
vec3 tonemap_simple(vec3 c, float exposure) {
  return vec3(1.0) - exp(-c * exposure);
}
```

Gamma correction should be handled consistently with the rest of the renderer.

Avoid doing incompatible gamma correction separately in the sky shader if a global post-process already handles it.

---

# Render Order

Recommended render order:

```text
1. begin frame
2. update camera matrices
3. derive CelestialState from WorldClock
4. derive SkyUniforms / AtmosphereState
5. render sky
6. render opaque chunks/entities
7. render transparent chunks/entities
8. composite transparency
9. render particles/effects as appropriate
10. render UI
```

The sky pass should not depend on chunk rendering.

Chunk shaders should receive daylight/fog uniforms derived from the same time state.

---

# Weather Interaction

Weather should modulate the sky, not replace the time system.

Weather may affect:

```text
cloud amount
cloud opacity
fog density
fog color
sun visibility
moon visibility
star visibility
ambient light
rain/snow tint
thunder darkness
```

Example:

```cpp
struct WeatherSkyModifiers {
  float cloud_coverage;      // 0..1
  float precipitation;       // 0..1
  float storm_darkening;     // 0..1
  float fog_density_scale;   // multiplier
};
```

Application:

```cpp
sky.star_visibility *= 1.0f - weather.cloud_coverage;
sky.daylight_strength *= std::lerp(1.0f, 0.55f, weather.storm_darkening);
sky.fog_color = mix(sky.fog_color, storm_fog_color, weather.storm_darkening);
```

Weather should not mutate `day_time_tick`.

---

# Sleep and Time Skips

Sleeping should modify `day_time_tick`, not `world_tick`.

Example:

```text
current day time: 23:40
players sleep
day time set to 06:00
world_tick unchanged except for normal tick advancement
scheduled simulation tasks remain valid
```

This prevents sleeping from breaking:

```text
liquid flow timers
crop schedules
delayed block updates
entity cooldowns
autosave cadence
```

If a system needs to react to a sleep skip, emit an explicit event:

```cpp
struct DayTimeSkippedEvent {
  std::uint32_t old_day_time_tick;
  std::uint32_t new_day_time_tick;
};
```

Consumers can choose whether to react.

---

# Plugin and Game Rule Policy

Plugins may read:

```text
world_tick
day_time_tick
day_length_ticks
day_fraction
24-hour display time
daylight strength
```

Plugins may request changes to:

```text
day_time_tick
daylight_cycle
time_scale
day_length_seconds
```

Plugins must not directly mutate:

```text
world_tick
```

If the engine supports custom dimensions/worlds, each world may have its own:

```text
day_length_seconds
day_time_tick
daylight_cycle
time_scale
sky profile
weather state
```

The server may still maintain a higher-level global tick outside individual worlds if needed, but world-local simulation systems should use the world’s monotonic tick.

---

# Suggested Files / Ownership

Suggested ownership:

```text
arv_world
  Owns WorldClock.
  Advances world_tick and day_time_tick during fixed simulation ticks.
  Saves/loads time state.
  Handles time commands at the authoritative level.

arv_server
  Replicates world time snapshots.
  Applies authoritative time changes.
  Emits relevant events.

arv_client
  Receives time snapshots.
  Maintains interpolated client-side visual estimate.
  Provides render-facing time state.

arv_render
  Owns sky shader, sky pass, atmosphere uniforms.
  Does not own authoritative time.

arv_game or rules layer
  Defines sleep behavior, mob spawning rules, daylight-sensitive gameplay.
```

---

# Tests

Recommended unit tests:

```text
world_tick increments every fixed tick
day_time_tick wraps at day_length_ticks
/time set changes day_time_tick only
/time add wraps correctly
/time freeze stops day_time_tick but not world_tick
time_scale affects day_time_tick but not world_tick
day_length_seconds derives expected day_length_ticks
24-hour conversion formats correctly
sleep skip changes day_time_tick only
network snapshot estimate advances correctly
```

Example test cases:

```text
60 TPS, 1200 second day => 72000 day_length_ticks
day_time_tick 71999 + 1 => 0
/time set 12:00 with 72000 ticks => 36000
/time set 06:00 with 72000 ticks => 18000
world_tick remains unchanged by /time set
```

---

# Non-Goals for Initial Implementation

Do not implement these in the first pass unless already needed:

```text
seasons
latitude
axial tilt
physically accurate atmospheric scattering
moon phases
eclipses
calendar months/years
multi-star systems
volumetric clouds
cloud shadows
dynamic ozone/air composition
```

The architecture should not prevent these later, but the first implementation should stay focused.

---

# Summary Rules

```text
1. Use world_tick for monotonic simulation scheduling.
2. Use day_time_tick for cyclical day/night state.
3. Configure day length in real seconds.
4. Derive day_length_ticks from fixed_tick_hz.
5. Use normalized day_fraction for renderer/gameplay queries.
6. Use 24-hour display for human-facing time.
7. Do not expose arbitrary Minecraft-style time units.
8. Commands/debug tools may change day_time_tick.
9. Commands/debug tools must not change world_tick.
10. Chunk sky light stores sky access, not current daylight brightness.
11. Day/night changes update global lighting uniforms, not chunk light data.
12. Sky rendering is a procedural OpenGL 3.3 pass using cheap atmospheric scattering.
```
