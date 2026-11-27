# 10. Game architecture — the module contract

This page is the **authoritative contract** of the finished game on branch
`full-game`: the module split, the shared interfaces (the real header
declarations, injected verbatim from the code), the frame order every module
relies on, the conventions, the headless harness and its test recipes, the
memory map, the measurements, and every deviation from the design brief.

The game was built in phases: a Core phase made everything work end to end
with basic versions, four feature areas made it good in parallel (rivals and
physics, city graphics, circuits and themes, game flow and UI), and an
integration phase merged them, fixed what the combination broke and tuned it.
Each feature area has its own page: [rivals, race and physics](11-rivals-ai.md),
[city graphics and textures](12-city-graphics.md),
[circuits and themes](13-circuits-and-themes.md),
[game flow, screens and HUD](14-game-flow-and-ui.md).

![Neon City](img/circuit-neon-city.png)

## 10.1 Flow

```
TITLE (features the 3 circuits, 5 s each) ──any button──> MAIN MENU
  │ 10 s idle                                         ├─ GRAND PRIX ──> race 1 ─┐
  v                                                   ├─ QUICK RACE ──> SELECT ─┤ (carousel + BACK)
 DEMO (countdown + autopilot on the featured circuit) ├─ DIFFICULTY  (value)    │
  │ any button / 45 s / finish -> TITLE               └─ BACK -> TITLE          v
                                         COUNTDOWN (5 lights, 3-2-1, GO, cars held)
                                                       v
                                         RACE <──> PAUSE (RESUME / RESTART / QUIT)
                                                       v  player crosses the line after the last lap
                                         FINISH (banner, autopilot cool-down 4 s)
                                                       v
                                         RESULTS ──GP──> STANDINGS ──> next race ... ──> CHAMPION ──> MENU
                                                 └─QUICK──> SELECT (same circuit)
MENU / SELECT idle 60 s -> TITLE (attract loop)
```

`game.cpp` owns the state machine (`GameState`), `ui.cpp` draws every screen.
Screen changes that swap the scene fade out (0.2 s), run the next action on a
black screen and fade in (0.3 s); in-race overlays (pause, finish, results,
standings) switch without a fade. Details: [page 14](14-game-flow-and-ui.md).

## 10.2 Modules

| Area | Files | Page |
| --- | --- | --- |
| Game flow, screens, HUD, save, input, harness | `game.*`, `ui.*`, `render_hud.*`, `save.*`, `input.*`, `input_esp32.cpp`, `platform*.{h,cpp}`, `car_game.ino`, `emulator/*` | [14](14-game-flow-and-ui.md) |
| Race, rivals, physics, car rendering | `race.*`, `rivals.*`, `physics.*`, `render_cars.*`, `render_traffic.*`, `render_player.*` | [11](11-rivals-ai.md) |
| Textures, buildings, road pass | `textures.*`, `render_building.*`, `render_road.*`, `gfx.*` | [12](12-city-graphics.md) |
| Circuits, palettes, backgrounds, sprites | `circuits.*`, `track.*`, `colors.*`, `rendering.*`, `render_scenery.*`, `utils.*` | [13](13-circuits-and-themes.md) |
| Shared data and hardware | `structs.h`, `config.h`, `board.h`, `display.h`, `display_esp32.cpp`, `car2_mesh.h`, `car2_texture.h`, `assets/*` | [3](03-architecture.md), [4](04-display-pipeline.md) |

Division of labour between circuits and rendering: `circuits.cpp` PLACES
things (segment flags, ground materials, building type / variant / height per
side, sprites); `render_road` / `render_building` RENDER the structural ones
(every `BuildingType`, every `GroundMat`, kerbs, crosswalks, the start gantry,
rails, lamps, tunnels and portals) and `render_scenery` renders the sprites.
Physics treats building faces, rails and tunnel walls as solid at the limits
of §10.5.

## 10.3 Frame order (what every module may rely on)

`game_loop()` (game.cpp), every frame:

1. `sPrev = sIn; sIn = input_read();` — edges come from consecutive snapshots.
2. Update (race states RACE / FINISH / RESULTS / STANDINGS / DEMO after GO):
   ```cpp
   if (!crashed) { handleInput(dt, in); updatePhysics(dt); checkCollisions(dt); }
   else updateCrash(dt);
   rivals_update(dt);     // every rival decides on the same snapshot, then all move;
                          // ends with physics_resolveContacts(dt): every car-car bump
   race_update(dt);       // mirrors the player into racers[PLAYER_ID]: progress, laps, places
   skyOffset += curve * (speed / maxSpeed) * 150 * dt;
   applyDayCycle(lerp(cycleStart, cycleEnd, playerProgress / race_distance()));
   ```
   COUNTDOWN (and the first 4 s of the demo) only run `rivals_update` +
   `race_update` (cars held on the grid); GO calls `race_go()`. PAUSE, the
   menus and CHAMPION do not update the race; SELECT moves a flyover camera.
3. Render (race states):
   ```cpp
   drawSky(skyOffset, timeSec);
   drawRoad(position, playerX, playerZdist, cameraDepth);  // cars_prepare(position) after pass 1;
                                                            // per slice in pass 2: road rows, buildings /
                                                            // tunnel, rails, cars_drawSlice(n, ...), then the
                                                            // slice's sprites, lamps, gantry, then the portal
   drawPlayerCar(timeSec);
   cars_drawNear();                                         // rivals nearer than the player's car
   drawHUD(timeSec, fps, lcdFps, frameMs);
   ui_* overlays (countdown lights, GO, banners, crash, finish, results ...)
   ```
   PAUSE copies a frozen frame back from a PSRAM snapshot instead of drawing
   the scene; SELECT draws the road from the flyover camera (`drawRoad` uses
   only its parameters, never the physics globals, and `racers[PLAYER_ID].z`
   is set to a phantom car ahead of that camera).
4. `display_present(gfx_fb)`.

Starting a race: `buildCircuit(c)` → `race_init()` (grid, names, paints,
`physics_placePlayer`, `rivals_init`: racing line and speed profile, rebuilt
only when the circuit changes) → COUNTDOWN → `race_go()`. `autopilot` is set
by the game: the demo, the finish cool-down and `--auto`.

Boot (`game_setup()`): `input_init()` → `display_init()` (DMA bands, PSRAM snapshots) →
`gfx_init()` (the 150 KB internal framebuffer while the heap is
unfragmented) → `initCarRenderer()` → `track_init()` → `textures_init()` →
`ui_init()` → pause snapshot → `save_init()` → circuit 0 → title.

## 10.4 Shared interfaces (real header declarations)

Banner comments and include guards are stripped; everything else is verbatim.

### structs.h — track data

```cpp
#include <stdint.h>

#define MAX_SEGS 2400          // capacity; each circuit uses segCount <= MAX_SEGS

// What lies beyond the road edge on each side (rendered by render_road,
// slows the car in physics; layout constants in config.h). MAT_SAND is a
// beach: sand next to the road, then the sea further out. MAT_WATER is water
// just past a narrow sea wall at the road edge (put a guard rail there).
// MAT_SIDEWALK is kerb + sidewalk up to the building line, street beyond.
enum GroundMat : uint8_t {
  MAT_GRASS = 0, MAT_SIDEWALK, MAT_SAND, MAT_WATER, MAT_ROCK, MAT_SNOW, MAT_DIRT, MAT_COUNT };

// Segment::style = type | (variant << 4), variant 0..15
enum BuildingType : uint8_t {
  BLD_NONE = 0,
  BLD_OFFICE,   // concrete office block, regular window grid
  BLD_GLASS,    // glass curtain-wall tower: sky reflection, mullions
  BLD_BRICK,    // brick apartments, punched windows, ledges
  BLD_STONE,    // sandstone classic building, cornices, tall windows
  BLD_MODERN,   // dark slate tower, vertical light strips / neon at night
  BLD_SHOPS,    // 2-4 floors, shopfronts with awnings / signs at street level
  BLD_VILLA,    // coast: white stucco villa, small windows, terracotta roof
  BLD_CLIFF,    // natural rock wall (cuttings, mountain side), no windows
  BLD_WALL,     // concrete retaining wall / sound barrier
  BLD_TYPE_COUNT };

#define SEGF_TUNNEL    0x01
#define SEGF_LAMP_L    0x02    // street lamp on the left at this segment
#define SEGF_LAMP_R    0x04
#define SEGF_CROSSWALK 0x08    // zebra crossing across the road
#define SEGF_START     0x10    // start/finish line at the NEAR edge of this segment (segment 0)
#define SEGF_RAIL_L    0x20    // guard rail along the left road edge (solid for physics)
#define SEGF_RAIL_R    0x40
#define SEGF_SPARE     0x80    // free; circuits.cpp uses it as scratch while generating (cleared after)

struct Segment {               // 20 bytes
  float   curve;
  float   y;                   // elevation at the FAR end of the segment
  uint8_t flags;               // SEGF_*
  uint8_t ground;              // left material | (right material << 4)
  uint8_t build[2];            // [0] left, [1] right: height / BUILDING_H_UNIT, 0 = none
  uint8_t style[2];            // BuildingType | variant << 4
  int8_t  spr[2];              // scenery sprite type per side, -1 = none
  uint8_t sprOff[2];           // distance of the sprite from the road centre, road half-widths * 16
  uint8_t deco;                // reserved (unused for now; every placeable prop is a sprite)
  uint8_t pad;
  bool  isTunnel() const { return flags & SEGF_TUNNEL; }
  int   mat(int side) const  { return side ? (ground >> 4) : (ground & 15); }
  float sprX(int side) const { return (side ? 1.0f : -1.0f) * sprOff[side] * (1.0f / 16.0f); }
};

static_assert(sizeof(Segment) == 20, "Segment must stay 20 bytes");

// Packing helpers (side: 0 = left, 1 = right)
static inline uint8_t groundPack(int left, int right) { return (uint8_t)((left & 15) | ((right & 15) << 4)); }
static inline uint8_t bldStyle(int type, int variant) { return (uint8_t)((type & 15) | ((variant & 15) << 4)); }
static inline int     bldType(uint8_t style)    { return style & 15; }
static inline int     bldVariant(uint8_t style) { return style >> 4; }
```

### config.h — shared constants

```cpp
// ═══════════════════════════════════════════════════════════════
//  SCREEN
// ═══════════════════════════════════════════════════════════════
#define SCR_W       320
#define SCR_H       240
#define SCR_CX      (SCR_W / 2)
#define SCR_CY      (SCR_H / 2)

// Show a small FPS / frame-time readout in the bottom-left corner
#define SHOW_FPS    1

// ═══════════════════════════════════════════════════════════════
//  WORLD
//  The track has segCount segments (track.h); MAX_SEGS (structs.h)
//  is only the capacity of the segment buffer.
// ═══════════════════════════════════════════════════════════════
#define SEG_LEN     200       // length of each segment (world units)
#define RUMBLE_LEN  3         // segments per rumble/stripe colour
#define DRAW_DIST   90        // draw distance in segments
#define ROAD_W      2000      // road half-width (world units)
#define LANES       3         // number of lanes
#define FOV_DEG     100       // field of view in degrees
#define CAM_HEIGHT  1000      // camera height
#define FOG_DENSITY 4.0f      // exponential fog density over DRAW_DIST
#define TUNNEL_H    2800      // tunnel ceiling height above the road

// ═══════════════════════════════════════════════════════════════
//  LATERAL LIMITS (road half-widths from the centre line).
//  Contract between the renderer and the physics: what is drawn
//  at these offsets is solid at these offsets.
// ═══════════════════════════════════════════════════════════════
#define ROAD_EDGE_X     1.00f  // tarmac ends here
#define RAIL_X          1.18f  // guard rails (SEGF_RAIL_L/R)
#define TUNNEL_WALL_X   1.20f  // tunnel walls
#define BUILDING_OFFSET 1.5f   // building / cliff / wall faces (Segment::build > 0)
#define OFFROAD_MAX_X   2.40f  // hard limit when nothing solid is closer

// Ground material layout beyond the road edge (render_road draws it this
// way, physics drags the car accordingly):
//   MAT_SIDEWALK: kerb up to SIDEWALK_KERB_X, sidewalk up to BUILDING_OFFSET,
//                 side-street asphalt beyond
//   MAT_SAND    : beach up to BEACH_WATER_X, the sea beyond
//   MAT_WATER   : sea-wall top up to SEAWALL_X, water beyond
//   others      : rumble strip, then the material
#define SIDEWALK_KERB_X 1.07f
#define BEACH_WATER_X   2.60f
#define SEAWALL_X       1.26f

// ═══════════════════════════════════════════════════════════════
//  BUILDINGS (geometry shared by the circuit generator, the
//  building renderer and the physics)
// ═══════════════════════════════════════════════════════════════
#define BUILDING_H_UNIT    2048    // Segment::build stores height / BUILDING_H_UNIT
#define BUILDING_FLOOR_H   11000   // height of one floor (same units)
#define BUILDING_W         400000  // depth of a building away from the road (px * scale)

// ═══════════════════════════════════════════════════════════════
//  PLAYER CAR LOOK
//  0 = original texture paint (navy). Otherwise an RGB565 colour that
//  replaces the paint, e.g. 0xD000 (red) or 0xFD20 (orange).
// ═══════════════════════════════════════════════════════════════
#define PLAYER_PAINT 0
```

