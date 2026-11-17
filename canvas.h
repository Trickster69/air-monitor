// Мини-Canvas 2D поверх кадра gx (RGB888): пути (линии, дуги, кривые Безье, эллипсы, скругления),
// заливка со сглаживанием по площади, обводка (ширина, концы butt/round, пунктир), градиенты (линейный/радиальный),
// globalAlpha, перенос/поворот, обрезка (прямоугольник или путь), тень-свечение (shadowBlur), текст из атласа шрифтов.
// Цель — повторить то, что рисуют HTML-дизайны (dizain_ish/*.html), с теми же числами в коде экранов.
#pragma once
#include "core.h"
#include "assets_fonts.h"

namespace cvs {
using namespace gx;

// ---------- цвет и краски ----------
struct Col { float r, g, b, a; };                               // r,g,b 0..255, a 0..1
constexpr Col hex(uint32_t h, float a = 1) { return {(float)((h >> 16) & 255), (float)((h >> 8) & 255), (float)(h & 255), a}; }
constexpr Col rgba(float r, float g, float b, float a) { return {r, g, b, a}; }
constexpr Col withA(Col c, float a) { return {c.r, c.g, c.b, a}; }

struct Paint {
  uint8_t type = 0;                                              // 0 цвет, 1 линейный, 2 радиальный
  Col c{0, 0, 0, 1};
  float x0 = 0, y0 = 0, r0 = 0, x1 = 0, y1 = 0, r1 = 0;           // в координатах пользователя (как в canvas)
  float stp[10]; Col stc[10]; int ns = 0;
  Paint() {}
  Paint(Col cc) : type(0), c(cc) {}
  // как addColorStop: точки сортируются по положению (при равных — в порядке добавления)
  Paint& stop(float p, Col cc) {
    if (ns >= 10) return *this;
    int i = ns; while (i > 0 && stp[i - 1] > p) { stp[i] = stp[i - 1]; stc[i] = stc[i - 1]; i--; }
    stp[i] = p; stc[i] = cc; ns++; return *this;
  }
};
inline Paint solid(Col c) { return Paint(c); }
inline Paint linear(float x0, float y0, float x1, float y1) { Paint p; p.type = 1; p.x0 = x0; p.y0 = y0; p.x1 = x1; p.y1 = y1; return p; }
inline Paint radial(float x0, float y0, float r0, float x1, float y1, float r1) { Paint p; p.type = 2; p.x0 = x0; p.y0 = y0; p.r0 = r0; p.x1 = x1; p.y1 = y1; p.r1 = r1; return p; }

// ---------- состояние ----------
struct Mat { float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; };
struct State { Mat m; float alpha = 1; float cx0 = 0, cy0 = 0, cx1 = W, cy1 = H; bool mask = false; };
inline State st;
inline State stack_[8];
inline int sp_ = 0;
inline uint8_t* maskBuf = nullptr;      // обрезка путём: покрытие 0..255 на весь экран
inline uint8_t* maskSave[8];
// тень (свечение)
inline float shadowBlur = 0; inline Col shadowCol{0, 0, 0, 0};

inline void save() { if (sp_ < 8) { stack_[sp_] = st; maskSave[sp_] = nullptr; if (st.mask && maskBuf) { maskSave[sp_] = (uint8_t*)GX_ALLOC(W * H); memcpy(maskSave[sp_], maskBuf, W * H); } sp_++; } }
inline void restore() {
  if (!sp_) return; sp_--; bool hadMask = st.mask; st = stack_[sp_];
  if (maskSave[sp_]) { memcpy(maskBuf, maskSave[sp_], W * H); free(maskSave[sp_]); maskSave[sp_] = nullptr; }
  (void)hadMask;
}
inline void resetState() { st = State(); sp_ = 0; shadowBlur = 0; }
inline void translate(float x, float y) { st.m.e += st.m.a * x + st.m.c * y; st.m.f += st.m.b * x + st.m.d * y; }
inline void rotate(float r) {
  float cs = cosf(r), sn = sinf(r); Mat m = st.m;
  st.m.a = m.a * cs + m.c * sn; st.m.b = m.b * cs + m.d * sn; st.m.c = -m.a * sn + m.c * cs; st.m.d = -m.b * sn + m.d * cs;
}
inline void setTransform(float a, float b, float c, float d, float e, float f) { st.m = {a, b, c, d, e, f}; }
static inline void tx(float x, float y, float& X, float& Y) { X = st.m.a * x + st.m.c * y + st.m.e; Y = st.m.b * x + st.m.d * y + st.m.f; }
static inline void itx(float X, float Y, float& x, float& y) {   // экран -> пользователь
  const Mat& m = st.m; float det = m.a * m.d - m.b * m.c; float dx = X - m.e, dy = Y - m.f;
  x = (m.d * dx - m.c * dy) / det; y = (-m.b * dx + m.a * dy) / det;
}

// ---------- краска в точке ----------
static inline void stopsAt(const Paint& p, float t, float& r, float& g, float& b, float& a) {
  if (t <= p.stp[0]) { r = p.stc[0].r; g = p.stc[0].g; b = p.stc[0].b; a = p.stc[0].a; return; }
  for (int i = 1; i < p.ns; i++) if (t <= p.stp[i]) {
    float span = p.stp[i] - p.stp[i - 1]; float k = span > 0 ? (t - p.stp[i - 1]) / span : 1;
    const Col &A = p.stc[i - 1], &B = p.stc[i];
    r = A.r + (B.r - A.r) * k; g = A.g + (B.g - A.g) * k; b = A.b + (B.b - A.b) * k; a = A.a + (B.a - A.a) * k; return;
  }
  const Col& L = p.stc[p.ns - 1]; r = L.r; g = L.g; b = L.b; a = L.a;
}
inline float conicT(float px_, float py_, float x0, float y0, float r0, float x1, float y1, float r1) {
  float cdx = x1 - x0, cdy = y1 - y0, dr = r1 - r0, pdx = px_ - x0, pdy = py_ - y0;
  float a = cdx * cdx + cdy * cdy - dr * dr, b = pdx * cdx + pdy * cdy + r0 * dr, c = pdx * pdx + pdy * pdy - r0 * r0;
  if (fabsf(a) < 1e-6f) return b != 0 ? c / (2 * b) : 0;
  float disc = b * b - a * c; if (disc < 0) disc = 0;
  float s = sqrtf(disc), ta = (b + s) / a, tb = (b - s) / a, t = ta > tb ? ta : tb;
  if (r0 + t * dr < 0) t = ta > tb ? tb : ta;
  return t;
}
// цвет краски в центре пикселя (X,Y) экрана; a 0..1
static inline void paintAt(const Paint& p, float X, float Y, float& r, float& g, float& b, float& a) {
  if (p.type == 0) { r = p.c.r; g = p.c.g; b = p.c.b; a = p.c.a; return; }
  float x, y; itx(X, Y, x, y);
  float t;
  if (p.type == 1) { float dx = p.x1 - p.x0, dy = p.y1 - p.y0, L = dx * dx + dy * dy; t = L > 0 ? ((x - p.x0) * dx + (y - p.y0) * dy) / L : 0; }
  else t = conicT(x, y, p.x0, p.y0, p.r0, p.x1, p.y1, p.r1);
  stopsAt(p, clampf(t, 0, 1), r, g, b, a);
}

// ---------- путь ----------
constexpr int MAXP = 3000, MAXS = 64;
inline float PX[MAXP], PY[MAXP];
inline int nP = 0;
inline int subStart[MAXS]; inline bool subClosed[MAXS]; inline int nSub = 0;
inline void beginPath() { nP = 0; nSub = 0; }
inline void addPt(float x, float y) { if (nP < MAXP) { tx(x, y, PX[nP], PY[nP]); nP++; } }
inline void moveTo(float x, float y) { if (nSub < MAXS) { subStart[nSub] = nP; subClosed[nSub] = false; nSub++; } addPt(x, y); }
inline void lineTo(float x, float y) { if (!nSub) { moveTo(x, y); return; } addPt(x, y); }
inline void closePath() { if (nSub) subClosed[nSub - 1] = true; }
inline void curUser(float& x, float& y) {   // последняя точка в координатах пользователя
  itx(PX[nP - 1], PY[nP - 1], x, y);
}
inline void bezierCurveTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
  float x0, y0; if (!nP) { moveTo(c1x, c1y); } curUser(x0, y0);
  float len = hypotf(c1x - x0, c1y - y0) + hypotf(c2x - c1x, c2y - c1y) + hypotf(x - c2x, y - c2y);
  float sc = sqrtf(fabsf(st.m.a * st.m.d - st.m.b * st.m.c));
  int n = (int)ceilf(len * sc / 2.f); if (n < 2) n = 2; if (n > 48) n = 48;
  for (int i = 1; i <= n; i++) {
    float t = (float)i / n, u = 1 - t;
    lineTo(u * u * u * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * x, u * u * u * y0 + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * y);
  }
}
inline void quadraticCurveTo(float cx, float cy, float x, float y) {
  float x0, y0; if (!nP) moveTo(cx, cy); curUser(x0, y0);
  bezierCurveTo(x0 + 2.f / 3 * (cx - x0), y0 + 2.f / 3 * (cy - y0), x + 2.f / 3 * (cx - x), y + 2.f / 3 * (cy - y), x, y);
}
inline void arc(float cx, float cy, float r, float a0, float a1, bool ccw = false) {
  float sweep = a1 - a0;
  if (!ccw) { if (sweep >= 2 * PI_F) sweep = 2 * PI_F; else { while (sweep < 0) sweep += 2 * PI_F; } }
  else { if (-sweep >= 2 * PI_F) sweep = -2 * PI_F; else { while (sweep > 0) sweep -= 2 * PI_F; } }
  float sc = sqrtf(fabsf(st.m.a * st.m.d - st.m.b * st.m.c)), R = r * sc;
  float dth = R > 0.2f ? 2 * acosf(fmaxf(-1.f, 1 - 0.08f / R)) : 1.f;
  int n = (int)ceilf(fabsf(sweep) / dth); if (n < 2) n = 2; if (n > 200) n = 200;
  for (int i = 0; i <= n; i++) {
    float a = a0 + sweep * i / n, x = cx + cosf(a) * r, y = cy + sinf(a) * r;
    if (i == 0 && !nSub) moveTo(x, y); else lineTo(x, y);
  }
}
inline void ellipse(float cx, float cy, float rx, float ry, float rot, float a0, float a1) {
  float cr = cosf(rot), sr = sinf(rot); int n = 48;
  float sweep = a1 - a0; if (sweep > 2 * PI_F) sweep = 2 * PI_F;
  for (int i = 0; i <= n; i++) {
    float a = a0 + sweep * i / n, ex = cosf(a) * rx, ey = sinf(a) * ry;
    float x = cx + ex * cr - ey * sr, y = cy + ex * sr + ey * cr;
    if (i == 0 && !nSub) moveTo(x, y); else lineTo(x, y);
  }
}
inline void circle(float cx, float cy, float r) { beginPath(); arc(cx, cy, r, 0, 7); }
// скруглённый прямоугольник как rr() в дизайнах (arcTo по углам)
inline void rrect(float x, float y, float w, float h, float r) {
  beginPath();
  if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
  moveTo(x + r, y);
  arc(x + w - r, y + r, r, -PI_F / 2, 0);
  arc(x + w - r, y + h - r, r, 0, PI_F / 2);
  arc(x + r, y + h - r, r, PI_F / 2, PI_F);
  arc(x + r, y + r, r, PI_F, 1.5f * PI_F);
  closePath();
}
inline void rect(float x, float y, float w, float h) { moveTo(x, y); lineTo(x + w, y); lineTo(x + w, y + h); lineTo(x, y + h); closePath(); }

