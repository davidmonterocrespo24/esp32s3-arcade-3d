# 🚀 GUÍA DE OPTIMIZACIÓN - ESP32-S3 Racing Game

## 📊 Estado Actual
- **Flash**: 411KB / 1.3MB (31%) ✅
- **RAM**: 32KB / 320KB (9%) ⚠️
- **PSRAM**: 8MB disponible ❌ NO USADO

---

## 🎯 OPTIMIZACIONES PRIORITARIAS

### 1. USAR PSRAM PARA SPRITE BUFFER (Ahorra ~150KB RAM)
**Mayor impacto - Implementar primero**

```cpp
// En car_game.ino, cambiar:
spr.createSprite(SCR_W, SCR_H);

// Por:
spr.setColorDepth(16);
spr.createSprite(SCR_W, SCR_H);
spr.setAttribute(PSRAM_ENABLE, true);  // ← Usar PSRAM
```

**Beneficio**: Libera 150KB de RAM → Solo queda 4KB de variables globales

---

### 2. REDUCIR SEGMENTOS DE PISTA (Ahorra ~8KB RAM)

**Actual**: 300 segmentos × 28 bytes = 8,400 bytes

```cpp
// En config.h cambiar:
#define TOTAL_SEGS  300   // Actual

// Por:
#define TOTAL_SEGS  200   // Optimizado (suficiente para el juego)
```

**Beneficio**: Ahorra 2,800 bytes de RAM sin afectar jugabilidad

---

### 3. OPTIMIZAR ESTRUCTURA Segment (Ahorra ~3KB)

**Actual**: 28 bytes por segmento
```cpp
struct Segment {
  float curve;              // 4 bytes
  float y;                  // 4 bytes
  int8_t spriteType;        // 1 byte
  float spriteOffset;       // 4 bytes
  bool tunnel;              // 1 byte
  int buildL, buildR;       // 8 bytes (2×4)
  uint16_t colorL, colorR;  // 4 bytes (2×2)
  // Total: 26 bytes + padding = 28 bytes
};
```

**Optimizado**: 16 bytes por segmento
```cpp
struct Segment {
  float curve;              // 4 bytes
  int16_t y;                // 2 bytes (SEG_LEN×100, suficiente rango)
  int8_t spriteType;        // 1 byte
  int8_t spriteOffset;      // 1 byte (×10, rango -12 a +12)
  uint8_t flags;            // 1 byte (tunnel:1, hasBuilding:1, etc)
  uint16_t buildHeight;     // 2 bytes (combinar L/R con bit de lado)
  uint16_t buildColor;      // 2 bytes (un solo color)
  // Total: 13 bytes + padding = 16 bytes
};
```

**Beneficio**: 300 seg × 12 bytes = 3,600 bytes ahorrados

---

### 4. USAR PROGMEM PARA DATOS CONSTANTES (Ahorra ~500 bytes)

Mover colores de tráfico, estrellas, etc. a Flash:

```cpp
// En track.cpp, cambiar:
const uint16_t cols[] = { ... };

// Por:
const uint16_t PROGMEM trafficColors[] = { ... };

// Y leer con:
uint16_t col = pgm_read_word(&trafficColors[i]);
```

---

### 5. REDUCIR DRAW_DISTANCE SI ES NECESARIO (Ahorra RAM cache)

```cpp
// En config.h:
#define DRAW_DIST   40   // Actual (bueno para 320×240)

// Si necesitas más RAM:
#define DRAW_DIST   30   // Suficiente para juego fluido
```

**Beneficio**: Ahorra ~120 bytes en rCache/rClip

---

## 🎨 MEJORAS VISUALES (Como Top Gear SNES)

### 6. AÑADIR VELOCÍMETRO CIRCULAR

```cpp
void drawSpeedometer(int speed, int maxSpeed) {
  int centerX = SCR_W - 60;
  int centerY = SCR_H - 60;
  int radius = 45;

  // Arco del velocímetro
  float angle = map(speed, 0, maxSpeed, -135, 135) * PI / 180;

  // Dibujar marcas (0, 100, 200, 300 km/h)
  for(int i = 0; i <= 3; i++) {
    float a = (-135 + i * 90) * PI / 180;
    int x1 = centerX + cos(a) * (radius - 5);
    int y1 = centerY + sin(a) * (radius - 5);
    int x2 = centerX + cos(a) * radius;
    int y2 = centerY + sin(a) * radius;
    spr.drawLine(x1, y1, x2, y2, TFT_ORANGE);
  }

  // Aguja
  int needleX = centerX + cos(angle) * (radius - 10);
  int needleY = centerY + sin(angle) * (radius - 10);
  spr.drawLine(centerX, centerY, needleX, needleY, TFT_RED);
}
```