### track.h — segment buffer and builders

```cpp
#include "structs.h"
#include "config.h"

// ═══════════════════════════════════════════════════════════════
//  GLOBAL TRACK STATE
// ═══════════════════════════════════════════════════════════════
extern Segment* segments;     // MAX_SEGS entries (PSRAM preferred)
extern int      segCount;     // segments in the current circuit
extern float    trackLength;  // segCount * SEG_LEN

// Allocate the segment buffer (once; PSRAM first, internal fallback,
// placement logged). Returns false when there is no memory at all.
bool track_init();

// Start a new circuit: segCount = 0.
void track_clear();

// Wrap any index into [0, segCount) (0 when the track is empty).
static inline int segWrap(int i) {
  if (segCount <= 0) return 0;
  i %= segCount;
  return i < 0 ? i + segCount : i;
}
static inline Segment& segAt(int i) { return segments[segWrap(i)]; }

// ═══════════════════════════════════════════════════════════════
//  BUILDERS
// ═══════════════════════════════════════════════════════════════
float lastY();
// Append a plain segment (grass both sides, no flags, no sprites).
// Returns its index, or -1 when the buffer is full.
int  addSeg(float curve, float y);
// Section with eased curve entry/exit and an eased elevation change of
// hillY * SEG_LEN spread over the whole section.
void addRoad(int enter, int hold, int leave, float curve, float hillY);
// Place a sprite on one side (0 left, 1 right) at |x| = dist half-widths.
void setSprite(int idx, int side, int type, float dist);
// Done building: trackLength = segCount * SEG_LEN.
void track_finish();
```

### circuits.h — circuits, minimap

```cpp
#include <stdint.h>

enum Theme : uint8_t { THEME_CITY = 0, THEME_COAST, THEME_MOUNTAIN, THEME_COUNT };

struct CircuitInfo {
  const char* name;            // "NEON CITY"
  const char* tagline;         // one short line for the select screen
  uint8_t theme, laps;
  float cycleStart, cycleEnd;  // day-cycle position at the start and at the end of the race
};

#define NUM_CIRCUITS 3
extern const CircuitInfo CIRCUITS[NUM_CIRCUITS];
extern int currentCircuit;
void buildCircuit(int id);     // segments, flags, materials, buildings, scenery, minimap; setTheme; initBackground
const CircuitInfo& curCircuit();

#define MINIMAP_PTS 128        // closed outline, points evenly spaced along the lap
extern uint8_t minimapX[MINIMAP_PTS], minimapY[MINIMAP_PTS];   // 0..255, aspect ratio preserved
void minimapAt(float z, float& u, float& v);                   // track position -> 0..1 map coordinates
```

### colors.h — palette, themes, day cycle

```cpp
#include <stdint.h>
#include "structs.h"

// Named colours (RGB565) used across the game
#define COL_BLACK   0x0000
#define COL_WHITE   0xFFFF
#define COL_RED     0xF800
#define COL_YELLOW  0xFFE0
#define COL_ORANGE  0xFD20
#define COL_GREEN   0x07E0
#define COL_GREY    0x7BEF
#define COL_CYAN    0x07FF

// ═══════════════════════════════════════════════════════════════
//  DAY / NIGHT CYCLE
//  0 = day, 1 = sunset, 2 = night, 3 = dawn (wraps at 4). Each phase
//  holds, then blends into the next during its last CYCLE_BLEND.
//  Races drive the cycle by race progress (circuit cycleStart ->
//  cycleEnd); the demo / free modes advance it by distance.
// ═══════════════════════════════════════════════════════════════
#define CYCLE_PHASE_DIST   250000.0f  // distance per phase in free mode
#define CYCLE_BLEND        0.30f      // last 30% of each phase blends to the next

// ═══════════════════════════════════════════════════════════════
//  RGB565 HELPERS
// ═══════════════════════════════════════════════════════════════
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// alpha in 0..32 (32 = fg only). Classic packed-channel blend.
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint32_t a32) {
  uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81Fu;
  uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81Fu;
  uint32_t r = ((((f - b) * a32) >> 5) + b) & 0x07E0F81Fu;
  return (uint16_t)(r | (r >> 16));
}

// Saturating add (for glows / lights)
static inline uint16_t add565(uint16_t a, uint16_t b) {
  uint32_t r = (a & 0xF800u) + (b & 0xF800u); if (r > 0xF800u) r = 0xF800u;
  uint32_t g = (a & 0x07E0u) + (b & 0x07E0u); if (g > 0x07E0u) g = 0x07E0u;
  uint32_t l = (a & 0x001Fu) + (b & 0x001Fu); if (l > 0x001Fu) l = 0x001Fu;
  return (uint16_t)(r | g | l);
}

// Scale a colour by s/32 (s in 0..32)
static inline uint16_t scale565(uint16_t c, uint32_t s32) {
  return blend565(c, 0, s32);
}

uint16_t darkenCol(uint16_t c, float f);
uint16_t lerpCol(uint16_t c1, uint16_t c2, float t);

// ═══════════════════════════════════════════════════════════════
//  ACTIVE PALETTE (RGB565, recomputed when the time of day moves)
// ═══════════════════════════════════════════════════════════════
// Facade base colours, shaded per face from the palette light
enum FacadeColor : uint8_t {
  FAC_CONCRETE = 0, FAC_BRICK, FAC_SANDSTONE, FAC_BLUEGLASS, FAC_TEAL, FAC_SLATE,
  FAC_CREAM, FAC_RUST, FAC_STUCCO, FAC_TERRACOTTA, FAC_ROCK, FAC_WALLGREY,
  BUILDING_PALETTE };

// Materials of the scenery props (sprites), lit by the palette's sun /
// moon and ambient light (no fog: the sprite fogs them per depth)
enum PropColor : uint8_t {
  PC_WHITE = 0, PC_RED, PC_YELLOW, PC_BLACK, PC_METAL, PC_DARKMETAL, PC_WOOD, PC_DARKWOOD,
  PC_CONCRETE, PC_GLASS, PC_ORANGE, PC_BLUE, PC_GREEN, PC_STONE, PC_SAND, PC_PURPLE,
  PROP_PALETTE };

struct ColorSet {
  uint16_t skyTop, skyMid, skyHorizon, fog;
  uint16_t grassL, grassD, roadL, roadD;
  uint16_t rumbleL, rumbleD, lane, edgeLine;
  uint16_t skylineFar, skylineNear, windowLit, windowDay;
  uint16_t sun, sunGlow, cloud;
  uint16_t tunnelWallL, tunnelWallD, tunnelCeil, tunnelLight, portal;
  uint16_t foliageL, foliageD, trunk, rock;
  uint16_t lampPole, lampLight;
  uint16_t ground[MAT_COUNT][2];          // light / dark band per material (no fog)
  uint16_t kerbL, kerbD;                  // city kerb (instead of the red/white rumble)
  uint16_t facadeLit[BUILDING_PALETTE];   // face turned to the sun
  uint16_t facadeShade[BUILDING_PALETTE]; // face away from the sun
  uint16_t facadeFront[BUILDING_PALETTE]; // face towards the camera
  uint16_t roof[BUILDING_PALETTE];
  uint16_t prop[PROP_PALETTE];            // lit prop materials (PropColor)
  uint16_t water, waterDeep, seaGlint;    // sea / lake surface (background + ground), sun glitter
  uint16_t skylineMid;                    // middle background layer (between far and near)
  uint16_t snowCap;                       // snow on the background ranges
  // Lighting for the 3D cars, linear 0..1 per channel
  float lightR, lightG, lightB;     // directional (sun/moon) colour
  float ambR, ambG, ambB;           // ambient colour
  float sunDirX, sunDirY, sunDirZ;  // direction TO the light, view space
  float night;                      // 0 = full day .. 1 = full night
  float sunY;                       // sun/moon height on screen (pixels)
  float stars;                      // star visibility 0..1
};

extern ColorSet pal;
extern int   timeOfDay;     // 0=day 1=sunset 2=night 3=dawn (dominant phase)
extern float dayCycle;      // continuous cycle position in [0, 4)
extern int   colorTheme;    // theme set by setTheme (Theme in circuits.h)

// Choose the circuit theme (tints the four time-of-day palettes and sets
// the ground materials). Call before setDayCycle; buildCircuit does it.
void setTheme(int theme);
// Set the cycle position (0..4, wraps) and rebuild `pal` now.
void setDayCycle(float cycle);
// Set the cycle position, rebuilding `pal` only when it can visibly change
// (inside a blend window, or every 1/64 of a phase). Cheap to call per frame.
void applyDayCycle(float cycle);
// Advance the cycle by distance travelled (demo / free mode).
void advanceDayCycle(float distance);
```

### rendering.h — sky and background

```cpp
#include "config.h"
#include "gfx.h"
#include "colors.h"

// Parallax background scroll (advanced by curves * speed)
extern float skyOffset;

// Seconds of the last drawSky() call: the animation clock of the
// background and of the scenery sprites (flags, beams, blinking lights).
extern float bgTime;

// Generate the background of a circuit theme (Theme in circuits.h):
// city skyline / sea horizon with islands / mountain ranges, stars and
// clouds. Deterministic per theme. Call once per circuit (buildCircuit).
void initBackground(int theme);

// Sky gradient, sun/moon, stars, clouds and the background layers of the
// theme chosen at init, with the current palette.
void drawSky(float skyOffset, float timeSec);

// Screen x of the sun / moon centre for a parallax offset (may lie off
// screen). drawSky draws it there; the road's sun glitter on the sea
// (render_road) uses the same value, so the two can never drift apart.
int sky_sunX(float skyOffset);
```

### render_scenery.h — sprites

```cpp
#include <stdint.h>

enum SpriteType : int8_t {
  SPR_PINE = 0,     // conifer, shaded halves
  SPR_TREE,         // round deciduous tree
  SPR_BUSH,         // low bush (not solid)
  SPR_ROCK,         // boulder
  SPR_POST,         // roadside marker post with a reflector
  SPR_PALM,         // palm tree (coast)
  SPR_PINE_SNOW,    // snowy conifer (mountain, high up)
  // ---- added by the themes phase (append only: values are stored in Segment::spr) ----
  SPR_CYPRESS,      // tall slim Mediterranean cypress
  SPR_FLOWERS,      // flower patch (not solid)
  SPR_GRASS,        // grass tuft (not solid)
  SPR_BOULDER,      // big rock-fall boulder
  SPR_FENCE,        // wooden ranch fence section (not solid; placed out of reach)
  SPR_CHEVRON_L,    // chevron board, arrows to the left (left-hand corner ahead)
  SPR_CHEVRON_R,    // chevron board, arrows to the right
  SPR_BOARD_3,      // distance boards before a hard corner: three stripes ...
  SPR_BOARD_2,      // ... two stripes ...
  SPR_BOARD_1,      // ... one stripe (last one before the braking point)
  SPR_BILLBOARD_A,  // advertising hoarding on two legs, three designs,
  SPR_BILLBOARD_B,  // lit at night
  SPR_BILLBOARD_C,
  SPR_LIGHTHOUSE,   // striped lighthouse, the lamp shines at dusk / night
  SPR_UMBRELLA,     // beach umbrella (not solid)
  SPR_TRAFFIC_LIGHT,// traffic light on a pole at a crossing
  SPR_BUS_STOP,     // bus shelter
  SPR_PLANTER,      // concrete planter with a shrub
  SPR_STREET_TREE,  // small tree in a sidewalk pit
  SPR_NEON_PINK,    // vertical neon blade sign fixed to a facade (not solid)
  SPR_NEON_CYAN,
  SPR_SAILBOAT,     // sailboat out on the water (not solid)
  SPR_SNOW_POLE,    // orange / black snow marker pole
  SPR_CHALET,       // alpine wooden hut
  SPR_FLAG,         // chequered flag on a pole (start area)
  SPR_UMBRELLA_B,   // beach umbrella, blue / yellow
  SPR_COUNT };

// Length along the road of one fence piece (world units): pieces placed
// every FENCE_LEN / SEG_LEN segments join into a continuous fence.
#define FENCE_LEN 400.0f

// Sprite anchored at its base (sx, sy) on screen. scale = road projection
// scale at that depth (ProjPt::scale); fog 0..1 towards pal.fog.
void  drawSceneSprite(int type, float sx, float sy, float scale, float fog);

// Collision width in road half-widths (full width of the solid part, e.g.
// a trunk), 0 = not solid (bushes, grass).
float spriteHitWidth(int type);

// Width of the whole sprite in road half-widths (crown, canopy, board),
// used by the circuit generator to keep props clear of the road, of the
// building line and of each other.
float spriteWidth(int type);
```

