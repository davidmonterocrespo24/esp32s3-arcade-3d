/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE RENDERIZADO DEL AUTO DEL JUGADOR
  Renderizado 3D con malla OBJ + textura RGB565
  ═══════════════════════════════════════════════════════════════
*/

#include "render_player.h"
#include "rendering.h"
#include "config.h"
#include "colors.h"
#include "physics.h"
#include "track.h"
#include "utils.h"
#include "car2_mesh.h"
#include "car2_texture.h"

// ---------------------------------------------------------------------------
// Rasterizador de triángulo texturizado (affine mapping, scanline)
// ---------------------------------------------------------------------------
static void drawTexturedTri(
    float ax, float ay, float au, float av,
    float bx, float by, float bu, float bv,
    float cx, float cy, float cu, float cv,
    float light)
{
  // Ordenar vértices por Y ascendente (a <= b <= c)
  if (ay > by) { float t; t=ax;ax=bx;bx=t; t=ay;ay=by;by=t; t=au;au=bu;bu=t; t=av;av=bv;bv=t; }
  if (ay > cy) { float t; t=ax;ax=cx;cx=t; t=ay;ay=cy;cy=t; t=au;au=cu;cu=t; t=av;av=cv;cv=t; }
  if (by > cy) { float t; t=bx;bx=cx;cx=t; t=by;by=cy;cy=t; t=bu;bu=cu;cu=t; t=bv;bv=cv;cv=t; }

  // Descartar triángulos degenerados
  if (cy - ay < 0.5f) return;

  int yA = (int)ay, yB = (int)by, yC = (int)cy;
  yA = max(yA, 0); yC = min(yC, SCR_H - 1);
  if (yA > yC) return;

  for (int y = yA; y <= yC; y++) {
    // Calcular bordes izq/der y UVs interpolados
    float t_AC = (cy - ay > 0.001f) ? (float)(y - ay) / (cy - ay) : 0.0f;
    float xAC = ax + t_AC * (cx - ax);
    float uAC = au + t_AC * (cu - au);
    float vAC = av + t_AC * (cv - av);

    float xL, xR, uL, uR, vL, vR;
    if (y < yB) {
      float t_AB = (yB != (int)ay) ? (float)(y - ay) / (by - ay) : 0.0f;
      float xAB = ax + t_AB * (bx - ax);
      float uAB = au + t_AB * (bu - au);
      float vAB = av + t_AB * (bv - av);
      if (xAC < xAB) { xL=xAC; xR=xAB; uL=uAC; uR=uAB; vL=vAC; vR=vAB; }
      else            { xL=xAB; xR=xAC; uL=uAB; uR=uAC; vL=vAB; vR=vAC; }
    } else {
      float t_BC = (yC != yB) ? (float)(y - by) / (cy - by) : 0.0f;
      float xBC = bx + t_BC * (cx - bx);
      float uBC = bu + t_BC * (cu - bu);
      float vBC = bv + t_BC * (cv - bv);
      if (xAC < xBC) { xL=xAC; xR=xBC; uL=uAC; uR=uBC; vL=vAC; vR=vBC; }
      else            { xL=xBC; xR=xAC; uL=uBC; uR=uAC; vL=vBC; vR=vAC; }
    }

    if (xR - xL < 0.5f) continue;   // scanline vacío
    if (xR - xL > 160.0f) continue; // scanline demasiado ancho = artefacto
    int x0 = max((int)xL, 0);
    int x1 = min((int)xR, SCR_W - 1);
    if (x0 > x1) continue;
    float dx = 1.0f / (xR - xL);

    for (int x = x0; x <= x1; x++) {
      float t = (x - xL) * dx;
      float u = uL + t * (uR - uL);
      float v = vL + t * (vR - vL);

      // Sample textura (clamp)
      int tx = (int)(u * (CAR2_TEX_W - 1));
      int ty = (int)((1.0f - v) * (CAR2_TEX_H - 1));  // flip V (OBJ es bottom-up)
      tx = max(0, min(tx, CAR2_TEX_W - 1));
      ty = max(0, min(ty, CAR2_TEX_H - 1));

#ifdef ARDUINO
      uint16_t texel = pgm_read_word(&car2_texture[ty * CAR2_TEX_W + tx]);
#else
      uint16_t texel = car2_texture[ty * CAR2_TEX_W + tx];
#endif

      // Aplicar iluminación simple (multiplicar canales)
      if (light < 0.99f) {
        uint8_t r = ((texel >> 11) & 0x1F);
        uint8_t g = ((texel >>  5) & 0x3F);
        uint8_t b = ( texel        & 0x1F);
        r = (uint8_t)(r * light);
        g = (uint8_t)(g * light);
        b = (uint8_t)(b * light);
        texel = (r << 11) | (g << 5) | b;
      }

      spr.drawPixel(x, y, texel);
    }
  }
}

