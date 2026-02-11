# 🏎️ OUTRUN ESP32 — Pseudo-3D Racing Game

Juego de carreras estilo OutRun con perspectiva pseudo-3D para **ESP32-S3** y pantalla **ILI9341 TFT 320×240**.

## Características

- Carretera con curvas y colinas (acumulación de dx + easeInOut)
- Niebla exponencial por distancia
- Tráfico con IA (6 autos)
- Objetos al lado de la carretera (pinos, palmeras, arbustos, rocas, postes)
- Fuerza centrífuga en curvas
- Ciclo día / atardecer / noche
- Detección de colisiones
- Cronómetro de vueltas con mejor tiempo
- HUD con velocímetro y barra de velocidad

## Hardware necesario

- ESP32-S3 Dev Module
- Pantalla ILI9341 TFT SPI 320×240
- 2 botones pulsadores (normalmente abiertos)
- Cables dupont

## Conexiones

### Pantalla LCD ILI9341

| Pin LCD     | GPIO ESP32-S3 |
|-------------|---------------|
| SCK         | **12**        |
| SDI (MOSI)  | **11**        |
| SDO (MISO)  | **13**        |
| CS           | **10**        |
| DC           | **9**         |
| RESET        | **8**         |
| VCC          | **3.3V**      |
| GND          | **GND**       |
| LED (BL)     | **39**        |

### Botones

| Botón       | GPIO ESP32-S3 | Conexión            |
|-------------|---------------|---------------------|
| IZQUIERDA   | **17**        | Botón → GND         |
| DERECHA     | **16**        | Botón → GND         |

> Los botones se conectan entre el GPIO y GND (sin resistencia externa).
> El código usa `INPUT_PULLUP`, así que la resistencia pull-up interna del ESP32 está activada.

### Diagrama de conexión

```
ESP32-S3                    ILI9341
────────                    ───────
GPIO 12  ──────────────►  SCK
GPIO 11  ──────────────►  SDI (MOSI)
GPIO 13  ◄──────────────  SDO (MISO)
GPIO 10  ──────────────►  CS
GPIO  9  ──────────────►  DC
GPIO  8  ──────────────►  RESET
GPIO 39  ──────────────►  LED (Backlight)
3.3V     ──────────────►  VCC
GND      ──────────────►  GND

GPIO 17  ──── [BTN IZQ] ──── GND
GPIO 16  ──── [BTN DER] ──── GND
```

## Librerías requeridas (Arduino IDE)

- `Adafruit GFX Library`
- `Adafruit ILI9341`
- `SPI` (incluida con ESP32)

Instalar desde **Sketch → Include Library → Manage Libraries** buscando "Adafruit ILI9341".

## Configuración en Arduino IDE

1. **Board:** ESP32S3 Dev Module
2. **USB CDC On Boot:** Enabled (para Serial Monitor)
3. **Flash Size:** 4MB o superior
4. **Partition Scheme:** Default
5. **Upload Speed:** 921600

## Controles

| Acción      | Control                        |
|-------------|--------------------------------|
| Acelerar    | Automático                     |
| Girar izq.  | Mantener botón GPIO 17         |
| Girar der.  | Mantener botón GPIO 16         |

> La sensibilidad del volante aumenta con la velocidad.
> Salir de la carretera reduce la velocidad.
> Chocar con tráfico u objetos causa un "CRASH" de 2 segundos.

## Archivo

- `car_racing_3d.ino` — Código completo del juego (~930 líneas)

## Créditos

Basado en técnicas de pseudo-3D de:
- [Lou's Pseudo 3D Page](http://www.extentofthejam.com/pseudo/)
- [Jake Gordon — JavaScript Racer](https://jakesgordon.com/writing/javascript-racer/)
 