### render_road.h — road pass, shared camera

```cpp
#include <stdint.h>
#include "config.h"
#include "structs.h"

// A projected road point: centre x, y, half-width (px), scale, camera z
struct ProjPt { float x, y, w, scale, z; };

// Shared camera/projection for the frame (so other modules project exactly
// like the road). Valid from the start of drawRoad() until the next frame.
struct RoadCam { float camX, camY, camDepth; };
extern RoadCam roadCam;

#define ROAD_NEAR_Z 30.0f    // near plane (camera-space z, world units)

static inline void roadProject(ProjPt& p, float wx, float wy, float wz) {
  p.z = wz;
  p.scale = roadCam.camDepth / wz;
  p.x = SCR_CX + p.scale * (wx - roadCam.camX) * SCR_CX;
  p.y = SCR_CY - p.scale * (wy - roadCam.camY) * SCR_CY;
  p.w = p.scale * ROAD_W * SCR_CX;
}

// Per-frame boundary data, valid during and after drawRoad: world x offset
// of the road centre / elevation / camera z of boundary n (boundary n is the
// near edge of segment roadBaseIdx + n).
extern float roadBX[DRAW_DIST + 1], roadBY[DRAW_DIST + 1], roadBZ[DRAW_DIST + 1];
extern int   roadBaseIdx;        // segment index of boundary 0
extern float roadPosOff;         // camera position inside the base segment

// Draws the whole road pass for the frame. Calls cars_prepare(position)
// after pass 1 and cars_drawSlice() per segment in pass 2 (render_cars.h).
void drawRoad(float position, float playerX, float playerZdist, float cameraDepth);
```

### render_building.h — buildings and the textured wall walker

```cpp
#include <stdint.h>
#include "render_road.h"

// One segment-long slice of a building beside the road.
//   pn / pf    : projected road centre at the near / far end of the segment
//   heightUnits: Segment::build[side] (height / BUILDING_H_UNIT)
//   style      : Segment::style[side] (BuildingType | variant << 4)
//   showFront  : first slice of this building -> draw its front facade
//   fog        : 0..1 haze towards pal.fog
void drawBuilding(const ProjPt& pn, const ProjPt& pf, int heightUnits, uint8_t style,
                  int sIdx, bool isLeft, bool showFront, float fog);

// ═══════════════════════════════════════════════════════════════
//  LOOK TUNABLES (renderer only; physics never reads them)
// ═══════════════════════════════════════════════════════════════
// Screen exaggeration of building heights. In the shared units a floor
// (BUILDING_FLOOR_H) is ~1/17 of a street lamp, which made the city a toy
// town (lamps and trees taller than 9-floor blocks); at 3x a floor is about
// a third of a lamp and the streets read as a downtown canyon. The circuit
// generator keeps placing heights in floors / units as before.
#define BLD_V_SCALE      3.0f
// Building depth away from the road, x BUILDING_W (front facades, roofs)
#define BLD_DEPTH_SCALE  1.6f
// Pitched villa roofs: ridge height in floors
#define BLD_RIDGE_FLOORS 0.55f

// Called once per frame by drawRoad() before any building (resets the
// per-frame palette caches).
void buildings_beginFrame();

// ═══════════════════════════════════════════════════════════════
//  TEXTURED VERTICAL WALL (shared with the road pass: tunnel walls)
//  A wall slice between two screen columns xa..xb whose ground and top
//  edges are straight lines; u along the wall is perspective-correct
//  (u/z and 1/z interpolated), v linear per column. Bands from the top:
//  cap (capV texels), body (repeats; one floor per period, window cells
//  re-coloured when `windows`), base (baseV texels at the bottom).
// ═══════════════════════════════════════════════════════════════
struct WallTex;
struct WallJob {
  float xa, xb;              // screen x of the two ends
  float gya, gyb;            // ground line y at the ends
  float tya, tyb;            // top line y at the ends
  float iza, izb;            // 1 / camera depth at the ends
  float uza, uzb;            // u / z (u: level-0 texels along the wall)
  const WallTex* tex;
  float vTot;                // texels from the top edge to the ground
  float capV, baseV;         // texels of the cap band (top) and base band (bottom)
  int   floors;              // floors (windowed walls): body periods + base
  int   dayState;            // -1 hashed window looks, -2 graded by height (curtain wall)
  bool  windows;             // re-colour window slots per cell / floor
  float jag;                 // jagged top amplitude in texels (0 = straight)
  const uint16_t* pal;       // cap + body palette (16 entries)
  const uint16_t* palBase;   // base band palette
  const uint16_t (*day)[3];  // window day looks (TEX_WIN_STATES)
  const uint16_t (*lit)[3];  // window lights (4)
  uint32_t seed;             // window pattern seed
  int   litPct, night100;
  uint16_t farCol;           // flat LOD colour (< ~1.3 px per floor)
};
void drawWallColumns(const WallJob& J);
```

### textures.h — procedural textures

```cpp
#include <stdint.h>

// ═══════════════════════════════════════════════════════════════
//  SEMANTIC PALETTE SLOTS (shared by every texture)
// ═══════════════════════════════════════════════════════════════
enum TexSlot : uint8_t {
  TS_DARK = 0,   // deep shadow, recess, grout, crack
  TS_SHADE,      // shaded material
  TS_BASE,       // base material
  TS_LIGHT,      // lit material / highlight
  TS_ALT,        // secondary material (mortar, spandrel panel, patch)
  TS_ALT2,       // secondary material, darker
  TS_TRIM,       // trim: sills, cornices, coping (light)
  TS_TRIMD,      // trim shadow
  TS_ACC,        // accent: awning, shutters, sign board, stripe
  TS_ACC2,       // accent 2: awning stripe, vegetation
  TS_FRAME,      // window frame / mullion
  TS_WINA,       // window pane (main)        } replaced per window:
  TS_WINB,       // window pane (upper/light) } day reflection or
  TS_WINC,       // window pane (lower/blind) } night lit / dark
  TS_EMITA,      // emissive A: light strip, shop interior (lit at night)
  TS_EMITB,      // emissive B: sign letters, neon (lit at night)
  TS_COUNT };

#define TEX_LEVELS 4

// Column-major wall texture. Rows from the top: CAP module (top of the
// wall: parapet, cornice, coping), BODY module (repeats: one floor of a
// building), BASE module (ground floor / footing). Level l holds
// (1 << wLog2) >> l columns of rows >> l texels each.
struct WallTex {
  uint8_t  wLog2;                 // columns at level 0 = 1 << wLog2
  uint8_t  cap, body, base;       // module rows at level 0 (multiples of 8, body a power of two)
  uint8_t  rows;                  // cap + body + base
  uint8_t  winShift;              // log2 of the window width in texels (night lighting cell)
  const uint8_t* lev[TEX_LEVELS]; // column-major texels
  const uint8_t* avg[TEX_LEVELS]; // one column per level: row-wise majority (grazing views)
};

// Row-major ground texture, level l is (W >> l) x (H >> l)
struct RowTex {
  uint8_t wLog2, hLog2;
  const uint8_t* lev[TEX_LEVELS];
};

enum WallTexId : uint8_t {
  WT_OFFICE = 0,    // concrete grid, recessed punched windows
  WT_RIBBON,        // office with ribbon windows and spandrel bands
  WT_GLASS,         // curtain wall, mullions, sky reflection streaks
  WT_BRICK,         // running bond, mortar, sills and lintels
  WT_STONE,         // ashlar, cornices, tall windows with pediments
  WT_MODERN,        // dark panels, narrow windows, vertical light strips
  WT_SHOPS,         // shopfronts + awnings + signs, flats above
  WT_VILLA,         // stucco, small shuttered windows
  WT_CLIFF,         // layered rock (no floors)
  WT_WALL,          // concrete retaining-wall panels
  WT_TUNNEL,        // tunnel lining: tiles, stripe, skirting
  WT_ROOF,          // terracotta for villa roofs: colour variants only (the
                    // roof is drawn as shaded tile courses, not sampled)
  WT_COUNT };

enum RowTexId : uint8_t {
  RT_ASPHALT = 0,   // aggregate, cracks, patches, tar seams, oil
  RT_PAVEMENT,      // kerb (u 0..4) + sidewalk slabs, spans |x| 1.0 .. 1.5
  RT_GRASS, RT_SAND, RT_WATER, RT_ROCK, RT_SNOW, RT_DIRT,
  RT_COUNT };

// Texel scales (level 0)
#define TEX_U_PER_SEG      16      // wall texels per segment along the road
#define TEX_V_PER_FLOOR    16      // wall texels per building floor
#define TEX_ASPHALT_U      128.0f  // asphalt texels per road half-width
#define TEX_ASPHALT_V      (32.0f / (4.0f * 200.0f))  // asphalt rows per world unit (32 rows = 4 segments)
#define TEX_PAVE_U         64.0f   // pavement texels per road half-width (u = 0 at the road edge)
#define TEX_PAVE_V         (16.0f / (2.0f * 200.0f))  // 16 rows = 2 segments
#define TEX_GROUND_U       32.0f   // ground material texels per road half-width
#define TEX_GROUND_V       (16.0f / (2.0f * 200.0f))

// Synthesise every texture once (idempotent). Returns false only when no
// memory at all could be found (the renderers then fall back to flat fills).
bool textures_init();
bool textures_ready();

const WallTex& tex_wall(int id);
const RowTex&  tex_row(int id);

// START (0) / FINISH (1) banner: row-major, TEX_BANNER_W x TEX_BANNER_H,
// nullptr when not available. Slots: TS_DARK background, TS_LIGHT /
// TS_SHADE chequer, TS_EMITB letters.
#define TEX_BANNER_W 128
#define TEX_BANNER_H 16
const uint8_t* tex_banner(int which);

// ═══════════════════════════════════════════════════════════════
//  PALETTES
// ═══════════════════════════════════════════════════════════════
// Albedo (RGB888) per slot of one colour variant. Emissive slots hold the
// daytime (unlit) colour here; their night colour is in `glow`.
struct TexColors {
  uint8_t rgb[TS_COUNT][3];
  uint8_t glow[2][3];             // night colour of TS_EMITA / TS_EMITB
};

#define TEX_VARIANTS 4
// Colour variant v (0..3) of a wall texture; the theme picks rock tints.
const TexColors& tex_colors(int wallTexId, int variant);

// Faces: sun shading factor of the palette light; TF_TUNNEL is lit by the
// tunnel lamps instead (the same at any time of day)
enum TexFace : uint8_t { TF_LIT = 0, TF_SHADE, TF_FRONT, TF_ROOF, TF_TUNNEL, TF_COUNT };

// Once per frame, before any palette is built: caches the lighting of
// the current palette `pal` (colors.h) per face, plus the window sets.
void tex_beginFrame();

// 16 RGB565 entries: material slots shaded for the face, emissive slots
// blended towards their glow by the night, all fogged (fog32 0..32
// towards fogc). Window slots get the "unlit" day look (callers patch
// them per window with tex_windowSet).
void tex_buildPalette(uint16_t out[TS_COUNT], const TexColors& c, int face, uint16_t fogc, int fog32);

// Night uplight on the lower floors (street lamps, shop spill): the same
// palette with a warm light added to the material slots.
void tex_buildUplitPalette(uint16_t out[TS_COUNT], const TexColors& c, int face, uint16_t fogc,
                           int fog32, float amount);

// Window looks (TS_WINA, TS_WINB, TS_WINC per state), fogged:
//  - day looks: 8 states built from the CURRENT sky (reflections, blinds,
//    curtains, dark interiors; they darken with the sky, so an unlit window
//    at night reflects the night sky). curtain = true: a gradient from the
//    street reflection (state 0) to the sky (state 7) for glass towers.
//    tint: glass tint (RGB888), face: mild per-face shading.
//  - lit looks: 4 interior lights (warm, white, cool, TV blue), not shaded.
#define TEX_WIN_STATES 8
void tex_windowSets(uint16_t out[TEX_WIN_STATES][3], int face, const uint8_t tint[3], bool curtain,
                    uint16_t fogc, int fog32);
void tex_litSets(uint16_t out[4][3], uint16_t fogc, int fog32);

// The 3 colours of window cell (col, floor) of a building (seed). A window
// is lit when it is one of the litPct % lit ones AND the night (0..100) has
// passed its own threshold, so the lights come on one by one at dusk.
// dayState < 0: hashed day look, else that state (curtain wall gradient).
static inline const uint16_t* tex_pickWindow(const uint16_t day[TEX_WIN_STATES][3],
                                             const uint16_t lit[4][3], uint32_t seed, int col,
                                             int floor, int litPct, int night100, int dayState) {
  uint32_t h = seed ^ ((uint32_t)col * 0x9E3779B1u) ^ ((uint32_t)floor * 0x85EBCA77u);
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 13;
  // bits: 0..6 lit roll (of 128), 7..12 switch-on threshold, 13..15 light
  // colour (warm lights dominate: warm 4/8, white 2/8, cool 1/8, TV 1/8),
  // 16..18 day look
  static const uint8_t LIT_PICK[8] = { 0, 0, 1, 0, 2, 1, 0, 3 };
  if ((int)(h & 127u) * 100 < litPct * 128 && night100 > 12 + (int)((h >> 7) & 63u))
    return lit[LIT_PICK[(h >> 13) & 7]];
  return day[dayState >= 0 ? (dayState & 7) : (int)((h >> 16) & 7)];
}

// Row-texture palette from a base colour (RGB565, no fog): per slot the
// base times the texture's relative factors, then fogged.
void tex_rowPalette(uint16_t out[TS_COUNT], int rowTexId, uint16_t base, uint16_t fogc, int fog32);

// Asphalt detail slots that a segment may hide (cracks, patches, seams,
// oil): a mask of TS_* bits; slots not in `show` take the base colour.
void tex_asphaltPalette(uint16_t out[TS_COUNT], uint16_t base, uint32_t showMask, uint16_t fogc, int fog32);

// Pavement palette: kerb slots from `kerb`, slab slots from `slab`.
void tex_pavementPalette(uint16_t out[TS_COUNT], uint16_t slab, uint16_t kerb, uint16_t fogc, int fog32);
```