// ---------- растеризация покрытия ----------
inline float* acc = nullptr;           // накопитель площади (W+2)*H
inline uint8_t* cov = nullptr;         // покрытие 0..255, W*H
inline int bx0, by0, bx1, by1;         // область последнего покрытия
inline void ensureBufs() {
  if (!acc) acc = (float*)GX_ALLOC((W + 2) * H * sizeof(float));
  if (!cov) cov = (uint8_t*)GX_ALLOC(W * H);
}
static inline void accLine(float x0, float y0, float x1, float y1, int ox, int oy, int bw, int bh) {
  if (y0 == y1) return;
  float dir = 1; if (y0 > y1) { dir = -1; float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
  x0 -= ox; x1 -= ox; y0 -= oy; y1 -= oy;
  const float dxdy = (x1 - x0) / (y1 - y0);
  float x = x0;
  int ys = y0 < 0 ? 0 : (int)y0; if (y0 < 0) x -= y0 * dxdy;
  const int ye = (int)fminf((float)bh, ceilf(y1));
  const int stride = bw + 2;
  for (int y = ys; y < ye; y++) {
    float* row = acc + y * stride;
    float dy = fminf((float)(y + 1), y1) - fmaxf((float)y, y0);
    float xn = x + dxdy * dy, d = dy * dir;
    float xa = x < xn ? x : xn, xb = x < xn ? xn : x;
    xa = clampf(xa, 0, (float)bw); xb = clampf(xb, 0, (float)bw);
    float xaf = floorf(xa); int xai = (int)xaf; float xbc = ceilf(xb); int xbi = (int)xbc;
    if (xbi <= xai + 1) {
      float xmf = 0.5f * (x + xn) - xaf; xmf = clampf(xmf, 0, 1);
      row[xai] += d - d * xmf; row[xai + 1] += d * xmf;
    } else {
      float s = 1.f / (xb - xa), x0f = xa - xaf, a0 = 0.5f * s * (1 - x0f) * (1 - x0f), x1f = xb - xbc + 1, am = 0.5f * s * x1f * x1f;
      row[xai] += d * a0;
      if (xbi == xai + 2) row[xai + 1] += d * (1 - a0 - am);
      else {
        float a1 = s * (1.5f - x0f); row[xai + 1] += d * (a1 - a0);
        for (int xi = xai + 2; xi < xbi - 1; xi++) row[xi] += d * s;
        float a2 = a1 + (xbi - xai - 3) * s; row[xbi - 1] += d * (1 - a2 - am);
      }
      row[xbi] += d * am;
    }
    x = xn;
  }
}
// покрытие заливки текущего пути -> cov (в пределах bx0..bx1, by0..by1)
inline bool fillCoverage() {
  ensureBufs();
  if (!nP) return false;
  float mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
  for (int i = 0; i < nP; i++) { mnx = fminf(mnx, PX[i]); mxx = fmaxf(mxx, PX[i]); mny = fminf(mny, PY[i]); mxy = fmaxf(mxy, PY[i]); }
  bx0 = (int)floorf(fmaxf(mnx, st.cx0)); by0 = (int)floorf(fmaxf(mny, st.cy0)); bx1 = (int)ceilf(fminf(mxx, st.cx1)); by1 = (int)ceilf(fminf(mxy, st.cy1));
  if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; if (bx1 > W) bx1 = W; if (by1 > H) by1 = H;
  if (bx0 >= bx1 || by0 >= by1) return false;
  const int bw = bx1 - bx0, bh = by1 - by0, stride = bw + 2;
  // края слева от области переносим на её край (площадь сохраняется)
  const int ox = bx0, oy = by0;
  for (int y = 0; y < bh; y++) memset(acc + y * stride, 0, stride * sizeof(float));
  for (int s = 0; s < nSub; s++) {
    int a = subStart[s], b = s + 1 < nSub ? subStart[s + 1] : nP;
    if (b - a < 2) continue;
    for (int i = a; i < b - 1; i++) accLine(PX[i], PY[i], PX[i + 1], PY[i + 1], ox, oy, bw, bh);
    accLine(PX[b - 1], PY[b - 1], PX[a], PY[a], ox, oy, bw, bh);   // заливка всегда замыкает
  }
  for (int y = 0; y < bh; y++) {
    float sum = 0; const float* row = acc + y * stride; uint8_t* c = cov + (by0 + y) * W + bx0;
    for (int x = 0; x < bw; x++) { sum += row[x]; float v = fabsf(sum); c[x] = v >= 1 ? 255 : (uint8_t)(v * 255.f + .5f); }
  }
  return true;
}

// обводка: объединение «капсул» отрезков (покрытие = максимум), концы butt/round
enum { CAP_BUTT, CAP_ROUND };
inline float dash[4]; inline int nDash = 0;
inline void setLineDash(float a, float b) { if (a <= 0) nDash = 0; else { dash[0] = a; dash[1] = b; nDash = 2; } }
static inline void segCov(float ax, float ay, float bx, float by, float lw, bool capA, bool capB, int cap) {
  const float hw = lw / 2, reach = (lw >= 1 ? hw + 0.5f : 1.f);
  int x0 = (int)floorf(fminf(ax, bx) - reach), x1 = (int)ceilf(fmaxf(ax, bx) + reach), y0 = (int)floorf(fminf(ay, by) - reach), y1 = (int)ceilf(fmaxf(ay, by) + reach);
  if (x0 < bx0) x0 = bx0; if (y0 < by0) y0 = by0; if (x1 > bx1) x1 = bx1; if (y1 > by1) y1 = by1;
  const float dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy, L = sqrtf(L2), iL = L > 0 ? 1.f / L : 0;
  for (int y = y0; y < y1; y++) {
    uint8_t* c = cov + y * W;
    for (int x = x0; x < x1; x++) {
      const float px_ = x + .5f - ax, py_ = y + .5f - ay;
      const float s = (px_ * dx + py_ * dy) * iL;          // вдоль отрезка
      float d, endk = 1;
      if (s < 0) { if (capA && cap == CAP_BUTT) { d = fabsf(px_ * dy - py_ * dx) * iL; endk = clampf(0.5f + s, 0, 1); } else d = sqrtf(px_ * px_ + py_ * py_); }
      else if (s > L) { if (capB && cap == CAP_BUTT) { d = fabsf(px_ * dy - py_ * dx) * iL; endk = clampf(0.5f + (L - s), 0, 1); } else { float qx = x + .5f - bx, qy = y + .5f - by; d = sqrtf(qx * qx + qy * qy); } }
      else d = L > 0 ? fabsf(px_ * dy - py_ * dx) * iL : sqrtf(px_ * px_ + py_ * py_);
      if (d >= reach) continue;
      float v = lw >= 1 ? clampf(hw - d + 0.5f, 0, 1) : clampf(1.f - d, 0, 1) * lw;
      v *= endk;
      int iv = (int)(v * 255.f + .5f); if (iv > c[x]) c[x] = iv;
    }
  }
}
inline bool strokeCoverage(float lw, int cap) {
  ensureBufs(); if (!nP) return false;
  const float sc = sqrtf(fabsf(st.m.a * st.m.d - st.m.b * st.m.c)); lw *= sc;
  float mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
  for (int i = 0; i < nP; i++) { mnx = fminf(mnx, PX[i]); mxx = fmaxf(mxx, PX[i]); mny = fminf(mny, PY[i]); mxy = fmaxf(mxy, PY[i]); }
  const float g = lw / 2 + 1.5f;
  bx0 = (int)floorf(fmaxf(mnx - g, st.cx0)); by0 = (int)floorf(fmaxf(mny - g, st.cy0)); bx1 = (int)ceilf(fminf(mxx + g, st.cx1)); by1 = (int)ceilf(fminf(mxy + g, st.cy1));
  if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; if (bx1 > W) bx1 = W; if (by1 > H) by1 = H;
  if (bx0 >= bx1 || by0 >= by1) return false;
  for (int y = by0; y < by1; y++) memset(cov + y * W + bx0, 0, bx1 - bx0);
  for (int s = 0; s < nSub; s++) {
    int a = subStart[s], b = s + 1 < nSub ? subStart[s + 1] : nP;
    bool closed = subClosed[s];
    if (b - a < 2) continue;
    if (!nDash) {
      for (int i = a; i < b - 1; i++) segCov(PX[i], PY[i], PX[i + 1], PY[i + 1], lw, !closed && i == a, !closed && i == b - 2, cap);
      if (closed) segCov(PX[b - 1], PY[b - 1], PX[a], PY[a], lw, false, false, cap);
    } else {   // пунктир: режем по длине, каждый штрих — отрезки с плоскими концами
      const float da = dash[0] * sc, db = dash[1] * sc; float pos = 0; bool on = true; float left = da;
      for (int i = a; i < b - 1; i++) {
        float x0 = PX[i], y0 = PY[i], x1 = PX[i + 1], y1 = PY[i + 1], L = hypotf(x1 - x0, y1 - y0), u = 0;
        while (u < L) {
          float step = fminf(left, L - u);
          if (on) { float t0 = u / L, t1 = (u + step) / L; segCov(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1, lw, true, true, CAP_BUTT); }
          u += step; left -= step;
          if (left <= 1e-4f) { on = !on; left = on ? da : db; }
        }
        pos += L;
      }
    }
  }
  return true;
}

// размытие покрытия (тень): 3 прохода блочного размытия ≈ гаусс с sigma = blur/2
inline uint8_t* blurA = nullptr; inline uint16_t* blurT = nullptr;
inline void boxBlurH(uint8_t* src, uint8_t* dst, int x0, int x1, int y0, int y1, int r) {
  for (int y = y0; y < y1; y++) {
    const uint8_t* s = src + y * W; uint8_t* d = dst + y * W; int sum = 0, n = 2 * r + 1; const int inv = 65536 / n;
    for (int k = -r; k <= r; k++) { int xx = x0 + k; sum += (xx >= x0 && xx < x1) ? s[xx] : 0; }
    for (int x = x0; x < x1; x++) {
      d[x] = (uint8_t)((sum * inv) >> 16);
      int xo = x - r, xi = x + r + 1;
      sum += (xi < x1 ? s[xi] : 0) - (xo >= x0 ? s[xo] : 0);
    }
  }
}
inline void boxBlurV(uint8_t* src, uint8_t* dst, int x0, int x1, int y0, int y1, int r) {
  for (int x = x0; x < x1; x++) {
    int sum = 0, n = 2 * r + 1; const int inv = 65536 / n;
    for (int k = -r; k <= r; k++) { int yy = y0 + k; sum += (yy >= y0 && yy < y1) ? src[yy * W + x] : 0; }
    for (int y = y0; y < y1; y++) {
      dst[y * W + x] = (uint8_t)((sum * inv) >> 16);
      int yo = y - r, yi = y + r + 1;
      sum += (yi < y1 ? src[yi * W + x] : 0) - (yo >= y0 ? src[yo * W + x] : 0);
    }
  }
}
// рисует тень от текущего покрытия cov (область расширяется на 3 sigma)
inline void drawShadow(float shapeAlpha) {
  if (shadowBlur <= 0 || shadowCol.a <= 0) return;
  if (!blurA) blurA = (uint8_t*)GX_ALLOC(W * H);
  const float sigma = shadowBlur / 2;
  int r = (int)floorf(sigma * sqrtf(12.f / 3 + 1) / 2 + 0.5f); if (r < 1) r = 1;   // радиус блока для 3 проходов
  const int g = 3 * r + 1;
  int x0 = bx0 - g, x1 = bx1 + g, y0 = by0 - g, y1 = by1 + g;
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  // копия покрытия в расширенную область
  for (int y = y0; y < y1; y++) { uint8_t* d = blurA + y * W; for (int x = x0; x < x1; x++) d[x] = (y >= by0 && y < by1 && x >= bx0 && x < bx1) ? cov[y * W + x] : 0; }
  static uint8_t* tmp = nullptr; if (!tmp) tmp = (uint8_t*)GX_ALLOC(W * H);
  for (int i = 0; i < 3; i++) { boxBlurH(blurA, tmp, x0, x1, y0, y1, r); boxBlurV(tmp, blurA, x0, x1, y0, y1, r); }
  const float ka = shadowCol.a * shapeAlpha * st.alpha;
  const int R = (int)(shadowCol.r + .5f), G = (int)(shadowCol.g + .5f), B = (int)(shadowCol.b + .5f);
  for (int y = y0; y < y1; y++) {
    if (y < st.cy0 || y >= st.cy1) continue;
    for (int x = x0; x < x1; x++) {
      int v = blurA[y * W + x]; if (!v) continue;
      if (x < st.cx0 || x >= st.cx1) continue;
      float a = v / 255.f * ka; if (st.mask) a *= maskBuf[y * W + x] / 255.f;
      blend(px(x, y), R, G, B, (int)(a * 255.f + .5f));
    }
  }
}
// закрасить покрытие краской (с учётом globalAlpha и обрезки).
// Градиенты: цвета — из таблицы на 256 шагов, положение в градиенте считается шагами по строке (без деления на пиксель).
inline uint8_t LUTc[257][4];
inline void buildLUT(const Paint& p) {
  for (int i = 0; i <= 256; i++) { float r, g, b, a; stopsAt(p, i / 256.f, r, g, b, a); LUTc[i][0] = (uint8_t)(r + .5f); LUTc[i][1] = (uint8_t)(g + .5f); LUTc[i][2] = (uint8_t)(b + .5f); LUTc[i][3] = (uint8_t)(a * 255.f + .5f); }
}
inline void composite(const Paint& p) {
  const int cy0 = (int)fmaxf((float)by0, st.cy0), cy1 = (int)fminf((float)by1, st.cy1);
  const int cx0 = (int)fmaxf((float)bx0, st.cx0), cx1 = (int)fminf((float)bx1, st.cx1);
  if (cx0 >= cx1 || cy0 >= cy1) return;
  if (p.type == 0) {
    const int R = (int)(p.c.r + .5f), G = (int)(p.c.g + .5f), B = (int)(p.c.b + .5f);
    const int ka = (int)(p.c.a * st.alpha * 256.f + .5f);
    for (int y = cy0; y < cy1; y++) {
      const uint8_t* c = cov + y * W; uint8_t* d = px(cx0, y);
      for (int x = cx0; x < cx1; x++, d += 3) {
        int v = c[x]; if (!v) continue;
        if (st.mask) { v = v * maskBuf[y * W + x] / 255; if (!v) continue; }
        const int A = (v * ka) >> 8; if (A > 0) blend(d, R, G, B, A);
      }
    }
    return;
  }
  buildLUT(p);
  // экран -> пользователь: u = ia*X + ic*Y + ie, v = ib*X + id*Y + if_
  const Mat& m = st.m; const float det = m.a * m.d - m.b * m.c;
  const float ia = m.d / det, ic = -m.c / det, ib = -m.b / det, id = m.a / det;
  const float ie = -(ia * m.e + ic * m.f), if_ = -(ib * m.e + id * m.f);
  const int ka = (int)(st.alpha * 256.f + .5f);
  const bool conc = p.type == 2 && p.x0 == p.x1 && p.y0 == p.y1;
  float LA = 0, LB = 0, LC = 0;                           // линейный: t = LA*u + LB*v + LC
  if (p.type == 1) { float dx = p.x1 - p.x0, dy = p.y1 - p.y0, L = dx * dx + dy * dy; if (L > 0) { LA = dx / L; LB = dy / L; LC = -(p.x0 * dx + p.y0 * dy) / L; } }
  const float invR = (conc && p.r1 != p.r0) ? 1.f / (p.r1 - p.r0) : 0;
  for (int y = cy0; y < cy1; y++) {
    const uint8_t* c = cov + y * W; uint8_t* d = px(cx0, y);
    float u = ia * (cx0 + .5f) + ic * (y + .5f) + ie, v = ib * (cx0 + .5f) + id * (y + .5f) + if_;
    for (int x = cx0; x < cx1; x++, d += 3, u += ia, v += ib) {
      int cv_ = c[x]; if (!cv_) continue;
      if (st.mask) { cv_ = cv_ * maskBuf[y * W + x] / 255; if (!cv_) continue; }
      float t;
      if (p.type == 1) t = LA * u + LB * v + LC;
      else if (conc) t = (sqrtf((u - p.x0) * (u - p.x0) + (v - p.y0) * (v - p.y0)) - p.r0) * invR;
      else t = conicT(u, v, p.x0, p.y0, p.r0, p.x1, p.y1, p.r1);
      int ti = (int)(t * 256.f + .5f); if (ti < 0) ti = 0; if (ti > 256) ti = 256;
      const uint8_t* q = LUTc[ti];
      const int A = (((cv_ * q[3]) >> 8) * ka) >> 8;   // ≈ cov*alpha*globalAlpha
      if (A > 0) blend(d, q[0], q[1], q[2], A > 255 ? 255 : A);
    }
  }
}
inline float paintAlphaHint(const Paint& p) { return p.type == 0 ? p.c.a : 1.f; }
inline void fill(const Paint& p) { if (fillCoverage()) { drawShadow(paintAlphaHint(p)); composite(p); } }
inline void stroke(const Paint& p, float lw, int cap = CAP_BUTT) { if (strokeCoverage(lw, cap)) { drawShadow(paintAlphaHint(p)); composite(p); } }
inline void fillRect(float x, float y, float w, float h, const Paint& p) { beginPath(); rect(x, y, w, h); fill(p); }
// обрезка: прямоугольник (в экранных координатах после переноса) или текущий путь
inline void clipRect(float x, float y, float w, float h) {
  float X0, Y0, X1, Y1; tx(x, y, X0, Y0); tx(x + w, y + h, X1, Y1);
  st.cx0 = fmaxf(st.cx0, fminf(X0, X1)); st.cy0 = fmaxf(st.cy0, fminf(Y0, Y1)); st.cx1 = fminf(st.cx1, fmaxf(X0, X1)); st.cy1 = fminf(st.cy1, fmaxf(Y0, Y1));
}
inline void clipPath() {
  if (!maskBuf) maskBuf = (uint8_t*)GX_ALLOC(W * H);
  if (!fillCoverage()) { st.cx1 = st.cx0; st.cy1 = st.cy0; return; }
  // маска нужна только внутри новой рамки обрезки (за её пределами ничего не рисуется)
  for (int y = by0; y < by1; y++)
    for (int x = bx0; x < bx1; x++) {
      const int v = cov[y * W + x];
      maskBuf[y * W + x] = st.mask ? maskBuf[y * W + x] * v / 255 : v;
    }
  st.mask = true;
  st.cx0 = fmaxf(st.cx0, (float)bx0); st.cy0 = fmaxf(st.cy0, (float)by0); st.cx1 = fminf(st.cx1, (float)bx1); st.cy1 = fminf(st.cy1, (float)by1);
}

// ---------- быстрые фигуры (формулой, без буфера покрытия) ----------
static inline bool clipOk(int x, int y) { return x >= st.cx0 && x < st.cx1 && y >= st.cy0 && y < st.cy1 && x >= 0 && x < W && y >= 0 && y < H; }
static inline void putA(int x, int y, int R, int G, int B, float a) {
  if (a <= 0.002f || !clipOk(x, y)) return;
  if (st.mask) a *= maskBuf[y * W + x] / 255.f;
  blend(px(x, y), R, G, B, (int)(a * 255.f + .5f));
}
// круг: заливка
inline void fastCircle(float cx, float cy, float r, Col c) {
  float X, Y; tx(cx, cy, X, Y);
  const float ka = c.a * st.alpha; const int R = (int)(c.r + .5f), G = (int)(c.g + .5f), B = (int)(c.b + .5f);
  const int x0 = (int)floorf(X - r - 1), x1 = (int)ceilf(X + r + 1), y0 = (int)floorf(Y - r - 1), y1 = (int)ceilf(Y + r + 1);
  const float lim = (r + 0.5f) * (r + 0.5f);
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
    const float dx = x + .5f - X, dy = y + .5f - Y, d2 = dx * dx + dy * dy; if (d2 >= lim) continue;
    float v = clampf(r - sqrtf(d2) + 0.5f, 0, 1); if (r < 0.5f) v *= r * 2;
    putA(x, y, R, G, B, ka * v);
  }
}
// окружность: обводка
inline void fastRing(float cx, float cy, float r, float lw, Col c) {
  float X, Y; tx(cx, cy, X, Y);
  const float ka = c.a * st.alpha; const int R = (int)(c.r + .5f), G = (int)(c.g + .5f), B = (int)(c.b + .5f);
  const float hw = lw / 2, reach = lw >= 1 ? hw + 0.5f : 1.f, ro = r + reach;
  const int x0 = (int)floorf(X - ro - 1), x1 = (int)ceilf(X + ro + 1), y0 = (int)floorf(Y - ro - 1), y1 = (int)ceilf(Y + ro + 1);
  const float lo2 = fmaxf(0, r - reach) * fmaxf(0, r - reach), hi2 = ro * ro;
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
    const float dx = x + .5f - X, dy = y + .5f - Y, d2 = dx * dx + dy * dy; if (d2 < lo2 || d2 >= hi2) continue;
    const float d = fabsf(sqrtf(d2) - r);
    const float v = lw >= 1 ? clampf(hw - d + 0.5f, 0, 1) : clampf(1.f - d, 0, 1) * lw;
    putA(x, y, R, G, B, ka * v);
  }
}
// скруглённый прямоугольник по строкам: на каждую строку — один корень, середина строки сплошная.
// Границы строки берутся по её центру, по вертикали — доля строки внутри фигуры (как площадное сглаживание).
static inline bool rrSpan(float X, float Y, float w, float h, float r, float yc, float& L, float& R) {
  if (yc < Y || yc > Y + h) return false;
  float inset = 0;
  if (yc < Y + r) { const float d = Y + r - yc; inset = r - sqrtf(fmaxf(0, r * r - d * d)); }
  else if (yc > Y + h - r) { const float d = yc - (Y + h - r); inset = r - sqrtf(fmaxf(0, r * r - d * d)); }
  L = X + inset; R = X + w - inset; return R > L;
}
static inline float spanCov(int x, float L, float R) { return fmaxf(0.f, fminf(x + 1.f, R) - fmaxf((float)x, L)); }
// заливка краской (цвет или вертикальный/любой градиент — через composite) и/или обводка
inline void fastRRect(float x, float y, float w, float h, float r, const Paint* fp, const Col* sc_, float lw = 1) {
  float X, Y; tx(x, y, X, Y);
  if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
  if (fp) {
    ensureBufs();
    bx0 = (int)floorf(X); by0 = (int)floorf(Y); bx1 = (int)ceilf(X + w); by1 = (int)ceilf(Y + h);
    if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; if (bx1 > W) bx1 = W; if (by1 > H) by1 = H;
    for (int yy = by0; yy < by1; yy++) {
      uint8_t* c = cov + yy * W;
      const float vf = fmaxf(0.f, fminf(yy + 1.f, Y + h) - fmaxf((float)yy, Y));
      float L, R;
      if (vf <= 0 || !rrSpan(X, Y, w, h, r, fminf(fmaxf(yy + .5f, Y), Y + h), L, R)) { memset(c + bx0, 0, bx1 - bx0); continue; }
      const int full = (int)(vf * 255.f + .5f);
      for (int xx = bx0; xx < bx1; xx++) {
        float cx = (xx >= L + 1 && xx + 1 <= R - 1) ? 1.f : spanCov(xx, L, R);
        c[xx] = cx >= 1 ? full : (uint8_t)(cx * vf * 255.f + .5f);
      }
    }
    composite(*fp);
  }
  if (sc_) {   // обводка = фигура, раздутая на lw/2, минус фигура, ужатая на lw/2
    const Col c = *sc_; const float ka = c.a * st.alpha; const int Rc = (int)(c.r + .5f), Gc = (int)(c.g + .5f), Bc = (int)(c.b + .5f);
    const float hw = lw / 2;
    const float oX = X - hw, oY = Y - hw, oW = w + lw, oH = h + lw, oR = r + hw;
    const float iX = X + hw, iY = Y + hw, iW = w - lw, iH = h - lw, iR = fmaxf(0, r - hw);
    int y0 = (int)floorf(oY), y1 = (int)ceilf(oY + oH);
    if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
    for (int yy = y0; yy < y1; yy++) {
      if (yy < st.cy0 || yy >= st.cy1) continue;
      const float yc = yy + .5f;
      const float vo = fmaxf(0.f, fminf(yy + 1.f, oY + oH) - fmaxf((float)yy, oY));
      const float vi = fmaxf(0.f, fminf(yy + 1.f, iY + iH) - fmaxf((float)yy, iY));
      float oL, oRr, iL = 0, iRr = 0;
      if (!rrSpan(oX, oY, oW, oH, oR, fminf(fmaxf(yc, oY), oY + oH), oL, oRr)) continue;
      const bool hasIn = vi > 0 && iW > 0 && iH > 0 && rrSpan(iX, iY, iW, iH, iR, fminf(fmaxf(yc, iY), iY + iH), iL, iRr);
      int xa = (int)floorf(oL), xb = (int)ceilf(oRr);
      auto doPx = [&](int xx) {
        float v = spanCov(xx, oL, oRr) * vo - (hasIn ? spanCov(xx, iL, iRr) * vi : 0.f);
        if (v > 0.002f) putA(xx, yy, Rc, Gc, Bc, ka * v);
      };
      if (hasIn && vi >= 0.999f && vo >= 0.999f) {   // средние строки: только левый и правый края
        const int la = (int)ceilf(iL) + 1, ra = (int)floorf(iRr) - 1;
        for (int xx = xa; xx < la && xx < xb; xx++) doPx(xx);
        for (int xx = ra > la ? ra : la; xx < xb; xx++) doPx(xx);
      } else for (int xx = xa; xx < xb; xx++) doPx(xx);
    }
  }
}

