// Общее ядро графики: кадр RGB888 в PSRAM, смешивание как в canvas (source-over),
// упорядоченный дизеринг Байера 4x4 в RGB565 — ровно как quant() в дизайнах.
// Файл без Arduino-зависимостей: тот же код собирается на ПК для проверки (pribor_tft/sim).
#pragma once
#pragma GCC optimize("O3")   // графика: скорость важнее размера (по умолчанию Arduino собирает с -Os)
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "lang.h"

#ifndef GX_ALLOC
#define GX_ALLOC(n) malloc(n)
#endif

namespace gx {

constexpr int W = 320, H = 240;
constexpr float PI_F = 3.14159265358979f;

// текущий кадр RGB888: указатели на строки (строки могут лежать в разных кусках быстрой внутренней памяти)
inline uint8_t* rows[H];
inline void setRows(uint8_t* buf) { for (int y = 0; y < H; y++) rows[y] = buf + y * W * 3; }
inline void copyFrom(const uint8_t* src, int y0 = 0, int y1 = H) { for (int y = y0; y < y1; y++) memcpy(rows[y], src + y * W * 3, W * 3); }
inline void copyTo(uint8_t* dst) { for (int y = 0; y < H; y++) memcpy(dst + y * W * 3, rows[y], W * 3); }

// ---------- дизеринг (как quant() в 03_weather.html) ----------
inline const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
// Узор порогов 16x16 «синий шум» (16 уровней). С матрицей Байера 4x4 на тёмных плавных фонах получались горизонтальные
// полоски (суммы порогов по строкам разные: 20, 36, 24, 40) — на экране они были видны как строчная развёртка (фото 16.11.2026).
inline const uint8_t DNOISE[256] = {15,0,7,14,1,13,11,10,5,8,13,10,15,8,13,9,7,12,10,2,6,3,4,1,15,12,0,5,2,0,6,4,1,5,4,13,9,15,8,13,2,4,7,14,8,12,10,14,11,15,8,2,7,0,12,6,7,11,9,3,11,4,7,3,8,0,6,14,11,5,2,10,0,15,1,13,1,15,2,10,3,13,9,3,4,15,9,14,4,12,6,5,7,9,6,14,12,7,1,11,8,12,1,5,8,2,9,11,3,12,0,5,2,10,5,13,0,6,3,13,11,14,0,14,2,10,14,8,15,13,3,7,15,10,9,1,7,4,10,5,8,6,3,6,8,0,11,5,2,4,12,15,2,12,6,15,0,13,12,1,9,3,14,9,13,2,8,5,8,11,1,9,4,10,4,11,15,5,7,1,11,6,13,0,4,14,3,11,7,14,1,6,2,12,10,4,14,1,10,14,9,10,6,13,2,5,9,12,8,0,14,7,5,9,4,7,2,5,0,8,13,0,15,3,13,6,3,12,1,15,3,12,14,11,15,9,4,11,7,10,2,11,9,4,10,8,6,0,7,3,1,6,3,12,1,5};
inline uint8_t d5[16][256], d6[16][256];
inline void initDither() {
  for (int k = 0; k < 16; k++) {
    double t = (k + 0.5) / 16.0;
    for (int v = 0; v < 256; v++) {
      int a = (int)floor(v * 31.0 / 255.0 + t); if (a > 31) a = 31;
      int b = (int)floor(v * 63.0 / 255.0 + t); if (b > 63) b = 63;
      d5[k][v] = a; d6[k][v] = b;
    }
  }
}
// прямоугольник кадра -> RGB565 (порядок байт родной, драйвер сам переставит)
inline void toRGB565(int x0, int y0, int w, int h, uint16_t* out) {
  for (int y = y0; y < y0 + h; y++) {
    const uint8_t* p = rows[y] + x0 * 3;
    const uint8_t* dn = DNOISE + (y & 15) * 16;
    for (int x = x0; x < x0 + w; x++, p += 3) {
      const int k = dn[x & 15];
      *out++ = (uint16_t)((d5[k][p[0]] << 11) | (d6[k][p[1]] << 5) | d5[k][p[2]]);
    }
  }
}

// ---------- смешивание ----------
static inline int div255(int v) { return (v + 128 + ((v + 128) >> 8)) >> 8; }
static inline uint8_t* px(int x, int y) { return rows[y] + x * 3; }
// цвет (r,g,b) с прозрачностью a 0..255 поверх
static inline void blend(uint8_t* p, int r, int g, int b, int a) {
  if (a <= 0) return;
  if (a >= 255) { p[0] = r; p[1] = g; p[2] = b; return; }
  p[0] = div255(r * a + p[0] * (255 - a));
  p[1] = div255(g * a + p[1] * (255 - a));
  p[2] = div255(b * a + p[2] * (255 - a));
}
// предумноженный цвет
static inline void blendPM(uint8_t* p, int rp, int gp, int bp, int a) {
  if (a <= 0 && rp <= 0 && gp <= 0 && bp <= 0) return;
  const int ia = 255 - a;
  int r = rp + div255(p[0] * ia), g = gp + div255(p[1] * ia), b = bp + div255(p[2] * ia);
  p[0] = r > 255 ? 255 : r; p[1] = g > 255 ? 255 : g; p[2] = b > 255 ? 255 : b;
}
// режим 'lighter' (сложение)
static inline void addPx(uint8_t* p, int r, int g, int b) {
  int a = p[0] + r, c = p[1] + g, d = p[2] + b;
  p[0] = a > 255 ? 255 : a; p[1] = c > 255 ? 255 : c; p[2] = d > 255 ? 255 : d;
}
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float easeOut3(float u) { u = clampf(u, 0, 1); return 1 - (1 - u) * (1 - u) * (1 - u); }

// прямоугольник с дробными краями (покрытие пикселя = площадь), как fillRect в canvas
inline void fillRectAA(float x, float y, float w, float h, int r, int g, int b, float alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  int x0 = (int)floorf(x), x1 = (int)ceilf(x + w), y0 = (int)floorf(y), y1 = (int)ceilf(y + h);
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  for (int yy = y0; yy < y1; yy++) {
    float cy = fminf(yy + 1.f, y + h) - fmaxf((float)yy, y);
    if (cy <= 0) continue;
    for (int xx = x0; xx < x1; xx++) {
      float cx = fminf(xx + 1.f, x + w) - fmaxf((float)xx, x);
      if (cx <= 0) continue;
      blend(px(xx, yy), r, g, b, (int)(alpha * cx * cy * 255.f + 0.5f));
    }
  }
}
// закрашенный круг со сглаженным краем
inline void fillCircleAA(float cx, float cy, float rad, int r, int g, int b, float alpha) {
  int x0 = (int)floorf(cx - rad - 1), x1 = (int)ceilf(cx + rad + 1), y0 = (int)floorf(cy - rad - 1), y1 = (int)ceilf(cy + rad + 1);
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++) {
      float d2 = (xx + .5f - cx) * (xx + .5f - cx) + (yy + .5f - cy) * (yy + .5f - cy);
      if (d2 >= (rad + 0.5f) * (rad + 0.5f)) continue;
      float c = clampf(rad - sqrtf(d2) + 0.5f, 0, 1);
      if (rad < 0.5f) c *= rad * 2;  // очень маленькие точки — меньше площадь
      if (c > 0) blend(px(xx, yy), r, g, b, (int)(alpha * c * 255.f + 0.5f));
    }
}
// окружность (обводка) толщиной lw
inline void strokeCircleAA(float cx, float cy, float rad, float lw, int r, int g, int b, float alpha) {
  float ro = rad + lw / 2, ri = rad - lw / 2;
  int x0 = (int)floorf(cx - ro - 1), x1 = (int)ceilf(cx + ro + 1), y0 = (int)floorf(cy - ro - 1), y1 = (int)ceilf(cy + ro + 1);
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++) {
      float d = sqrtf((xx + .5f - cx) * (xx + .5f - cx) + (yy + .5f - cy) * (yy + .5f - cy));
      float c = clampf(ro - d + 0.5f, 0, 1) * clampf(d - ri + 0.5f, 0, 1);
      if (c > 0) blend(px(xx, yy), r, g, b, (int)(alpha * c * 255.f + 0.5f));
    }
}
// отрезок с круглыми концами (как stroke + lineCap round)
inline void lineAA(float ax, float ay, float bx, float by, float lw, int r, int g, int b, float alpha) {
  float hw = lw / 2;
  int x0 = (int)floorf(fminf(ax, bx) - hw - 1), x1 = (int)ceilf(fmaxf(ax, bx) + hw + 1);
  int y0 = (int)floorf(fminf(ay, by) - hw - 1), y1 = (int)ceilf(fmaxf(ay, by) + hw + 1);
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  float dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
  const float iL2 = L2 > 0 ? 1.f / L2 : 0.f;
  const float reach = (lw >= 1 ? hw + 0.5f : 1.f), reach2 = reach * reach;   // дальше — пиксель не задет
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++) {
      float pxx = xx + .5f - ax, pyy = yy + .5f - ay;
      float t = clampf((pxx * dx + pyy * dy) * iL2, 0, 1);
      float ex = pxx - t * dx, ey = pyy - t * dy, e2 = ex * ex + ey * ey;
      if (e2 >= reach2) continue;
      float d = sqrtf(e2);
      float c = lw >= 1 ? clampf(hw - d + 0.5f, 0, 1) : clampf(1.f - d, 0, 1) * lw;   // тоньше 1 px — как волосяная линия Skia
      if (c > 0) blend(px(xx, yy), r, g, b, (int)(alpha * (c > 1 ? 1 : c) * 255.f + 0.5f));
    }
}