### gfx.h — software renderer

```cpp
#include <stdint.h>
#include "config.h"
#include "platform.h"

extern uint16_t* gfx_fb;            // SCR_W * SCR_H pixels
extern int gfx_cx0, gfx_cy0, gfx_cx1, gfx_cy1;   // clip: [x0,x1) x [y0,y1)

bool gfx_init();
bool gfx_fbInternal();

void gfx_setClip(int x0, int y0, int x1, int y1);
void gfx_resetClip();
static inline void gfx_setClipBottom(int y) {
  gfx_cy1 = (y < 0) ? 0 : (y > SCR_H ? SCR_H : y);
}

enum GfxMode { GFX_SOLID = 0, GFX_BLEND = 1, GFX_ADD = 2 };

void gfx_clear(uint16_t c);
void gfx_hspan(int y, int x0, int x1, uint16_t c);                 // [x0, x1)
void gfx_hspanMode(int y, int x0, int x1, uint16_t c, int mode, int alpha32);
void gfx_vspan(int x, int y0, int y1, uint16_t c);                 // [y0, y1)
void gfx_fillRect(int x, int y, int w, int h, uint16_t c);
void gfx_fillRectMode(int x, int y, int w, int h, uint16_t c, int mode, int alpha32);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_pixel(int x, int y, uint16_t c);
void gfx_pixelMode(int x, int y, uint16_t c, int mode, int alpha32);
void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);

// Convex polygon, any winding, float vertices (xy pairs), up to 8 vertices.
void gfx_fillPoly(const float* xy, int n, uint16_t c, int mode = GFX_SOLID, int alpha32 = 32);
void gfx_fillQuad(float x0, float y0, float x1, float y1,
                  float x2, float y2, float x3, float y3,
                  uint16_t c, int mode = GFX_SOLID, int alpha32 = 32);
void gfx_fillTri(float x0, float y0, float x1, float y1, float x2, float y2,
                 uint16_t c, int mode = GFX_SOLID, int alpha32 = 32);

void gfx_fillCircle(int cx, int cy, int r, uint16_t c);
void gfx_circle(int cx, int cy, int r, uint16_t c);
void gfx_fillEllipse(int cx, int cy, int rx, int ry, uint16_t c,
                     int mode = GFX_SOLID, int alpha32 = 32);
// Additive radial light: brightest at the centre, fades to 0 at r.
void gfx_glow(int cx, int cy, int r, uint16_t c, int strength32 = 32);

// One screen row split into colour regions at fractional x boundaries,
// with the boundary pixels blended by coverage (cheap 1-px anti-aliasing).
// bounds[nRegions-1] must be ascending; regions extend to the clip edges.
void gfx_rowRegions(int y, const float* bounds, const uint16_t* cols, int nRegions);

// Ordered-dithered (4x4 Bayer) row of one colour given in 8-bit floats:
// smooth gradients without RGB565 banding. ditherRow mixes c0..c1 by t.
void gfx_gradientRow(int y, int x0, int x1, float r8, float g8, float b8);
void gfx_ditherRow(int y, int x0, int x1, uint16_t c0, uint16_t c1, float t);

// ═══════════════════════════════════════════════════════════════
//  TEXTURED SPANS (8-bit indexed texels -> 16-entry RGB565 palette)
//  Fixed point 16.16 coordinates; `mask` wraps the texel index
//  (power-of-two size - 1). All clipped to the clip rectangle.
// ═══════════════════════════════════════════════════════════════
// Vertical span x, rows [y0, y1): texel col[(v >> 16) & mask], v += dv per row.
void gfx_texColumn(int x, int y0, int y1, const uint8_t* col, uint32_t v, int32_t dv,
                   uint32_t mask, const uint16_t* pal);
// gfx_texColumn over a module that repeats every (1 << perShift) texels
// (building floors): whenever v enters period p, the three palette entries
// pal[slot..slot+2] are replaced by win[p] (clamped to nWin - 1), so every
// floor of the column gets its own window colours inside one call.
void gfx_texColumnWin(int x, int y0, int y1, const uint8_t* mod, uint32_t v, int32_t dv,
                      uint32_t mask, int perShift, uint16_t* pal, int slot,
                      const uint16_t* const* win, int nWin);
// Horizontal span y, columns [x0, x1): texel row[(u >> 16) & mask], u += du per pixel
// (u is the value at the centre of column x0).
void gfx_texSpan(int y, int x0, int x1, const uint8_t* row, uint32_t u, int32_t du,
                 uint32_t mask, const uint16_t* pal);

// A texture source for gfx_rowRegionsTex: texel row, u at the centre of
// screen column 0 (16.16, may wrap), du per pixel, mask, palette.
struct GfxTexRow {
  const uint8_t*  row;
  uint32_t        u0;
  int32_t         du;
  uint32_t        mask;
  const uint16_t* pal;
};
// gfx_rowRegions with textured regions: region r is filled from
// tex[sel[r] - 1] when sel[r] > 0 (flat cols[r] otherwise). Boundary
// pixels still blend the flat colours of their two regions.
void gfx_rowRegionsTex(int y, const float* bounds, const uint16_t* cols, int nRegions,
                       const uint8_t* sel, const GfxTexRow* tex);

// 7 rows of the 5x7 glyph of c (bit 4 = leftmost column), for textures.
const uint8_t* gfx_fontGlyph(char c);
// The glyph table itself (ASCII 32..95), for callers that look glyphs up
// per pixel row (ui.cpp's text runs) and want the lookup inlined.
extern const uint8_t gfx_font5x7[64][7];

// 5x7 bitmap font, 6 px advance per character at scale 1.
int  gfx_textWidth(const char* s, int scale);
void gfx_text(int x, int y, const char* s, uint16_t c, int scale);
void gfx_textShadow(int x, int y, const char* s, uint16_t c, int scale, uint16_t shadow);
void gfx_textCentered(int cx, int y, const char* s, uint16_t c, int scale, uint16_t shadow);
```

### race.h — racers, laps, places, results

```cpp
#include <stdint.h>

#define NUM_RIVALS 5
#define NUM_RACERS (NUM_RIVALS + 1)
#define PLAYER_ID  0

// Car footprint in world terms, derived from the renderer (not guessed):
// a car is drawn with pxPerUnit = scale * 27000 and the road half-width is
// scale * ROAD_W * SCR_CX px, so the 2.9 x 6.5-unit model is 0.245 road
// half-widths wide and ~1100 world units (~5.5 segments) long.
#define CAR_HALF_W   0.12f              // road half-widths
#define CAR_LEN      1100.0f            // world units
#define GRID_X       0.45f              // grid columns at x = -0.45 / +0.45
#define GRID_ROW     (CAR_LEN * 1.5f)   // distance between cars of the same column
#define GRID_FRONT   (CAR_LEN * 0.5f + 150.0f)   // pole car centre behind the line

struct Racer {
  char     name[8];
  uint16_t paint;              // RGB565 (player: PLAYER_PAINT, 0 = texture paint)
  float    z;                  // track position of the car centre, [0, trackLength)
  float    x;                  // lateral offset, road half-widths
  float    speed;
  float    yaw;                // visual heading relative to the road (radians, + = nose right)
  float    progress;           // race distance from the start line (negative on the grid)
  int      lap;                // completed laps
  float    lapStart, lastLap, bestLap;
  bool     finished;
  float    finishTime;
  int      place;              // 1-based, updated every frame
  bool     braking, boosting;
};

extern Racer racers[NUM_RACERS];
extern float raceTime;         // seconds since GO (0 during the countdown)
extern bool  raceRunning;      // false on the grid / countdown
extern int   difficulty;       // 0 easy, 1 normal, 2 hard

void  race_init();             // after buildCircuit(): grid positions, names, paints, reset times/laps
void  race_go();               // the countdown says GO
void  race_update(float dt);   // after physics + rivals: progress, laps, finish, places
float race_distance();         // laps * trackLength
int   race_order(int place);   // racer index at 1-based place

// Final classification, in place order: finished racers by finish time,
// the others with an estimated time (remaining distance / average pace).
struct ResultRow {
  uint8_t racer;
  bool    finished;            // false: time is an estimate
  float   time;
  float   bestLap;
};
void  race_results(ResultRow out[NUM_RACERS]);
// Start progress of a grid slot (0 = pole ... NUM_RACERS-1 = last) and its column
float race_gridProgress(int slot);
float race_gridX(int slot);

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (feature phase)
// ═══════════════════════════════════════════════════════════════
// Copy the player's physics state (position + playerZdist, playerX, speed,
// yaw, brake, turbo) into racers[PLAYER_ID]. race_update() does it; the
// rival AI calls it first so it sees the player of THIS frame.
void  race_syncPlayer();
// Seconds since racer i started its current lap (0 before the start)
float race_lapTime(int i);
```

### rivals.h — rival AI

```cpp
void rivals_init();            // per-rival skill, line preferences, start reaction
void rivals_update(float dt);  // drive the rivals (idle until race_go)

// Shared driving helpers (the player's autopilot uses them too)
// Racing-line x (road half-widths) at track position z, plus a lateral bias.
float ai_lineTarget(float z, float bias);
// Fraction of maxSpeed the corners just ahead of z allow (0.6 .. 1).
float ai_cornerSpeed(float z);

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (feature phase)
// ═══════════════════════════════════════════════════════════════
// What the controller wants a car to do this frame.
struct AiCmd {
  float xTarget;     // lateral target (road half-widths)
  float xVel;        // lateral velocity to follow (half-widths / s), already
                     // limited in speed and acceleration
  float vTarget;     // speed limit of the corners / top speed (world units / s):
                     // brake when above it by more than AI_CORNER_TOL (like a human)
  float vTraffic;    // speed that keeps the gap to the car ahead: ease off above it
  bool  brake;       // press the brake
  bool  turbo;       // press the turbo
};
#define AI_CORNER_TOL 330.0f   // world units / s over the corner profile before braking

// The autopilot: the rival controller driving the player's car (demo,
// finish cool-down, --auto). Called by handleInput() every frame while
// `autopilot` is set; racers[PLAYER_ID] must be in sync (race_syncPlayer).
void ai_drivePlayer(float dt, AiCmd& out);

// Lateral velocity of racer i's controller (half-widths / s). Contacts
// kick it (physics_resolveContacts) so bumped rivals bounce visibly.
float ai_latVel(int i);
void  ai_kickLatVel(int i, float dv);

// Rival turbo state for the renderer (flame) and the HUD: meter 0..1.
float ai_turboMeter(int i);

// Personality of racer i (for the UI, optional): one word, upper case.
const char* ai_style(int i);
```

### physics.h — player car, contacts, turbo