// мягкий ореол (радиальный градиент от центра) — профиль считаем один раз на радиус
inline void halo2(float x, float y, float r, Col c) {
  struct HC { float r; uint8_t* a; int n; };
  static HC cache[6]; static int nc = 0;
  HC* h = nullptr; for (int i = 0; i < nc; i++) if (cache[i].r == r) h = &cache[i];
  if (!h && nc < 6) {
    h = &cache[nc++]; h->r = r; h->n = (int)ceilf(r) * 2 + 2; h->a = (uint8_t*)GX_ALLOC(h->n * h->n);
    const float o = h->n / 2.f;
    for (int yy = 0; yy < h->n; yy++) for (int xx = 0; xx < h->n; xx++) {
      const float dx = xx + .5f - o, dy = yy + .5f - o, d = sqrtf(dx * dx + dy * dy);
      const float t = clampf((d - 1) / (r - 1), 0, 1), edge = clampf(r - d + 0.5f, 0, 1);
      h->a[yy * h->n + xx] = (uint8_t)(0.22f * (1 - t) * edge * 255.f + .5f);
    }
  }
  if (!h) return;
  float X, Y; tx(x, y, X, Y);
  const int ox = (int)floorf(X - h->n / 2.f + .5f), oy = (int)floorf(Y - h->n / 2.f + .5f);
  const int R = (int)(c.r + .5f), G = (int)(c.g + .5f), B = (int)(c.b + .5f);
  const int ka = (int)(st.alpha * 256.f + .5f);
  for (int yy = 0; yy < h->n; yy++) {
    const int Yp = oy + yy; if (Yp < st.cy0 || Yp >= st.cy1 || Yp < 0 || Yp >= H) continue;
    for (int xx = 0; xx < h->n; xx++) {
      const int Xp = ox + xx; if (Xp < st.cx0 || Xp >= st.cx1 || Xp < 0 || Xp >= W) continue;
      const int a = (h->a[yy * h->n + xx] * ka) >> 8; if (a) blend(px(Xp, Yp), R, G, B, a);
    }
  }
}

