/*
  ═══════════════════════════════════════════════════════════════
  CIRCUITS — three designed, themed tracks
  Each layout is an explicit list of sections (straights and corners
  given by their turn angle) that closes exactly in the plan; zones per
  section are decorated from a fixed seed. buildCircuit(id) fills the
  segment buffer (layout, elevation, flags, ground materials, buildings,
  scenery), builds the minimap and switches the palette theme and the
  background. Deterministic: a circuit is the same every time it is
  built, in any order. See wiki/13-circuits-and-themes.md.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef CIRCUITS_H
#define CIRCUITS_H

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

#endif // CIRCUITS_H