```cpp
#include "input.h"

// ═══════════════════════════════════════════════════════════════
//  TUNABLES (per-frame factors are defined at 60 Hz and rescaled by
//  dt, so the handling does not depend on the frame rate)
// ═══════════════════════════════════════════════════════════════
#define SPEED_MULTIPLIER   65.0f   // maxSpeed = SEG_LEN * SPEED_MULTIPLIER (~246 km/h)
#define ACCEL_TARGET       0.9f    // targetAccel = maxSpeed * ACCEL_TARGET
#define ACCEL_RAMP         420.0f  // how fast acceleration ramps up (u/s^3)
#define ACCEL_NEAR_MAX     0.90f   // speed threshold to reduce acceleration
#define ACCEL_DAMPING      0.97f   // multiplier when approaching maxSpeed (per 60 Hz frame)
#define FRICTION           0.996f  // friction per 60 Hz frame (~3.3 s coast-down)
#define BRAKE_DECEL        9000.0f // brake deceleration (u/s^2)
#define GRAVITY_FACTOR     1600.0f // effect of slopes on speed
#define CENTRIFUGAL        0.18f   // centrifugal force in curves
#define CURVE_FORCE        3.0f    // lateral force multiplier in curves
#define LATERAL_FRICTION   0.90f   // lateral velocity damping per 60 Hz frame
#define STEER_AUTO         1.8f    // autopilot response
#define STEER_MANUAL       2.2f    // manual steering speed (road half-widths / s)
#define CENTRIFUGAL_DX     1.5f    // lateral centrifugal push factor
#define TURBO_BOOST        0.12f   // +12 % top speed while boosting
#define TURBO_ACCEL        1.6f    // acceleration multiplier while boosting
#define TURBO_DRAIN        (1.0f / 3.0f)    // a full meter lasts 3 s
#define TURBO_REFILL       (1.0f / 20.0f)   // and refills in ~20 s
#define CRASH_TIME         2.0f    // seconds of spin before the reset

// ---- additions (feature phase) ----
#define BRAKE_ACCEL_KEEP   0.985f  // throttle state kept per 60 Hz frame while braking. It
                                   // used to drop to 0, and with the slow ACCEL_RAMP a single
                                   // brake tap at speed cost ~5 s of sagging speed.
#define ROCKET_WINDOW      0.30f   // TURBO pressed within this long after GO...
#define ROCKET_ACCEL       0.0f    // ...starts with at least this throttle state (u/s^2)...
#define ROCKET_TIME        0.45f   // ...and a free boost: ~+110 u/s^2 of throttle, worth
                                   // ~2.5 car lengths by top speed (1 s = ~10 car lengths)
#define BUMP_REST          0.30f   // restitution of nose-to-tail bumps
#define BUMP_PUSH          0.50f   // share of the bump impulse the car in front receives
#define BUMP_CRASH         0.50f   // closing speed (fraction of maxSpeed) that crashes the player
#define SIDE_KICK          0.35f   // lateral bounce of a side contact (half-widths / s)
#define SIDE_RUB_KEEP      0.996f  // speed kept per 60 Hz frame of side contact (~1 % a bump)
#define RIVAL_EDGE_X       1.08f   // a shoved rival stops on the rumble strip (never in the scenery)
#define DRAFT_BOOST        0.035f  // slipstream: top speed +3.5 % right behind a car...
#define DRAFT_RANGE        2750.0f // ...fading to 0 at this bumper gap (2.5 car lengths)
#define DRAFT_WIDTH        0.30f   // |dx| below this is "behind" it

// ═══════════════════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════════════════
extern float cameraDepth;
extern float playerZdist;      // camera -> player car distance
extern float position;         // camera track position [0, trackLength)
extern float playerX;          // lateral, road half-widths
extern float speed;
extern float maxSpeed;
extern float centrifugal;
extern bool  crashed;
extern float crashTime;        // seconds since the crash (while crashed)

extern float velocityX;        // lateral velocity (drift)
extern float acceleration;     // current throttle acceleration
extern float driftAngle;       // drift angle (visual)
extern float steerVisual;      // smoothed steering for the car's yaw (-1..1)
extern bool  braking;
extern bool  boosting;         // turbo active this frame
extern float turboMeter;       // 0..1
extern bool  autopilot;        // the AI drives the player car (demo, after the finish)
extern float offRoad;          // 0 on tarmac .. 1 on rough ground (visual shake)
extern float rocketTime;       // > 0 while a rocket start boost lasts (seconds left)
extern bool  rocketStarted;    // the player got a rocket start in this race (HUD banner)
extern float bumpShake;        // 0..1, set by a contact, decays (camera / car jolt)
extern float draft;            // 0..1, the player's slipstream (HUD can show it)

// ═══════════════════════════════════════════════════════════════
//  FUNCTIONS
// ═══════════════════════════════════════════════════════════════
void initPhysics();                              // constants + a car at rest at 0
void physics_placePlayer(float camPos, float x); // put the car at rest (grid, reset)
void handleInput(float dt, const InputState& in);// steering, brake, turbo, autopilot
void updatePhysics(float dt);                    // speed, gravity, drift, materials
void checkCollisions(float dt);                  // walls, rails, scenery (rivals: see below)
void updateCrash(float dt);                      // while crashed: spin, then reset

// ---- additions (feature phase) ----
// Car-to-car contacts for every pair (player and rivals), with everybody
// at the same time step: rivals_update() calls it after moving the rivals,
// so checkCollisions() only handles walls and scenery for the player.
void  physics_resolveContacts(float dt);
// Largest |x| a car centre may have on one side (0 left, 1 right) at track
// position z: the nearest solid limit under the car's footprint (rails,
// tunnel walls, building faces, OFFROAD_MAX_X) minus CAR_HALF_W.
float physics_maxX(float z, int side);
// Instant slipstream of racer i (0..1) from the car right in front of it
// in the same lane; cars smooth it before use.
float physics_draftOf(int i);
// Throttle state (u/s^2) that holds the cruise speed (ACCEL_NEAR_MAX * top)
// against friction: the engine eases back to this, never below, at the top.
float physics_cruiseAccel(float top);
```

### render_cars.h / render_traffic.h / render_player.h — car rendering

```cpp
void cars_prepare(float position);              // pick visible rivals and sort them far -> near
void cars_drawSlice(int n, float fog, bool tunnel);
// Rivals whose centre is nearer to the camera than the player's car are
// skipped by the road pass and drawn here, AFTER drawPlayerCar(), so they
// correctly cover it.
void cars_drawNear();
```

```cpp
#include <stdint.h>

// Pixels per model unit relative to the road projection scale. Chosen so a
// car at the player's distance is exactly the size of the player car.
#define TRAFFIC_PX_PER_SCALE 27000.0f

// (sx, sy): projected ground position, scale: road projection scale,
// yaw: heading relative to the line of sight, pitch: viewing elevation,
// camDistModel: distance from the eye in model units (perspective),
// paint: RGB565 (0 = texture paint), brake: brake lights on.
void drawTrafficCar(float sx, float sy, float scale, uint16_t paint, float fog,
                    float yaw, float pitch, bool inTunnel, float camDistModel, bool brake);

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (feature phase)
// ═══════════════════════════════════════════════════════════════
// Level of detail of a car view (render_cars picks it with hysteresis)
#define CAR_VIEW_FULL   0      // full mesh renderer
#define CAR_VIEW_MID    1      // mesh, cheaper lighting (CAR_LOD_MID)
#define CAR_VIEW_SPRITE 2      // a few shaded quads (far away)

// Everything needed to draw one car (filled by render_cars).
struct CarView {
  float sx, sy;          // screen anchor: the car's ground centre
  float ppu;             // pixels per model unit at the anchor
  float camDist;         // eye -> anchor distance in model units
  float yaw, pitch, roll;
  float fog;             // 0..1 towards the fog colour (black in tunnels)
  float flame;           // turbo flame strength 0..1
  uint16_t paint;        // RGB565, 0 = texture paint
  bool  tunnel;          // tunnel lighting and black fog
  bool  lights;          // tail lights
  bool  brake;           // brake lights
  uint8_t lod;           // CAR_VIEW_*
};
void drawCarView(const CarView& v, float timeSec);

// Car width in pixels below which a car is a sprite / not fully lit
// (render_cars adds hysteresis around these).
#ifndef CAR_SPRITE_W
#define CAR_SPRITE_W   10.0f
#endif
#ifndef CAR_MIDLOD_W
#define CAR_MIDLOD_W   40.0f
#endif
```

```cpp
#include <stdint.h>
#include "config.h"

struct CarRender {
  float ax, ay;          // screen anchor: the car's ground centre
  float pxPerUnit;       // screen pixels per model unit at the anchor
  float yaw;             // + turns the nose right
  float pitch;           // camera elevation + road slope (radians)
  float roll;            // body lean (radians, + leans right)
  float camDist;         // eye -> anchor distance in model units (perspective)
  uint16_t paint;        // 0 = texture paint, else RGB565 paint colour
  float fog;             // 0..1 blend towards fogColor
  uint16_t fogColor;
  float lightScale;      // global light multiplier (tunnels)
  bool  tunnelLight;     // light from the tunnel ceiling instead of the sun
  bool  lights;          // tail lights on
  bool  brake;           // brake lights (brighter)
  bool  shadow;          // draw the contact shadow
};

// Viewpoint the player's car is drawn from (rivals near the camera use it too)
#define PLAYER_VIEW_PITCH 0.30f   // radians
#define PLAYER_VIEW_DIST  11.0f   // eye distance in model units
#define PLAYER_PX_PER_UNIT 27.0f

// Copy the texture to internal RAM and tag its paint texels (call once).
void initCarRenderer();

// Draw one car with the current palette lighting.
void renderCar(const CarRender& cr);

// The player's car at its fixed screen position (after drawRoad)
void drawPlayerCar(float timeSec);

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (feature phase)
// ═══════════════════════════════════════════════════════════════
// Screen anchor of the player's car: its ground centre is drawn here
// (bounce excluded). Rivals near the player use the same "car camera".
#define PLAYER_ANCHOR_Y   (SCR_H - 13)
#define PLAYER_STEER_PX   6.0f        // anchor x shift per unit of steerVisual

// Level of detail for renderCarLod (render_cars picks it with hysteresis)
enum CarLod : uint8_t {
  CAR_LOD_FULL = 0,   // Gouraud diffuse + specular + sky reflection, two shadow rings
  CAR_LOD_MID,        // diffuse + ambient only, one shadow ring (small cars)
};

// renderCar() at a chosen level of detail (renderCar == CAR_LOD_FULL).
void renderCarLod(const CarRender& cr, int lod);

// Turbo flame from the two exhausts of a car drawn with `cr` (call right
// after rendering it). strength 0..1 (e.g. fading in/out), time for flicker.
void renderCarFlame(const CarRender& cr, float strength, float timeSec);

// The player's road-slope pitch as drawn this frame (radians, smoothed),
// so rivals beside the player can be pitched the same way.
float playerViewSlope();
```

### input.h

```cpp
struct InputState {
  float steer;     // -1 .. +1
  float stickY;    // -1 up .. +1 down (menus)
  bool  brake, turbo;
  bool  start;     // pause / skip
  bool  confirm;   // menu accept
  bool  back;      // menu back
  bool  any;       // any control touched
};

void input_init();
InputState input_read();
```

### platform.h

```cpp
#include <stdint.h>
#include <stddef.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <esp_attr.h>
  #define PLATFORM_ESP32 1
  // Per-pixel loops must not run from flash: executing XIP through the
  // instruction cache while the same cache serves PSRAM and texture reads
  // thrashes it (lesson from the SOTN/MGS ports on this same chip).
  // noinline: GCC otherwise inlines a HOT function into its caller, which
  // lives in flash, and the IRAM placement is silently lost (measured:
  // rasterTri and convertBand ran from flash). So HOT goes ONLY on
  // functions that contain a whole per-pixel / per-span / per-column loop;
  // per-pixel helpers stay `static inline` without HOT.
  #define HOT IRAM_ATTR __attribute__((noinline))
  // Small hot tables kept in internal DRAM instead of flash-mapped rodata.
  #define FAST_DATA DRAM_ATTR
#else
  #define PLATFORM_ESP32 0
  #define HOT
  #define FAST_DATA
#endif

// Allocation helpers. On the ESP32-S3:
//   internal SRAM : ~1 cycle access, DMA-capable, scarce (~300 KB free)
//   PSRAM (octal) : 8 MB, goes through the shared 32/64 KB cache
// The render target lives in internal SRAM when it fits; big, rarely-touched
// buffers (scanout snapshots) go to PSRAM.
void* plat_alloc_internal(size_t bytes);
void* plat_alloc_psram(size_t bytes);
void  plat_log(const char* fmt, ...);
void  plat_report_memory(const char* tag);

// Persistent storage of one small blob (save data).
//   ESP32   : Preferences / NVS (namespace "arcade3d", key "save")
//   PC      : a small file next to the binary
//   headless: disabled unless the harness passes --save
// plat_load returns false when nothing (or a blob of another size) is stored.
bool  plat_save(const void* data, size_t n);
bool  plat_load(void* data, size_t n);

// ---- additions (UI phase) ----
// Once-per-second health line on the serial port (ESP32 only):
//   [st] fps, frame ms, LCD fps | internal free / largest / minimum ever |
//   PSRAM free | stack high-water of the loop task and of lcd_scan
// Set to 0 to silence it.
#ifndef PLAT_STATUS_LOG
#define PLAT_STATUS_LOG 1
#endif
// Early platform setup: logs every failed heap allocation (ESP32).
void  plat_init();
// Call once per second from the game loop (prints only if PLAT_STATUS_LOG).
void  plat_status(float fps, float frameMs, float lcdFps);

// ---- additions (integration) ----
// Boot check that a HOT loop really landed in IRAM (ESP32: logs
// "iram: <name> yes" or "iram: <name> NO (flash)"; nothing on the PC).
void  plat_log_iram(const char* name, const void* fn);
```

### save.h