// ---------- свечение формулой (быстро) ----------
// Гауссово размытие (как shadowBlur в Chrome, sigma = blur/2) для дуги-обводки и прямоугольника:
// размытая полоса = разность функций нормального распределения от расстояний до краёв.
inline float PHI[257];   // Ф(z) для z = -4..4, шаг 1/32
inline void initPhi() { static bool ok = false; if (ok) return; for (int i = 0; i <= 256; i++) PHI[i] = 0.5f * (1 + erff((i / 32.f - 4) / 1.41421356f)); ok = true; }
static inline float Phi(float z) { float u = (z + 4) * 32; if (u <= 0) return 0; if (u >= 256) return 1; int i = (int)u; float f = u - i; return PHI[i] + (PHI[i + 1] - PHI[i]) * f; }
static inline void glowPx(int x, int y, float a, int R, int G, int B) {
  if (a <= 0.002f || y < st.cy0 || y >= st.cy1 || x < st.cx0 || x >= st.cx1) return;
  blend(px(x, y), R, G, B, (int)(a * 255.f + .5f));
}
// тень дуги-обводки (центр, радиус, углы, толщина), концы butt; координаты — пользователя (только перенос)
inline void arcShadow(float cx, float cy, float r, float a0, float a1, float lw) {
  if (shadowBlur <= 0 || shadowCol.a <= 0) return;
  initPhi();
  float X0, Y0; tx(cx, cy, X0, Y0);
  const float sg = shadowBlur / 2, is = 1.f / sg, hw = lw / 2, ext = hw + 3 * sg;
  const float ka = shadowCol.a * st.alpha; const int R = (int)(shadowCol.r + .5f), G = (int)(shadowCol.g + .5f), B = (int)(shadowCol.b + .5f);
  const float rin = fmaxf(0, r - ext), rout = r + ext, rin2 = rin * rin, rout2 = rout * rout;
  // рамка вокруг дуги: концы + крайние точки по осям, если они внутри дуги
  float mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
  auto addP = [&](float ang, float rad) { float px_ = X0 + cosf(ang) * rad, py_ = Y0 + sinf(ang) * rad; mnx = fminf(mnx, px_); mxx = fmaxf(mxx, px_); mny = fminf(mny, py_); mxy = fmaxf(mxy, py_); };
  addP(a0, rin); addP(a0, rout); addP(a1, rin); addP(a1, rout);
  for (int k = -4; k <= 4; k++) { float ang = k * PI_F / 2; if (ang > a0 && ang < a1) addP(ang, rout); }
  int x0 = (int)floorf(mnx - 3 * sg), x1 = (int)ceilf(mxx + 3 * sg), y0 = (int)floorf(mny - 3 * sg), y1 = (int)ceilf(mxy + 3 * sg);
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  // концы дуги — радиальные прямые; расстояние до них со знаком (внутрь дуги > 0)
  const float n0x = -sinf(a0), n0y = cosf(a0), n1x = sinf(a1), n1y = -cosf(a1);
  const bool small = (a1 - a0) < PI_F;
  const float mcx = cosf((a0 + a1) / 2), mcy = sinf((a0 + a1) / 2);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      const float dx = x + .5f - X0, dy = y + .5f - Y0, d2 = dx * dx + dy * dy;
      if (d2 < rin2 || d2 > rout2) continue;
      const float e0 = dx * n0x + dy * n0y, e1 = dx * n1x + dy * n1y;
      if (small && (e0 < -3 * sg || e1 < -3 * sg)) continue;
      // по другую сторону центра — не наша половина
      if (small && dx * mcx + dy * mcy < 0) continue;
      const float d = sqrtf(d2) - r;
      const float pr = Phi((d + hw) * is) - Phi((d - hw) * is);
      const float pe = Phi(e0 * is) * Phi(e1 * is);
      glowPx(x, y, ka * pr * pe, R, G, B);
    }
}
// обводка дуги (концы butt) формулой: покрытие = по радиусу × по концам; цвет с прозрачностью
inline void arcStroke(float cx, float cy, float r, float a0, float a1, float lw, Col c) {
  if (a1 - a0 <= 0.0005f) return;
  float X0, Y0; tx(cx, cy, X0, Y0);
  const float hw = lw / 2, rin = fmaxf(0, r - hw - 1), rout = r + hw + 1, rin2 = rin * rin, rout2 = rout * rout;
  float mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
  auto addP = [&](float ang, float rad) { float px_ = X0 + cosf(ang) * rad, py_ = Y0 + sinf(ang) * rad; mnx = fminf(mnx, px_); mxx = fmaxf(mxx, px_); mny = fminf(mny, py_); mxy = fmaxf(mxy, py_); };
  addP(a0, rin); addP(a0, rout); addP(a1, rin); addP(a1, rout);
  for (int k = -4; k <= 4; k++) { float ang = k * PI_F / 2; if (ang > a0 && ang < a1) addP(ang, rout); }
  int x0 = (int)floorf(mnx - 1), x1 = (int)ceilf(mxx + 1), y0 = (int)floorf(mny - 1), y1 = (int)ceilf(mxy + 1);
  if (x0 < st.cx0) x0 = (int)st.cx0; if (y0 < st.cy0) y0 = (int)st.cy0; if (x1 > st.cx1) x1 = (int)st.cx1; if (y1 > st.cy1) y1 = (int)st.cy1;
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  const float n0x = -sinf(a0), n0y = cosf(a0), n1x = sinf(a1), n1y = -cosf(a1);
  const float mcx = cosf((a0 + a1) / 2), mcy = sinf((a0 + a1) / 2);
  const bool small = (a1 - a0) < PI_F;
  const float ka = c.a * st.alpha; const int R = (int)(c.r + .5f), G = (int)(c.g + .5f), B = (int)(c.b + .5f);
  for (int y = y0; y < y1; y++) {
    uint8_t* d = px(x0, y);
    for (int x = x0; x < x1; x++, d += 3) {
      const float dx = x + .5f - X0, dy = y + .5f - Y0, d2 = dx * dx + dy * dy;
      if (d2 < rin2 || d2 > rout2) continue;
      if (small && dx * mcx + dy * mcy < 0) continue;
      const float e0 = dx * n0x + dy * n0y, e1 = dx * n1x + dy * n1y;
      if (small && (e0 < -0.5f || e1 < -0.5f)) continue;
      const float rad = clampf(hw - fabsf(sqrtf(d2) - r) + 0.5f, 0, 1);
      const float cv_ = rad * clampf(0.5f + e0, 0, 1) * clampf(0.5f + e1, 0, 1);
      if (cv_ > 0) blend(d, R, G, B, (int)(ka * cv_ * 255.f + .5f));
    }
  }
}