// ---------------------------------------------------------------------------
// Proyectar y renderizar la malla Car2 completa
// ---------------------------------------------------------------------------
static void renderCar2Mesh(int centerX, int centerY,
                            float rotY, float pitch, float camDist, float fov)
{
  float cosY = cosf(rotY), sinY = sinf(rotY);
  float cosP = cosf(pitch), sinP = sinf(pitch);

  // Proyectar todos los vértices
  static float px[428], py[428], pz[428];

  for (int i = 0; i < car2_vert_count; i++) {
    float x = car2_verts[i].x;
    float y = car2_verts[i].y;
    float z = car2_verts[i].z;

    // Bajar el modelo para que las ruedas queden al fondo (Y suelo ~0)
    // Desplazar para que el centro visual quede en ~0.8 (entre ruedas y techo)
    y -= 0.5f;

    // Rotación Y (yaw) — negada para orientación correcta
    float rx = x * cosY - z * sinY;
    float ry = -y;   // Invertir Y: OBJ Y-up → pantalla Y-down
    float rz = x * sinY + z * cosY;

    // Rotación X (pitch de cámara)
    float fy = ry * cosP - rz * sinP;
    float fz = ry * sinP + rz * cosP;

    fz += camDist;
    pz[i] = fz;

    if (fz > 0.01f) {
      px[i] = centerX + (rx * fov) / fz;
      py[i] = centerY + (fy * fov) / fz;  // + porque Y ya está invertido
    } else {
      px[i] = -9999;
      py[i] = -9999;
    }
  }

  // Renderizar triángulos back-to-front (painter's algorithm — Z promedio)
  // Construir lista ordenable
  static int order[312];
  static float zdepth[312];
  int ntri = car2_tri_count;

  for (int t = 0; t < ntri; t++) {
    int i0 = car2_indices[t*3+0];
    int i1 = car2_indices[t*3+1];
    int i2 = car2_indices[t*3+2];
    zdepth[t] = (pz[i0] + pz[i1] + pz[i2]) / 3.0f;
    order[t] = t;
  }

  // Insertion sort (312 elementos — suficientemente rápido)
  for (int i = 1; i < ntri; i++) {
    float kd = zdepth[order[i]];
    int   ki = order[i];
    int j = i - 1;
    while (j >= 0 && zdepth[order[j]] < kd) {
      order[j+1] = order[j];
      j--;
    }
    order[j+1] = ki;
  }

  for (int ti = 0; ti < ntri; ti++) {
    int t  = order[ti];
    int i0 = car2_indices[t*3+0];
    int i1 = car2_indices[t*3+1];
    int i2 = car2_indices[t*3+2];

    if (pz[i0] < 0.01f || pz[i1] < 0.01f || pz[i2] < 0.01f) continue;

    float ax = px[i0], ay = py[i0];
    float bx = px[i1], by = py[i1];
    float cx = px[i2], cy = py[i2];

    // Descartar triángulos con vértices fuera de pantalla (evita rayas)
    const float MARGIN = 20.0f;
    if (ax < -MARGIN || ax > SCR_W+MARGIN || bx < -MARGIN || bx > SCR_W+MARGIN ||
        cx < -MARGIN || cx > SCR_W+MARGIN) continue;
    if (ay < -MARGIN || ay > SCR_H+MARGIN || by < -MARGIN || by > SCR_H+MARGIN ||
        cy < -MARGIN || cy > SCR_H+MARGIN) continue;

    // Backface culling (invertido porque Y está negado)
    float cross = (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
    if (cross <= 0) continue;

    // Iluminación por normal de cara (dot con luz fija desde arriba-frente)
    float nx = (by-ay)*(0) - (by-ay)*(cy-ay);  // simplificado: luz fija
    // Normal en espacio objeto
    float ex1 = car2_verts[i1].x - car2_verts[i0].x;
    float ey1 = car2_verts[i1].y - car2_verts[i0].y;
    float ez1 = car2_verts[i1].z - car2_verts[i0].z;
    float ex2 = car2_verts[i2].x - car2_verts[i0].x;
    float ey2 = car2_verts[i2].y - car2_verts[i0].y;
    float ez2 = car2_verts[i2].z - car2_verts[i0].z;
    float fnx = ey1*ez2 - ez1*ey2;
    float fny = ez1*ex2 - ex1*ez2;
    float fnz = ex1*ey2 - ey1*ex2;
    float fnlen = sqrtf(fnx*fnx + fny*fny + fnz*fnz);
    if (fnlen > 0.0001f) { fnx/=fnlen; fny/=fnlen; fnz/=fnlen; }
    // Luz desde arriba y ligeramente desde el frente
    float lx=0.0f, ly=0.7f, lz=-0.7f;
    float dot = fnx*lx + fny*ly + fnz*lz;
    float light = 0.35f + 0.65f * max(0.0f, dot);

    drawTexturedTri(
      ax, ay, car2_verts[i0].u, car2_verts[i0].v,
      bx, by, car2_verts[i1].u, car2_verts[i1].v,
      cx, cy, car2_verts[i2].u, car2_verts[i2].v,
      light);
  }
}

// ---------------------------------------------------------------------------
// API pública
// ---------------------------------------------------------------------------
void drawPlayerCar() {
  int centerX = SCR_CX;
  int centerY = SCR_H - 40;

  // Calcular pendiente real de la carretera promediando varios segmentos
  // para que el auto siga la inclinación suavemente (igual que la cámara)
  int segIdx = findSegIdx(position + playerZdist);
  const int SLOPE_SAMPLES = 6;
  float yStart = segments[segIdx].y;
  int farIdx   = (segIdx + SLOPE_SAMPLES) % TOTAL_SEGS;
  float yEnd   = segments[farIdx].y;
  float slope  = (yEnd - yStart) / (SEG_LEN * SLOPE_SAMPLES);
  float roadPitch = atanf(slope) * 0.20f;
  roadPitch = clampF(roadPitch, -0.25f, 0.25f);
  static float smoothPitch = 0.0f;
  smoothPitch += (roadPitch - smoothPitch) * 0.15f;

  float rotY  = playerX * 0.5f;
  // pitch base (cámara desde arriba) + inclinación de la carretera
  float pitch = 0.28f + smoothPitch;

  renderCar2Mesh(centerX, centerY, rotY, pitch, 6.5f, 130.0f);
}

void drawStartScreen(float time) {
  spr.fillSprite(TFT_BLACK);

  spr.fillRect(0, 100, SCR_W, 3, TFT_RED);
  spr.fillRect(0, 105, SCR_W, 3, TFT_WHITE);

  spr.setTextColor(TFT_RED);
  spr.setTextSize(3);
  spr.setCursor(22, 115);
  spr.print("OUTRUN ESP32");

  spr.setTextSize(2);
  spr.setTextColor(TFT_YELLOW);
  spr.setCursor(60, 145);
  spr.print("3D RACING");

  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE);
  spr.setCursor(30, 170);
  spr.print("LEFT / RIGHT buttons to steer");
  spr.setCursor(30, 185);
  spr.print("Car accelerates automatically");

  // Sombra
  spr.fillEllipse(SCR_CX, 95, 55, 18, rgb(15, 15, 15));

  // Auto girando en la pantalla de inicio
  renderCar2Mesh(SCR_CX, 75, time * 1.5f, 0.5f, 5.0f, 130.0f);

  spr.pushSprite(0, 0);
}

void drawCrashMessage() {
  spr.fillRect(SCR_CX - 70, SCR_CY - 15, 140, 30, TFT_BLACK);
  spr.drawRect(SCR_CX - 71, SCR_CY - 16, 142, 32, TFT_RED);
  spr.setTextSize(3);
  spr.setTextColor(TFT_RED, TFT_BLACK);
  spr.setCursor(SCR_CX - 55, SCR_CY - 8);
  spr.print("CRASH!");
}