```cpp
#include <stdint.h>
#include "circuits.h"

#define SAVE_MAGIC   0x44335241u   // "AR3D"
#define SAVE_VERSION 1

struct SaveData {
  uint32_t magic;
  uint16_t version;
  uint8_t  difficulty;               // last chosen difficulty
  uint8_t  lastCircuit;              // last circuit raced in QUICK RACE
  float    bestLap[NUM_CIRCUITS];    // 0 = none
  float    bestRace[NUM_CIRCUITS];   // 0 = none
  uint16_t gpWins[3];                // per difficulty
  uint16_t races;                    // races finished
  uint32_t checksum;
};

extern SaveData saveData;

void save_init();                              // load, or defaults when missing / corrupt
bool save_commit();                            // write now (between races only)
bool save_recordLap(int circuit, float t);     // true = new record (not written yet)
bool save_recordRace(int circuit, float t);    // true = new record (not written yet)
void save_recordGPWin(int difficulty);
```

### game.h

```cpp
#include <stdint.h>

enum GameState : uint8_t {
  GS_TITLE = 0, GS_DEMO, GS_MENU, GS_SELECT, GS_COUNTDOWN, GS_RACE, GS_PAUSE,
  GS_FINISH, GS_RESULTS, GS_STANDINGS, GS_CHAMPION, GS_COUNT };

void game_setup();
void game_loop();

GameState   game_state();
const char* game_stateName(GameState s);
float       game_stateTime();      // seconds in the current state
bool        game_isGP();
int         game_gpRace();         // 0-based race of the current Grand Prix
int         game_gpPoints(int racer);

// ═══════════════════════════════════════════════════════════════
//  HARNESS HOOKS (emulator/main_headless.cpp)
// ═══════════════════════════════════════════════════════════════
struct GameDebug {
  bool  autoDrive;     // --auto: the autopilot drives the player for the whole run
  bool  freezeCycle;   // --cycle: keep the day cycle at `cycle`
  float cycle;
  float teleportZ;     // --pos: >= 0 teleports the camera here right after GO
  bool  noCountdown;   // --nocd: GO at once
  bool  devkitNav;     // --devkit: menus as on the two-button dev board (added in the UI phase)
};
extern GameDebug gameDebug;

void game_debugStartRace(int circuit);   // --race C: straight to the grid + countdown
void game_debugStartGP();                // --gp: a Grand Prix from the first circuit

// ---- additions (UI phase) ----
// --state NAME[:C]: jump straight to a screen (TITLE, DEMO, MENU, SELECT,
// COUNTDOWN, RACE, PAUSE, FINISH, RESULTS, STANDINGS, CHAMPION) on circuit C.
// Screens that need race data (FINISH .. CHAMPION) fast-forward the races
// with the autopilot, without rendering. Returns false for an unknown name.
bool game_debugJump(const char* stateName, int circuit);
// Records beaten by the last finished race: bit 0 lap, bit 1 race time
int  game_recordFlags();
// The pending screen transition is fading out (input is ignored)
bool game_inTransition();
```

### ui.h

```cpp
#include <stdint.h>
#include "race.h"

#define UI_COUNTDOWN_GO 4.0f      // seconds from the start of the countdown to GO
#define UI_TITLE_FEATURE_TIME 5.0f   // the title features each circuit this long

// ---- full screens ----
void ui_drawTitle(float t);                       // sky, showroom car, logo, PRESS START
void ui_drawMenuBackdrop(float t);                // showroom without the logo (behind menus)

// ---- overlays ----
// Vertical list: title + items, the selected one highlighted.
void ui_drawMenu(const char* title, const char* const* items, int count, int sel, float t);
// Circuit list + BACK (sel == NUM_CIRCUITS), with the selected circuit's
// tagline, laps, records and minimap (the circuit must be built).
void ui_drawCircuitSelect(int sel, float t);
// Start lights and 3-2-1 (t = seconds since the countdown began, GO at UI_COUNTDOWN_GO)
void ui_drawCountdown(float t);
void ui_drawGo(float sinceGo);                    // "GO!" fading out
void ui_drawPause(int sel);                       // RESUME / RESTART / QUIT
void ui_drawFinish(float t, int place);           // FINISH banner + final place
// Results table (place order). points: per racer index, or nullptr outside a GP.
void ui_drawResults(const ResultRow* rows, const int* points, float t);
// GP standings: order = racer indices sorted by points; raceNo 1..3
void ui_drawStandings(const int* order, const int* points, int raceNo, int races, float t);
void ui_drawChampion(int winner, int points, float t);
void ui_drawDemoBanner(float t);
void ui_drawCrash(float sinceCrash);
void ui_drawBanner(const char* text, uint16_t color, float t);   // centred, slides in
void ui_fade(float t, float dur);                 // fade from black over [0, dur)
// Outline of the current circuit in a size x size box at (x, y); with
// racer dots (player last, on top) when dots is true.
void ui_drawMinimap(int x, int y, int size, bool dots);

// Time as M:SS.CC
void ui_fmtTime(char* out, int n, float t);

// ═══════════════════════════════════════════════════════════════
//  ADDITIONS (UI phase) — toolkit shared with the HUD
// ═══════════════════════════════════════════════════════════════

// Colours of the UI theme (RGB565)
#define UI_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define UI_INK      UI_RGB(6, 8, 24)        // panel tint
#define UI_EDGE     UI_RGB(70, 84, 140)     // panel border
#define UI_ACCENT   UI_RGB(255, 56, 104)    // hot pink-red: selection, rules
#define UI_ACCENT2  UI_RGB(56, 206, 255)    // cyan: secondary info
#define UI_GOLD     UI_RGB(255, 206, 52)
#define UI_SILVER   UI_RGB(206, 214, 230)
#define UI_BRONZE   UI_RGB(222, 140, 70)
#define UI_TEXT     UI_RGB(238, 240, 250)
#define UI_DIM      UI_RGB(146, 152, 180)
#define UI_GOOD     UI_RGB(84, 244, 128)
#define UI_BAD      UI_RGB(255, 76, 64)
#define UI_PURPLE   UI_RGB(204, 100, 255)  // fastest lap
#define UI_SHADOW   UI_RGB(0, 0, 10)

// Text drawn as spans: every font dot is a scale x scale block (scale may be
// fractional), grown by outlinePx for the outline / shadow, sheared by slant
// (x shift per pixel of height, the bottom row stays put) and filled with a
// vertical gradient g[0] (top) -> g[1] (middle) | g[2] (middle) -> g[3] (bottom).
struct UiTextStyle {
  float    scale;
  float    slant;
  int8_t   spacing;          // extra pixels between characters
  uint8_t  outlinePx;        // 0 = no outline
  int8_t   shadowDx, shadowDy;   // 0,0 = no shadow
  uint8_t  alpha;            // 32 = opaque, less blends the whole text
  uint16_t g[4];
  uint16_t outline, shadow;
};
enum { UI_LEFT = 0, UI_CENTER = 1, UI_RIGHT = 2 };

UiTextStyle ui_style(float scale, uint16_t color);                  // flat colour, 1 px outline
UiTextStyle ui_styleGrad(float scale, uint16_t top, uint16_t bottom);  // gradient + outline + shadow
UiTextStyle ui_styleChrome(float scale);                            // 80s chrome headline
UiTextStyle ui_styleGold(float scale);
int  ui_textWidth(const char* s, float scale, int spacing = 0);
// (x, y): top-left / top-centre / top-right of the glyph box (align)
void ui_text(float x, float y, const char* s, const UiTextStyle& st, int align = UI_LEFT);
// Outlined text in one colour (the everyday label)
void ui_label(int x, int y, const char* s, uint16_t c, int scale, int align = UI_LEFT);

// Chamfered translucent panel with a border; accent != 0 draws an accent
// rule along the top edge.
void ui_panel(int x, int y, int w, int h, uint16_t accent, int alpha = 22);
// Darken towards UI_INK (fast: shifts and masks, two pixels per word). alpha
// picks the level: <= 16 keeps 1/2 of the background, <= 21 keeps 3/8,
// more keeps 1/4.
void ui_shadeRect(int x, int y, int w, int h, int alpha = 20);
void ui_shadeEllipse(int cx, int cy, int r, int alpha = 20);
// Animated selection bar (gradient, sweeping shine, edge marker)
void ui_selBar(int x, int y, int w, int h, float t, uint16_t c = UI_ACCENT);
// Small icons
void ui_carIcon(int x, int y, uint16_t paint);   // 18 x 9 side view
void ui_arrow(int cx, int cy, int size, int dir, uint16_t c);   // dir 0 up 1 down 2 left 3 right
// Controls hint line for this board (bottom of the menus)
const char* ui_hintNav();                          // e.g. "STICK SELECT  B OK  SELECT BACK"
const char* ui_hintConfirm();                      // e.g. "PRESS B"
// Darken the whole frame (pause, results): a = 0..32
void ui_dim(int a);
// Fade TO black over [0, dur) (screen exits)
void ui_fadeOut(float t, float dur);

// Title with the featured circuit (0..2, its name highlighted; the backdrop
// is that circuit's theme) and the time since it was featured (dips).
void ui_drawTitleEx(float t, int featured, float featuredT);
// Main menu: GRAND PRIX / QUICK RACE / DIFFICULTY / BACK, a line of help
// for the selected item, and the save statistics.
void ui_drawMainMenu(int sel, int difficulty, float t);
// Results with record flags (bit 0 new lap record, bit 1 new race record)
void ui_drawResultsEx(const ResultRow* rows, const int* points, int recordFlags, float t);
// Standings with the points of the last race and the previous order
// (prevOrder may be nullptr). nextCircuit < 0: final standings.
void ui_drawStandingsEx(const int* order, const int* points, const int* racePts,
                        const int* prevOrder, int raceNo, int races, int nextCircuit, float t);
// Champion: order = final standings (racer indices), points per racer.
// Draws the backdrop too (winner's car on a podium, confetti).
void ui_drawChampionEx(const int* order, const int* points, float t);
// Finish: place, race time, record flags
void ui_drawFinishEx(float t, int place, float raceTimeSec, int recordFlags);
// Lap banner with a second line (e.g. "LAST 0:34.21") and an optional tag
void ui_drawBannerEx(const char* text, const char* sub, const char* tag, uint16_t color, float t);
// Pause with the race state underneath the items; gp: quitting ends the GP
void ui_drawPauseEx(int sel, bool gp, float t);
// Circuit select for the carousel layout (the game draws the flyover)
void ui_drawCircuitSelectEx(int sel, float t, float selT);
// Grid + countdown header (GET READY / race number / circuit) and lights
void ui_drawCountdownEx(float t, bool gp, int gpRace, int gpRaces);
// Lights + 3-2-1 only (the demo)
void ui_drawCountdownLights(float t);

// ---- additions (integration) ----
// Boot: logs where the HOT shading span landed (IRAM check, ESP32 only).
void ui_init();
// Start-light state of the countdown t seconds after it began (the world
// gantry in render_road mirrors it): bit i set = light i of 5 lit red;
// *green = all five green (from GO until UI_GO_GREEN seconds after it).
int  ui_countdownLightMask(float t, bool* green);
#define UI_GO_GREEN 2.5f
```

### render_hud.h

```cpp
// fps: game frames per second, lcdFps: frames actually shown on the panel,
// frameMs: CPU time spent producing the last frame.
void drawHUD(float timeSec, float fps, float lcdFps, float frameMs);

// ---- additions (UI phase) ----
// Forget the position / gap tracking (call when a race or the demo starts).
void hud_reset();
```

### utils.h — helpers and the deterministic Rng

```cpp
#include <stdint.h>

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

// Portable min/max (Arduino-ESP32 3.x maps min/max to std:: templates that
// refuse mixed int/float arguments; the emulator does the same).
static inline int   imin(int a, int b)   { return a < b ? a : b; }
static inline int   imax(int a, int b)   { return a > b ? a : b; }
static inline int   iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float fminF(float a, float b) { return a < b ? a : b; }
static inline float fmaxF(float a, float b) { return a > b ? a : b; }

// Clamp a value between a minimum and maximum
float clampF(float v, float lo, float hi);

// Linear interpolation
float lerpF(float a, float b, float t);

// Ease-in interpolation
float easeIn(float a, float b, float t);

// Ease-in-out interpolation
float easeInOut(float a, float b, float t);

// Increment a value with wrap-around into [0, mx)
float loopIncrease(float v, float inc, float mx);

// Fraction of the way through the current cycle (0..1)
float percentRemaining(float v, float total);

// Segment index of track position z (wraps; 0 when no track is loaded)
int findSegIdx(float z);

// Calculate exponential fog
float expFog(float d, float density);

// Check overlap between two objects (centres and full widths)
bool overlapChk(float x1, float w1, float x2, float w2);

// Signed shortest distance a -> b on a loop of length len, in [-len/2, len/2)
float wrapDelta(float a, float b, float len);

// Deterministic integer hash (window patterns, star twinkle, ...)
static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// ═══════════════════════════════════════════════════════════════
//  DETERMINISTIC RANDOM NUMBERS
//  Circuits, scenery and backgrounds are generated from a seeded Rng,
//  never from Arduino's random(), so a circuit is identical every time
//  and headless runs are reproducible. (xorshift32; never yields 0.)
// ═══════════════════════════════════════════════════════════════
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 1u) { setSeed(seed); }
  void setSeed(uint32_t seed) { s = hash32(seed ^ 0xA5A5F00Du); if (!s) s = 0x9E3779B9u; }
  uint32_t next() {
    uint32_t x = s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s = x;
  }
  // integer in [lo, hi) — returns lo when the range is empty
  int range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(next() % (uint32_t)(hi - lo));
  }
  // float in [0, 1)
  float uniform() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
  // float in [lo, hi)
  float frange(float lo, float hi) { return lo + (hi - lo) * uniform(); }
  bool  chance(float p) { return uniform() < p; }
  // -1 or +1
  int   sign() { return (next() & 0x100) ? 1 : -1; }
};
```