// одиночный отрезок с плоскими концами — сразу в кадр (риски, короткие линии)
inline void lineStroke(float ax, float ay, float bx_, float by_, float lw, Col c) {
  float AX, AY, BX, BY; tx(ax, ay, AX, AY); tx(bx_, by_, BX, BY);
  const float hw = lw / 2, reach = (lw >= 1 ? hw + 0.5f : 1.f);
  int x0 = (int)floorf(fminf(AX, BX) - reach), x1 = (int)ceilf(fmaxf(AX, BX) + reach), y0 = (int)floorf(fminf(AY, BY) - reach), y1 = (int)ceilf(fmaxf(AY, BY) + reach);
  if (x0 < st.cx0) x0 = (int)st.cx0; if (y0 < st.cy0) y0 = (int)st.cy0; if (x1 > st.cx1) x1 = (int)st.cx1; if (y1 > st.cy1) y1 = (int)st.cy1;
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  const float dx = BX - AX, dy = BY - AY, L = sqrtf(dx * dx + dy * dy); if (L <= 0) return;
  const float ux = dx / L, uy = dy / L;
  const float ka = c.a * st.alpha; const int R = (int)(c.r + .5f), G = (int)(c.g + .5f), B = (int)(c.b + .5f);
  for (int y = y0; y < y1; y++) {
    uint8_t* d = px(x0, y);
    for (int x = x0; x < x1; x++, d += 3) {
      const float px_ = x + .5f - AX, py_ = y + .5f - AY;
      const float sAl = px_ * ux + py_ * uy, dist = fabsf(px_ * uy - py_ * ux);
      if (dist >= reach || sAl < -0.5f || sAl > L + 0.5f) continue;
      float v = lw >= 1 ? clampf(hw - dist + 0.5f, 0, 1) : clampf(1.f - dist, 0, 1) * lw;
      v *= clampf(0.5f + sAl, 0, 1) * clampf(0.5f + L - sAl, 0, 1);
      if (v > 0) blend(d, R, G, B, (int)(ka * v * 255.f + .5f));
    }
  }
}