// ---------- спрайты ----------
// fmt 0: RGBA предумноженный; fmt 1: только альфа, цвет c1; fmt 2: альфа, слева от split — c1, справа — c2
struct Sprite {
  int16_t x, y, w, h;
  uint8_t fmt;
  uint32_t c1, c2;
  int16_t split;
  const uint8_t* data;
};
// рисует спрайт со сдвигом (dx, целый; dy — дробный) и общей прозрачностью ga; clipX — правая граница (дробная)
inline void drawSprite(const Sprite& s, int dx, float dy, float ga, float clipX = 1e9f) {
  if (ga <= 0.002f) return;
  const int gai = (int)(ga * 256.f + 0.5f);
  int iy = (int)floorf(dy);
  const float fy = dy - iy;
  const int f1 = (int)(fy * 256.f + 0.5f), f0 = 256 - f1;
  const int bpp = s.fmt == 0 ? 4 : 1;
  for (int ry = 0; ry <= s.h; ry++) {                 // строка экрана = s.y+iy+ry, берём строки спрайта ry-1 и ry
    const int Y = s.y + iy + ry;
    if (Y < 0 || Y >= H) continue;
    const uint8_t* rowA = (ry - 1 >= 0) ? s.data + (ry - 1) * s.w * bpp : nullptr;  // вес f1
    const uint8_t* rowB = (ry < s.h) ? s.data + ry * s.w * bpp : nullptr;          // вес f0
    if (!f1) rowA = nullptr;
    if (!rowA && !rowB) continue;
    for (int rx = 0; rx < s.w; rx++) {
      const int X = s.x + dx + rx;
      if (X < 0 || X >= W) continue;
      float cov = 1;
      if (X + 1 > clipX) { cov = clipX - X; if (cov <= 0) continue; }
      int r = 0, g = 0, b = 0, a = 0;
      if (s.fmt == 0) {
        if (rowB) { const uint8_t* q = rowB + rx * 4; r += q[0] * f0; g += q[1] * f0; b += q[2] * f0; a += q[3] * f0; }
        if (rowA) { const uint8_t* q = rowA + rx * 4; r += q[0] * f1; g += q[1] * f1; b += q[2] * f1; a += q[3] * f1; }
        if (!a) continue;
      } else {
        if (rowB) a += rowB[rx] * f0;
        if (rowA) a += rowA[rx] * f1;
        if (!a) continue;
        const uint32_t c = (s.fmt == 2 && rx + s.x >= s.split) ? s.c2 : s.c1;
        r = ((c >> 16) & 255) * a / 255; g = ((c >> 8) & 255) * a / 255; b = (c & 255) * a / 255;
      }
      int m = (int)(gai * cov);
      // r,g,b,a сейчас умножены на 256 (вес строк); переводим в 0..255 с учётом общей прозрачности
      blendPM(px(X, Y), (r * m) >> 16, (g * m) >> 16, (b * m) >> 16, (a * m) >> 16);
    }
  }
}

}  // namespace gx