### display.h

```cpp
#include <stdint.h>

struct DisplayStats {
  uint32_t offered;       // frames handed to display_present()
  uint32_t dropped;       // frames skipped because the panel was busy
  uint32_t shown;         // frames fully sent to the panel
  uint32_t bandsSkipped;  // 8-line bands not sent (identical to last frame)
  uint32_t timeouts;      // lost DMA completions (recovered)
  uint32_t sendFails;     // refused SPI submissions (recovered)
  uint32_t lastSendUs;    // wire time of the last frame
  uint32_t copyUs;        // time the game spent copying its frame
};

extern DisplayStats displayStats;

bool display_init();
void display_present(const uint16_t* fb);
```

## 10.5 Conventions every module shares

**Coordinates.** `x` is lateral in road half-widths (road edge at |x| = 1),
`z` runs along the track in world units, `SEG_LEN` = 200 per segment,
`trackLength = segCount * SEG_LEN`. Segment `n` spans boundary `n` (near) to
`n+1` (far); `Segment::y` is the elevation of its far end. The start/finish
line is the near edge of segment 0 (`SEGF_START`). Positive `curve` turns
right. Every index wraps with `segCount` (`segWrap`, `segAt`, `findSegIdx`).

**Camera vs car.** `position` (physics.h) is the camera; the player's car is
at `position + playerZdist` (~839 units ahead). `racers[i].z` is a car
**centre** in `[0, trackLength)`.

**Car footprint** (derived from the renderer: `pxPerUnit = scale * 27000`,
road half-width `scale * ROAD_W * SCR_CX`): `CAR_HALF_W` = 0.12 road
half-widths, `CAR_LEN` = 1100 world units (~5.5 segments). Collision boxes,
grid spacing and following distances use these.

**Lateral limits** (renderer ↔ physics): road edge |x| = 1.0; guard rails
(`SEGF_RAIL_*`) at `RAIL_X` 1.18; building / cliff / wall faces
(`build[side] > 0`) at `BUILDING_OFFSET` 1.5; tunnel walls at `TUNNEL_WALL_X`
1.20; otherwise a hard limit at `OFFROAD_MAX_X` 2.4. The car centre stays one
`CAR_HALF_W` inside any solid limit. Physics looks at every segment under the
car's footprint: a limit that appears more than 0.15 inside the car at > 30 %
speed is a frontal hit (crash), otherwise the car scrapes along it. Solid
sprites (`spriteHitWidth() > 0`) crash the player at the front bumper above
25 % speed and are scraped alongside. Rivals drive within |x| ≤ 0.8, never
look at the scenery and are held at `RIVAL_EDGE_X` 1.08 when shoved.

**Contacts** (all six cars, `physics_resolveContacts`): nose to tail costs the
rear car speed and pushes the front car; side contact pushes both apart; only
a closing speed above half of `maxSpeed` crashes the player.

**Ground materials** (`Segment::mat(side)`), as drawn by render_road and
dragged by physics:

| Material | From the road edge outwards |
| --- | --- |
| `MAT_SIDEWALK` | kerb (`kerbL/kerbD`) to `SIDEWALK_KERB_X` 1.07, sidewalk slabs to `BUILDING_OFFSET` 1.5, side-street asphalt beyond |
| `MAT_SAND` | rumble, beach to `BEACH_WATER_X` 2.6, foam, then the sea |
| `MAT_WATER` | rumble, sea-wall top to `SEAWALL_X` 1.26, then water (always with `SEGF_RAIL_*`) |
| `MAT_GRASS`, `MAT_ROCK`, `MAT_SNOW`, `MAT_DIRT` | rumble (0.11), then the material |

Colours: `pal.ground[mat][0/1]` (light/dark band, every `RUMBLE_LEN`
segments), no fog applied (render_road fogs per row); near rows are textured
(`textures.h`, row-major ground textures with mip levels).

