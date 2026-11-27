/*
  ═══════════════════════════════════════════════════════════════
  SAVE DATA — implementation
  ═══════════════════════════════════════════════════════════════
*/

#include "save.h"
#include "platform.h"
#include <stddef.h>
#include <string.h>

SaveData saveData;

static uint32_t checksumOf(const SaveData& d) {
  // FNV-1a over everything before the checksum field
  const uint8_t* p = (const uint8_t*)&d;
  const size_t n = offsetof(SaveData, checksum);
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x01000193u; }
  return h;
}

static void defaults() {
  memset(&saveData, 0, sizeof(saveData));
  saveData.magic = SAVE_MAGIC;
  saveData.version = SAVE_VERSION;
  saveData.difficulty = 1;
}

void save_init() {
  SaveData d;
  memset(&d, 0, sizeof(d));
  if (plat_load(&d, sizeof(d)) && d.magic == SAVE_MAGIC && d.version == SAVE_VERSION &&
      d.checksum == checksumOf(d)) {
    saveData = d;
    if (saveData.difficulty > 2) saveData.difficulty = 1;
    if (saveData.lastCircuit >= NUM_CIRCUITS) saveData.lastCircuit = 0;
    plat_log("save: loaded (%u races)\n", (unsigned)saveData.races);
  } else {
    defaults();
    plat_log("save: defaults\n");
  }
}

bool save_commit() {
  saveData.magic = SAVE_MAGIC;
  saveData.version = SAVE_VERSION;
  saveData.checksum = checksumOf(saveData);
  return plat_save(&saveData, sizeof(saveData));
}

bool save_recordLap(int circuit, float t) {
  if (circuit < 0 || circuit >= NUM_CIRCUITS || !(t > 0)) return false;
  float& b = saveData.bestLap[circuit];
  if (b > 0 && b <= t) return false;
  b = t;
  return true;
}

bool save_recordRace(int circuit, float t) {
  if (circuit < 0 || circuit >= NUM_CIRCUITS || !(t > 0)) return false;
  float& b = saveData.bestRace[circuit];
  if (b > 0 && b <= t) return false;
  b = t;
  return true;
}

void save_recordGPWin(int difficulty) {
  if (difficulty < 0 || difficulty > 2) return;
  if (saveData.gpWins[difficulty] < 65535) saveData.gpWins[difficulty]++;
}