### 7. SISTEMA DE VUELTAS Y POSICIÓN

```cpp
// Añadir en physics.h:
extern int currentLap;
extern int totalLaps;
extern int playerPosition;  // 1st, 2nd, etc.

// En HUD:
spr.print("LAP ");
spr.print(currentLap);
spr.print("/");
spr.print(totalLaps);
```

### 8. MINIATURAS DE RIVALES EN EL RETROVISOR

```cpp
void drawRearViewMirror() {
  // Pequeño rectángulo superior central
  int mirrorX = SCR_CX - 30;
  int mirrorY = 5;
  int mirrorW = 60;
  int mirrorH = 25;

  spr.drawRect(mirrorX, mirrorY, mirrorW, mirrorH, TFT_DARKGREY);
  spr.fillRect(mirrorX+1, mirrorY+1, mirrorW-2, mirrorH-2, rgb(20,30,40));

  // Dibujar coches cercanos detrás
  // (implementar lógica de detección de coches atrás)
}
```

---

## ⚡ OPTIMIZACIONES DE RENDIMIENTO

### 9. PRE-CALCULAR TRIGONOMETRÍA

```cpp
// Tabla de lookup para sin/cos
#define SIN_TABLE_SIZE 360
float sinTable[SIN_TABLE_SIZE];
float cosTable[SIN_TABLE_SIZE];

void initTrigTables() {
  for(int i = 0; i < SIN_TABLE_SIZE; i++) {
    float rad = i * PI / 180.0;
    sinTable[i] = sin(rad);
    cosTable[i] = cos(rad);
  }
}

// Usar en lugar de sin()/cos()
float fastSin(int deg) {
  return sinTable[deg % SIN_TABLE_SIZE];
}
```

### 10. USAR DUAL CORE DEL ESP32-S3

```cpp
// Renderizado en Core 0, Física en Core 1
TaskHandle_t physicsTask;

void physicsLoop(void * parameter) {
  while(1) {
    handleInput(dt);
    updatePhysics(dt);
    checkCollisions();
    vTaskDelay(10);
  }
}

void setup() {
  // ... código actual ...

  // Crear tarea en Core 1
  xTaskCreatePinnedToCore(
    physicsLoop,
    "Physics",
    4096,
    NULL,
    1,
    &physicsTask,
    1  // Core 1
  );
}
```

---

## 📈 RESULTADOS ESPERADOS

| Optimización | RAM Ahorrada | Impacto |
|--------------|--------------|---------|
| PSRAM para sprite | ~150KB | 🔥🔥🔥 |
| Reducir segmentos | ~3KB | ⭐⭐ |
| Optimizar Segment | ~4KB | ⭐⭐⭐ |
| PROGMEM | ~500B | ⭐ |
| Reducir DRAW_DIST | ~120B | ⭐ |
| **TOTAL** | **~157KB** | **De 32KB → 5KB RAM!** |

---

## 🎮 MEJORAS DE JUGABILIDAD

### 11. AÑADIR POWER-UPS (Turbo, Escudo)
### 12. SISTEMA DE DAÑOS AL COCHE
### 13. CHECKPOINT MARKERS
### 14. EFECTOS DE PARTÍCULAS (Humo, Chispas)
### 15. MÚSICA Y EFECTOS DE SONIDO (I2S DAC)

---

## 🚀 PRIORIDAD DE IMPLEMENTACIÓN

1. ✅ **CRÍTICO**: PSRAM para sprite (5 min)
2. ⭐ **ALTO**: Reducir segmentos (1 min)
3. ⭐ **MEDIO**: Optimizar struct (30 min)
4. 💡 **VISUAL**: Velocímetro circular (1 hora)
5. 💡 **VISUAL**: Sistema de vueltas (30 min)
6. ⚡ **PERFORMANCE**: Dual core (2 horas)

---

**¿Por dónde empezamos?** 🏁