**Buildings.** `build[side]` = height / `BUILDING_H_UNIT` (2048), `style[side]`
= `BuildingType | variant << 4`. The renderer draws heights `BLD_V_SCALE` (3)
times taller, with a floor every `BUILDING_FLOOR_H` (about 1/10 of a street
lamp on screen: a dense downtown); villas use floors three times as tall
(`VILLA_FLOOR_K`). Height ranges placed by the circuits: city towers 140-255,
offices 80-210, brick / stone 60-160, shops 40-80,
villas 32-52 (two to three storeys under a pitched roof), rock faces 50-125
(the background ranges show above them), tunnel mouths 110-140 (the top of
the portal's rock face), city underpass walls 34-44. The variant picks the
colour scheme, window type and lit share ([page 12](12-city-graphics.md)).

**Sprites.** `spr[side]` (`SpriteType`, -1 none) stands at the near boundary
of its segment at |x| = `sprOff[side]` / 16. Sizes are world units (a pine is
~3200 units tall); `spriteWidth(type)` is the footprint, `spriteHitWidth(type)`
the solid width (trunk, posts), 0 = soft. Placement keep-out rules (nothing
on the road or rumble, in tunnels, through a facade, inside a rail, at a
lamp, crossing or the gantry; land props on land): [page 13](13-circuits-and-themes.md).

**Grid and race rules.** Rivals 1..5 start in slots 0..4, the player last
(slot 5, P6). Staggered grid: slot k centre at progress
`-(GRID_FRONT + k * GRID_ROW / 2)` (`GRID_FRONT` = 700, `GRID_ROW` = 1650 =
1.5 car lengths between cars of the same column), columns x = ∓0.45.
`progress` = distance since the start line (negative on the grid);
`lap` = completed laps = line crossings after the start; a racer finishes when
`progress >= race_distance()` (sub-frame crossing time). Places: finished by
finish time, then by progress. Rivals keep driving after they finish (70 %
pace). `race_results()` estimates unfinished racers as
`raceTime + remaining / average pace` (marked `*` on the results screen).
GP points 10-7-5-3-2-1; champion = most points, ties broken by the better place
in the last race.

**Rival pace.** Rivals use exactly the player's longitudinal model with their
own top speed `pace * maxSpeed`; pace 1.0 = a car identical to the player's.
Base pace EASY 0.955 / NORMAL 1.000 / HARD 1.030 plus a personality per rival
(VIPER aggressive, NOVA smooth, BLITZ wild, RAVEN defensive, FURY erratic),
a small catch-up effect and a slipstream for every car
([page 11](11-rivals-ai.md)).

**Start lights.** One state for both gantries: `ui_countdownLightMask(t)`
(five lights switch on in a ripple, go out one by one during 3-2-1, all green
at GO for `UI_GO_GREEN` 2.5 s). The UI draws its gantry under the HUD; the
world gantry over the start line (`render_road`) mirrors it in the countdown,
the demo and the first lap.

**Day cycle.** `CircuitInfo::cycleStart → cycleEnd` by race progress through
`applyDayCycle()` (rebuilds `pal` only when it can change). City 1.4 → 2.4
(dusk → night), coast 0.5 → 1.4 (afternoon → sunset), mountain 3.3 → 4.3
(dawn → morning, wraps). The title features each circuit at its own light,
the menus use 1.25. `--cycle` freezes it.

**Rival look.** A rival near the player is drawn from the player car's own
camera (pitch 0.30, eye 11 model units behind), blending into the true road
projection between 1 and 4.5 car lengths ahead, so a rival beside the player
is its exact twin. Each rival is drawn in the road slice of the rear of its
shadow, before that slice's sprites and lamps; rivals behind the player car's
centre are drawn after it (`cars_drawNear`). Three levels of detail.

**Sun.** `sky_sunX(skyOffset)` is the one sun / moon position: `drawSky`
draws it there and the road's sun glitter on the sea follows it.

**HOT code.** `HOT` = `IRAM_ATTR __attribute__((noinline))` on the ESP32.
Only functions that contain a whole per-pixel / per-span / per-column loop
are `HOT` (the gfx span and polygon fillers, the texture column / span
fillers, the car's `rasterTri`, the HUD's `shadeSpan`, the scanout's
`convertBand`); per-pixel helpers are `static inline` without it. The boot
log checks the placement (`iram: <name> yes`).

**Determinism.** Circuits, scenery, textures and backgrounds come from fixed
seeds (`Rng`, `hash32`); rivals from `Rng(circuit, difficulty)`; the headless
clock is virtual. Two runs with the same options are identical, and a circuit
is identical whatever was built before it (`tools/check_circuits.sh`).

## 10.6 Headless harness

```
car_game_headless <outdir> <frames> [options] [frame numbers to save...]
```

| Option | Meaning |
| --- | --- |
| `--race C` | skip title/menus and go straight to circuit C's grid + countdown |
| `--nocd` | with `--race` / `--gp`: skip the countdown (GO at frame 1) |
| `--gp` | start a Grand Prix from the first circuit; the harness confirms the results and standings screens (1.5 s after they appear) so the whole GP runs |
| `--auto` | the player car is driven by the autopilot for the whole run |
| `--diff D` | difficulty 0/1/2 |
| `--cycle C` | override the day cycle (and freeze it) |
| `--pos Z` | teleport the player (camera) to track position Z right after GO (progress is not credited for jumps over a quarter lap) |
| `--in A:B:SPEC` | input from frame A to B (inclusive); SPEC = comma list of `L`,`R`,`U`,`D`,`steer=V`,`brake`,`turbo`,`start`,`confirm`,`back`. Menus react to press edges: a 1-frame press is `--in 50:50:D` |
| `--dump N` | every N frames print state, state time, raceTime, day cycle, and name/place/lap/z/x/speed (km/h) of every racer |
| `--every N` | save every Nth frame |
| `--title` | no automatic presses (the title falls into the demo after 10 s idle) |
| `--state NAME[:C]` | jump straight to a screen before frame 0 (`TITLE` … `CHAMPION`, circuit C); `FINISH` … `CHAMPION` first fast-forward the race(s) with the autopilot |
| `--devkit` | menus react like the two-button dev board (one button moves on release, both = confirm) |
| `--save` | enable save data (`car_game_save.bin` in the current folder) |
| `--segs F` | at frame F print the 40 segments ahead of the camera (curve, y, flags, ground, buildings, sprites) |
| `--steer A:B:V`, `--brake A:B` | older forms of `--in` |

Without `--race` / `--gp` / `--title` / `--state` the harness walks title →
menu → QUICK RACE → circuit 0 with presses at frames 30, 50, 60, 80 (screen
changes fade out for 0.2 s, so the countdown starts at frame ~86 and GO is at
~206). The virtual clock runs at 30 Hz: frame = seconds × 30. At the end it
prints the state, the classification when a race ran, the GP points during a
Grand Prix and the average host time per frame.

Tools (`emulator/tools/`):

| Tool | Use |
| --- | --- |
| `sheet.py out.png scale a.ppm b.ppm ...` | contact sheet (2 columns, Pillow) |
| `balance.sh` | the autopilot races every circuit × difficulty; prints places, times, winners |
| `analyze_dump.py log.txt` | checks a `--dump 1` log: overlapping cars, contacts, lateral steps, jitter, overtakes, lap counters |
| `check_circuits.sh` | builds `circuit_dump`, validates every circuit's layout and keep-out rules (`validate_circuits.py`) and checks determinism |

Examples (from `emulator/`, in WSL):

```bash
make headless
./car_game_headless out 600 --dump 30 10 40 70 90 150 200 300          # menus -> countdown -> race
./car_game_headless out 4400 --race 0 --auto --dump 600               # full 3-lap race, results
./car_game_headless out 11000 --gp --auto                              # whole Grand Prix, GP points
./car_game_headless out 1080 --race 1 --nocd --auto --every 90         # a tour of the coast
./car_game_headless out 46 --race 2 --nocd --cycle 3.6 --pos 40000 --in 0:46:brake 45   # one still
./car_game_headless out 60 --state RESULTS:1 50                        # a screen
./car_game_headless out 760 --title 700                                # title -> demo
python tools/sheet.py sheet.png 1 out/frame_0040.ppm out/frame_0070.ppm
bash tools/balance.sh && bash tools/check_circuits.sh
```

## 10.7 Test recipes

| Check | Recipe | Expected |
| --- | --- | --- |
| Build | `make headless` (WSL) | no warnings (`-Wall`) |
| Flow | default run, `--dump 30` | TITLE, MENU (~f 36), SELECT (~66), COUNTDOWN (~86), RACE (~206) |
| Full race | `--race C --auto --dump 600`, 4400 frames | 3 laps, best laps, FIN for every racer, RESULTS, classification |
| Balance | `tools/balance.sh` | autopilot P1 on EASY, P2-P4 on NORMAL, P4-P6 on HARD |
| AI traces | `--race C --auto --dump 1` + `tools/analyze_dump.py` | no overlaps, lateral steps ≤ 0.06, lap counters only +1 |
| Grand Prix | `--gp --auto`, 11000 frames | 3 countdowns, STANDINGS, CHAMPION; GP points sum to 84 |
| Circuits | `tools/check_circuits.sh` | every rule OK, every circuit deterministic |
| Screens | `--state NAME[:C]` for every state | every screen drawn |
| Pause | `--in` start / D / confirm presses | PAUSE freezes everything; RESUME; RESTART → COUNTDOWN; QUIT → MENU |
| Demo | `--title`, 760 frames | TITLE until ~f 300, then DEMO (countdown, autopilot) |
| Walls | `--race 0 --nocd --in 150:700:R --dump 25` | scrapes along facades, crashes into street trees at speed, reset |
| Save | run with `--save` in an empty folder twice | 44-byte `car_game_save.bin`, the second run loads it |
| Visual | `--every`, `--pos` + `--cycle` stills, `tools/sheet.py` | look at the sheets |
| ESP32 | arduino-cli, both FQBNs, own staging folder | no warnings; report flash / static RAM |
| Raylib | `g++ -fsyntax-only -I<raylib include> emulator/main.cpp` | compiles |

## 10.8 Memory map (ESP32-S3, 8 MB octal PSRAM)

| Buffer | Placement | Size |
| --- | --- | --- |
| Framebuffer `gfx_fb` | internal SRAM (PSRAM fallback, logged) | 153,600 B |
| Car texture: 8-bit indices + 234-colour palette | internal heap + `.bss` | 16,384 B + 512 B |
| Procedural textures (12 wall + 8 ground, 4 mips) | internal SRAM first (PSRAM fallback, logged) | 23,143 B |
| Car mesh copy (verts, normals, indices) | internal only if > 96 KB stay free, else read from flash (logged) | 11,716 B |
| DMA bands ×2 | internal, `MALLOC_CAP_DMA` | 2 × 5,120 B |
| Task stacks: `lcd_scan`, `input_adc` | internal | 4 KB each |
| Mesh work arrays (per-vertex, sort) | internal `.bss` | ~21 KB |
| Road pass (`SegView`, band specs, texture rows) | internal `.bss` | ~9.4 KB |
| Track `segments` (`MAX_SEGS` 2400 × 20 B) | PSRAM (internal fallback, logged) | 48,000 B |
| Racing line + speed profile | PSRAM (internal fallback, logged) | 19,200 B |
| Scanout snapshots ×2 | PSRAM | 2 × 153,600 B |
| Pause snapshot | PSRAM | 153,600 B |
| START / FINISH banners | PSRAM | 4,096 B |
| Background maps, circuit build scratch | PSRAM | 4,480 B + 1,796 B |
| Save data | `.bss` + NVS (`Preferences`, namespace `arcade3d`) | 44 B |
| Mesh, texture source, palettes, texture designs, font | flash (`.rodata`) | ~60 KB |

Allocation order (§10.3 boot) keeps the big internal block first. The serial
log reports every placement:

```
gfx: 320x240 framebuffer (153600 B) in internal SRAM
iram: gfx_hspan           yes        (and the other HOT loops)
car: texture 128x128 as 234-colour indices (16384 B) in internal SRAM
car: mesh copied to internal SRAM (11716 B)      or: mesh read from flash
track: 2400 segments (48000 B) in PSRAM
tex: 12 wall + 8 ground textures with 4 mips (23143 B) in internal SRAM, banners (4096 B) in PSRAM
ui: pause snapshot 153600 B in PSRAM
circuit: build scratch (1796 B) in PSRAM
background: maps (4480 B) in PSRAM
ai: racing line + speed profile (19200 B) in PSRAM
[st] fps ms lcd | int free lg largest min minimum-ever | psram free | stk loop lcd adc   (every second)
```

**Internal RAM budget (XIAO build, from the linker map; estimate until the
device prints its `[mem]` lines).** Static: `.dram0.data` 15,678 B +
`.dram0.bss` 46,432 B (the IDE's "62,112 B"), plus IRAM 65,536 B, of which
49,152 B overlap the data RAM (`.dram0.dummy`): the heap starts at
`0x3FCA32A0`. The game's own share is 38.8 KB of `.bss` and 6 KB of IRAM (the
HOT loops); the rest is ESP-IDF / Arduino. The S3's heap regions then give
about 281 KiB (up to `0x3FCE9710`) + 21 KiB (reclaimed startup stack) +
32 KiB (`0x3FCF0000`) of internal heap before the Arduino tasks start. After
the runtime (~30-40 KB of tasks and drivers) and the game's internal
allocations (~228 KB: framebuffer, car texture, textures, DMA bands, two task
stacks; + 11.7 KB if the mesh is copied) roughly **70-85 KB of internal heap
stay free**. The framebuffer is allocated when the largest internal block is
still ~230-250 KB, so it fits with a wide margin; the textures and the mesh
copy fall back to PSRAM / flash if the real numbers are smaller.

`EXT_RAM_BSS_ATTR` cannot help here: the core's `qio_opi` sdkconfig has
neither `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` nor the NOINIT
equivalent, so the attribute is empty (checked); big buffers are allocated
in PSRAM at runtime instead.

## 10.9 Measurements (end of the integration)

| Item | Result |
| --- | --- |
| Headless build (g++ 13, `-Wall`) | no warnings; Raylib frontend `-fsyntax-only` OK |
| ESP32 XIAO (`esp32:esp32:XIAO_ESP32S3:PSRAM=opi`, core 4.0.0) | no warnings; 559,109 B flash (16 %), 62,112 B static RAM (18 %) |
| ESP32 dev board (`esp32:esp32:esp32s3:PSRAM=opi`, `-DBOARD=2`) | no warnings; 587,741 B flash (44 %), 62,512 B static RAM (19 %) |
| Circuits | NEON CITY 1856 segments, SUNSET COAST 1976, ALPINE PASS 1960; all rules OK, deterministic |
| Autopilot best lap / race (NORMAL) | city 32.2 s / 1:43.6, coast 33.4 s / 1:47.5, mountain 33.8 s / 1:49.1 |
| Balance (autopilot place, EASY / NORMAL / HARD) | city P1 / P3 / P4, coast P1 / P2 / P5, mountain P1 / P2 / P5 |
| AI traces (NORMAL, every frame, 3 circuits) | 0 overlapping frames, 0 contacts, lateral step ≤ 0.06, every car within \|x\| ≤ 0.97, 0 crashes, lap counters correct |
| Grand Prix `--gp --auto` | VIPER 27, YOU 19, NOVA 18, BLITZ 11, RAVEN 5, FURY 4 (sum 84) |
| Host CPU per frame vs Core (WSL, best of 5-8 runs) | city race start 0.72 → 1.76 ms (night 0.77 → 1.82), city lap 0.72 → 1.39, coast start 0.57 → 1.19, mountain start 0.62 → 1.08, mountain lap 0.60 → 1.18 |

The merged game costs about twice the Core per frame on the host. A gprof
profile of the city at night: car renderer ~32 % (`rasterTri` 27 %),
textured building walls ~30 %, sprites ~13 %, glows ~5 %, HUD panel shading
~5 %; on the mountain lap the cars are ~41 % and the sprites ~19 %. Nothing
has run on the S3 yet: the HUD's `FPS / MS / LCD` readout and the `[st]` line
are the first things to read on the device; the cheapest knobs are listed in
pages 11 (car LOD thresholds), 12 (`TEX_MAX_Z`, `BLD_V_SCALE`, `WALL_BLOCK`)
and 13 (sprite LODs).

## 10.10 Deviations from the brief (and why)

Core phase:

1. **`config.h` holds only shared constants.** Module tunables live with
   their owner: physics → `physics.h`, day cycle → `colors.h`, car footprint /
   grid → `race.h`, building ranges → `circuits.cpp`. `TOTAL_SEGS`,
   `TOTAL_LAPS` (now `CircuitInfo::laps`), `RANDOM_TRACK`, `CYCLE_START`,
   `MAX_CARS`, `BUILDING_H_MIN/MAX`, `BUILDING_SEG/GAP_*` are gone.
2. **New shared constants** in `config.h`: `ROAD_EDGE_X`, `RAIL_X`,
   `TUNNEL_WALL_X`, `OFFROAD_MAX_X`, `SIDEWALK_KERB_X`, `BEACH_WATER_X`,
   `SEAWALL_X` — the renderer/physics contract of §10.5 as code.
3. **Material semantics** chosen for one material per side: `MAT_SAND` is a
   beach that ends in the sea, `MAT_WATER` is water behind a sea wall,
   `MAT_SIDEWALK` is kerb + sidewalk + side street.
4. **`cars_drawNear()`**: without it the player's car (drawn after the road)
   covered rivals between it and the camera.
5. **Physics API**: `checkCollisions(float dt)`, `updateCrash(dt)`,
   `physics_placePlayer()`, `boosting`, `turboMeter`, `crashTime` (seconds);
   laps and times live in `race.h`.
6. **No idle autopilot in races** (the old 8 s rule): the game sets
   `autopilot` (demo, finish cool-down, `--auto`).
7. **Staggered grid** (slots half a row apart); cars in the same column are
   1.5 car lengths apart.
8. **Rival pace** is relative to the player's car (identical longitudinal
   model with a per-rival top speed), not a fraction of `maxSpeed`: the
   player's car cruises at ~90 % of `maxSpeed`.
9. **Title** leaves on any control (the dev board has no single START
   button); menus move with steer too.

Feature phase (details on pages 11-14): car-car contacts resolved after every
car moved (`physics_resolveContacts`); slipstream and rocket start; the car
texture as 8-bit indices; rivals drawn from the player's car camera nearby;
building heights drawn 3× (`BLD_V_SCALE`); designed section-based circuits
instead of generated ones; a PSRAM pause snapshot; the ADC read in its own
task; NVS writes only between races.

Integration phase:

10. **Building heights.** The circuits had raised every height about 3× to
    look right on the Core's 1× renderer, while the city renderer draws
    heights 3× on its own: villas became 10-18-storey blocks and rock faces
    hid the whole sky. City heights were kept (towers leaving the screen are
    the downtown look); villas were lowered to 32-52 and drawn with 3× floors
    (`VILLA_FLOOR_K`, texels keep their aspect) so they read as two- or
    three-storey houses under terracotta roofs; rock faces were lowered to
    50-125 and tunnel mouths to 110-140, so the islands and the ranges show
    above them and the mouths meet the portal's rock face.
11. **Start lights.** The world gantry had three lights coming on one by one
    while the UI gantry has five going out one by one; the world gantry now
    has five lights mirroring `ui_countdownLightMask()`.
12. **Slice order.** Each road slice draws its cars before its sprites, lamps
    and gantry (they stand on the slice's near edge, in front of the cars).
13. **`textures_init()` at boot**, right after `track_init()`, instead of
    lazily on the first race frame.
14. **Rival tables cached per circuit**: a restart or a carousel move back to
    the same circuit does not rebuild the racing line and speed profile.
15. **HOT = IRAM + noinline**, `convertBand` non-inline, IRAM placement
    logged at boot (the brief's assignment to the integrator).
16. **One sun position** (`sky_sunX()`) for the sky and the sea glitter; one
    font table (`gfx_font5x7`) for gfx and the UI.
17. **HUD**: a ROCKET START banner (after the GO lights have gone) and a
    SLIPSTREAM tag above the turbo meter while towed.
18. **City chevron boards stay solid** (a crash when hit at speed): with full
    steering the car drifts only ~0.27 half-widths out of a 90° city corner
    at cruise speed, so they punish only a real mistake.

## 10.11 Known limitations

- **Hardware**: nothing of this game has run on the S3 yet. Frame time,
  internal heap and the input task are estimates or reasoning until the
  device's `[mem]` / `[st]` lines and the HUD readout are read. The host
  profile suggests the car renderer and the textured walls are the first
  places to look.
- **Gameplay**: rivals do not defend; rival-rival contact is rare; most place
  changes happen in the first 20 s; no sound.
- **Visuals**: villa roofs and tunnel lamp strips are shaded polygons; rock
  portals show a gentle repeat; box props (fences, bus shelters, chalets)
  point at the vanishing point in tight curves; the parallax backdrop does not
  track the real sea side on the coast.
- **UI**: the select carousel rebuilds the circuit on every move (a short
  hitch hidden by a dip); the HUD position banner is timed on the wall clock
  (it may expire during a pause).
