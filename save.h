/*
  ═══════════════════════════════════════════════════════════════
  SAVE DATA: records per circuit, Grand Prix wins, settings
  Stored as one blob through plat_save / plat_load (platform.h).
  ═══════════════════════════════════════════════════════════════
*/

#ifndef SAVE_H
#define SAVE_H

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

#endif // SAVE_H