// тень прямоугольника (координаты пользователя, только перенос)
inline void rectShadow(float x, float y, float w, float h) {
  if (shadowBlur <= 0 || shadowCol.a <= 0) return;
  initPhi();
  float X, Y; tx(x, y, X, Y);
  const float sg = shadowBlur / 2, is = 1.f / sg, m = 3 * sg;
  const float ka = shadowCol.a * st.alpha; const int R = (int)(shadowCol.r + .5f), G = (int)(shadowCol.g + .5f), B = (int)(shadowCol.b + .5f);
  int x0 = (int)floorf(X - m), x1 = (int)ceilf(X + w + m), y0 = (int)floorf(Y - m), y1 = (int)ceilf(Y + h + m);
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
  float colA[64]; const int nx = x1 - x0 < 64 ? x1 - x0 : 64;
  for (int i = 0; i < nx; i++) { float c = x0 + i + .5f; colA[i] = Phi((c - X) * is) - Phi((c - X - w) * is); }
  for (int yy = y0; yy < y1; yy++) {
    const float c = yy + .5f, ry = Phi((c - Y) * is) - Phi((c - Y - h) * is);
    for (int i = 0; i < nx; i++) glowPx(x0 + i, yy, ka * ry * colA[i], R, G, B);
  }
}

// ---------- текст ----------
enum { AL_LEFT, AL_CENTER, AL_RIGHT };
static inline uint32_t nextCp(const char*& s) {
  uint8_t c = *s++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) { uint32_t v = (c & 0x1F) << 6; v |= (*s++ & 0x3F); return v; }
  if ((c & 0xF0) == 0xE0) { uint32_t v = (c & 0x0F) << 12; v |= (*s++ & 0x3F) << 6; v |= (*s++ & 0x3F); return v; }
  uint32_t v = (c & 0x07) << 18; v |= (*s++ & 0x3F) << 12; v |= (*s++ & 0x3F) << 6; v |= (*s++ & 0x3F); return v;
}
static inline const Glyph* findG(const Font& f, uint32_t cp) {
  int lo = 0, hi = f.n - 1;
  while (lo <= hi) { int m = (lo + hi) >> 1; if (f.gl[m].cp == cp) return &f.gl[m]; if (f.gl[m].cp < cp) lo = m + 1; else hi = m - 1; }
  return nullptr;
}
static inline float kernOf(const Font& f, uint32_t a, uint32_t b) {   // пары отсортированы — двоичный поиск
  int lo = 0, hi = f.nk - 1;
  while (lo <= hi) {
    int m = (lo + hi) >> 1; const Kern& k = f.kern[m];
    if (k.a == a && k.b == b) return k.k;
    if (k.a < a || (k.a == a && k.b < b)) lo = m + 1; else hi = m - 1;
  }
  return 0;
}
// «₂» (CO₂) в мелком шрифте высотой 4 px не читается: берём обычную «2» и опускаем на 2 px
static inline const Glyph* glyphOf(const Font& f, uint32_t cp, int& dy) {
  dy = 0;
  if (cp == 0x2082 && f.asc < 18) { dy = 2; return findG(f, '2'); }
  return findG(f, cp);
}
// ширина как measureText (с letterSpacing после каждой буквы и кернингом пар цифр, как в canvas)
inline float measure(const Font& f, const char* s, float ls = 0) {
  float w = 0; uint32_t prev = 0;
  while (*s) { uint32_t cp = nextCp(s); int dy_; const Glyph* g = glyphOf(f, cp, dy_); if (prev && f.nk) w += kernOf(f, prev, cp); if (g) w += g->adv; w += ls; prev = cp; }
  return w;
}
inline void fillText(const Font& f, const char* s, float x, float y, Col c, int align = AL_LEFT, float ls = 0) {
  float w = measure(f, s, ls);
  if (align == AL_CENTER) x -= w / 2; else if (align == AL_RIGHT) x -= w;
  float X, Y; tx(x, y, X, Y);   // только перенос (поворотов у текста в дизайнах нет)
  // мелкий текст ставим ровно на сетку пикселей: при дробном положении строка размазывалась на два ряда и на экране выглядела мутной
  if (f.asc < 18) { X = floorf(X + 0.5f); Y = floorf(Y + 0.5f); }
  const float ga = c.a * st.alpha; if (ga <= 0.002f) return;
  const int R = (int)(c.r + .5f), G = (int)(c.g + .5f), B = (int)(c.b + .5f);
  // толщина сглаживания зависит от цвета (как в Chrome): для цветов дизайна — своя таблица, иначе — по яркости
  const uint8_t *L0, *L1; float lf = 0;
  {
    int best = -1; float bd = 1e9;
    for (int i = 6; i < (int)(sizeof(TEXT_LUT) / 256); i++) { float d = fabsf(c.r - TEXT_LUT_RGB[i][0]) + fabsf(c.g - TEXT_LUT_RGB[i][1]) + fabsf(c.b - TEXT_LUT_RGB[i][2]); if (d < bd) { bd = d; best = i; } }
    if (best >= 0 && bd < 6) { L0 = L1 = TEXT_LUT[best]; }
    else {
      const float Lum = clampf((0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.f, 0, 1) * 5;
      const int li = Lum >= 5 ? 4 : (int)Lum; lf = Lum - li; L0 = TEXT_LUT[li]; L1 = TEXT_LUT[li + 1];
    }
  }
  int iy = (int)floorf(Y); const float fy = Y - iy; const int f1 = (int)(fy * 256 + .5f), f0 = 256 - f1;
  float pen = X; uint32_t prev = 0;
  while (*s) {
    uint32_t cp = nextCp(s); int gdy; const Glyph* g = glyphOf(f, cp, gdy);
    if (prev && f.nk) pen += kernOf(f, prev, cp);
    prev = cp;
    if (!g) { pen += ls; continue; }
    float fl = floorf(pen); int phase = (int)((pen - fl) * 4 + 0.5f); int ix = (int)fl; if (phase == 4) { phase = 0; ix++; }
    const GPhase& P = g->ph[phase];
    const uint8_t* a = f.data + P.off;
    for (int ry = 0; ry <= P.h; ry++) {
      const int YY = iy + P.oy + ry + gdy; if (YY < st.cy0 || YY >= st.cy1 || YY < 0 || YY >= H) continue;
      for (int rx = 0; rx < P.w; rx++) {
        const int XX = ix + P.ox + rx; if (XX < st.cx0 || XX >= st.cx1 || XX < 0 || XX >= W) continue;
        int v = 0;
        if (ry < P.h) v += a[ry * P.w + rx] * f0;
        if (ry > 0 && f1) v += a[(ry - 1) * P.w + rx] * f1;
        if (!v) continue;
        const int av = v >> 8; const float lv = L0[av] + (L1[av] - L0[av]) * lf;
        float A = lv / 255.f * ga; if (st.mask) A *= maskBuf[YY * W + XX] / 255.f;
        blend(px(XX, YY), R, G, B, (int)(A * 255.f + .5f));
      }
    }
    pen += g->adv + ls;
  }
}

}  // namespace cvs
