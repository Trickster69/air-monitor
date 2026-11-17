// Экран «Все показатели» с живыми иконками — дизайн dizain_ish/02_dashboard_icons.html (альфы — COLOR_FIX_v3).
// Данные: dash::setLive(значения, есть-ли-данные) — дальше плавно доезжают. Датчик молчит — прочерк и серая карточка.
// Средний ряд: VOC и NOx (оба с SGP41).
#pragma once
#include "canvas.h"
#include "data.h"

namespace dash {
using namespace cvs;

enum { K_T, K_H, K_C, K_V, K_N, K_P1, K_P, K_P10, NK };   // = app::M_*
constexpr float NI = -1e9f;
struct Mt { const char* lab; const char* sen; const char* u; bool dec; float g0, g1, m0, m1, s0, s1; const char* labEn; const char* uEn; };
inline const Mt MT[NK] = {
  {"ТЕМП.", nullptr, "°C", true, 19, 25, 16, 28, 14, 32, "TEMP", "°C"}, {"ВЛАЖН.", nullptr, "%", false, 40, 60, 30, 70, 0, 100, "HUMIDITY", "%"},
  {"CO₂", nullptr, "ppm", false, NI, 800, NI, 1200, 400, 2000, "CO₂", "ppm"}, {"VOC", "SGP41", "индекс", false, NI, 150, NI, 250, 0, 500, "VOC", "index"},
  {"NOx", "SGP41", "индекс", false, NI, 20, NI, 150, 0, 300, "NOx", "index"}, {"PM1", nullptr, nullptr, false, NI, 10, NI, 25, 0, 60, "PM1", nullptr},
  {"PM2.5", nullptr, nullptr, false, NI, 12, NI, 35, 0, 75, "PM2.5", nullptr}, {"PM10", nullptr, nullptr, false, NI, 45, NI, 100, 0, 150, "PM10", nullptr}};
inline const char* lab(int k) { return L(MT[k].lab, MT[k].labEn); }
inline const char* unit(int k) { return L(MT[k].u, MT[k].uEn); }
// сценарии для проверки на ПК: норма / сейчас / готовка
inline const float SC[3][NK] = {{22.4f, 48, 620, 98, 1, 4, 6, 9}, {22.8f, 52, 870, 112, 3, 5, 8, 12}, {24.6f, 68, 1350, 340, 180, 33, 46, 64}};
inline float val[NK], tgt[NK];
inline bool ok[NK] = {true, true, true, true, true, true, true, true};
inline float t0ms = 0;
inline uint32_t prof[12];
#ifndef GX_US
#define GX_US() 0
#endif
#define DP(i) { uint32_t _n = GX_US(); prof[i] += _n - _pt; _pt = _n; }
inline char hhmm[6] = "--:--"; inline char date[40] = "";

constexpr Col AM = hex(0xFFB54A), TL = hex(0x4FB0B8), RD = hex(0xE5483B), CR = hex(0xF4E7D4), NEU = hex(0x9A8F82);
static inline Col CRa(float a) { return withA(CR, a); }
static inline bool same(Col a, Col b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
static inline bool calm(Col c) { return same(c, TL) || same(c, NEU); }   // спокойная карточка (норма или нет данных)

struct Rng { uint32_t a; double operator()() { a += 0x6D2B79F5u; uint32_t t = (a ^ (a >> 15)) * (1u | a); t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t; return (double)(t ^ (t >> 14)) / 4294967296.0; } };
inline Rng rnd{42};
struct Bubble { float x, y, s, r; };
struct Dust { float x, y, vx, vy, p; int cls; };
inline Bubble bubbles[22];
inline Dust dust[150];
inline bool inited = false;
inline void initParticles() {
  rnd = Rng{42};
  for (auto& b : bubbles) { b.x = rnd(); b.y = rnd(); b.s = 0.4f + rnd() * 0.6f; b.r = 0.6f + rnd() * 1.2f; }
  for (int i = 0; i < 150; i++) { Dust& d = dust[i]; d.x = rnd(); d.y = rnd(); d.cls = i < 50 ? 0 : i < 100 ? 1 : 2; d.vx = (rnd() - 0.5f) * 0.02f; d.vy = (rnd() - 0.5f) * 0.02f; d.p = rnd() * 7; }
  inited = true;
}

// цвет зоны считается по настоящему значению датчика, а не по «доезжающему»: пока стрелка едет, готовый слой не пересобирается
inline Col col(int k) { if (!ok[k]) return NEU; const Mt& m = MT[k]; float v = tgt[k]; if (v >= m.g0 && v <= m.g1) return TL; if (v >= m.m0 && v <= m.m1) return AM; return RD; }
inline const char* word(Col c) { return same(c, TL) ? "" : same(c, NEU) ? L("НЕТ ДАННЫХ", "NO DATA") : same(c, AM) ? L("ВНИМАНИЕ", "WARNING") : L("ПЛОХО", "POOR"); }
inline float fr(int k) { return clampf((val[k] - MT[k].s0) / (MT[k].s1 - MT[k].s0), 0, 1); }
inline void fmt(int k, char* out) {
  if (!ok[k]) { strcpy(out, "−−"); return; }
  if (MT[k].dec) { snprintf(out, 16, "%.1f", val[k]); for (char* p = out; *p; p++) if (*p == '.' && !en()) *p = ','; }
  else snprintf(out, 16, "%d", (int)floorf(val[k] + 0.5f));
}

inline void setLive(const float v[NK], const bool o[NK]) { for (int k = 0; k < NK; k++) { ok[k] = o[k]; if (o[k]) tgt[k] = v[k]; } }
inline void setScenario(int i) { memcpy(tgt, SC[i], sizeof tgt); for (bool& o : ok) o = true; }
inline uint32_t rs = 4242;
inline float frnd() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) * (1.f / 16777216.f); }
inline void begin(float now) {
  if (!inited) initParticles();
  for (int k = 0; k < NK; k++) val[k] = MT[k].s0;   // появление: цифры, стрелки, столбик градусника, вода в капле, пузырьки — от начала шкалы к значению
  t0ms = now - 1000;                                // карточки сразу готовым слоем (дёшево), движется только живое поверх него
}
inline void step(float now, float dt) {
  const float kv = 1 - powf(0.95f, dt * 60);
  for (int k = 0; k < NK; k++) val[k] += (tgt[k] - val[k]) * kv;
  for (auto& b : bubbles) { b.y -= dt * 0.18f * b.s; if (b.y < 0) { b.y = 1; b.x = frnd(); } }
  for (auto& d : dust) {
    d.vx += (frnd() - 0.5f) * 0.004f; d.vy += (frnd() - 0.5f) * 0.004f - 0.0003f * d.cls; d.vx *= 0.98f; d.vy *= 0.98f;
    d.x = fmodf(d.x + d.vx * dt * 3 + 1, 1.f); d.y = fmodf(d.y + d.vy * dt * 3 + 1, 1.f);
  }
}

// ---------- части ----------
// Слои: MODE 0 — всё (появление); 1 — только неподвижное (готовый слой); 2 — только живое поверх готового слоя.
// Карточка в красной зоне пульсирует — её рисуем целиком каждый кадр (cdyn).
inline int MODE = 0; inline bool cdyn = false;
static inline bool S_() { return MODE == 0 || (MODE == 1 && !cdyn) || (MODE == 2 && cdyn); }
static inline bool D_() { return MODE != 1; }

inline void card(float x, float y, float w, float h, int k, float t) {
  if (!S_()) return;
  const Col c = col(k); const float pz = 0.5f + 0.5f * sinf(t * 4);
  const Paint f = solid(calm(c) ? CRa(0.10f) : withA(c, same(c, RD) ? 0.18f + 0.05f * pz : 0.18f));
  const Col sk = calm(c) ? CRa(0.30f) : withA(c, same(c, RD) ? 0.5f + 0.35f * pz : 0.5f);
  fastRRect(x, y, w, h, 8, &f, &sk, 0.7f);
  const Paint hl = linear(0, y, 0, y + 14).stop(0, CRa(0.05f)).stop(1, CRa(0));
  fastRRect(x + 0.5f, y + 0.5f, w - 1, 14, 7.5f, &hl, nullptr);
}
inline void label(float x, float y, int k, float right, bool wide = true) {   // wide — в карточке хватает места на «НЕТ ДАННЫХ»
  if (!S_()) return;
  const Mt& m = MT[k]; const Col c = col(k);
  fillText(F_ON85B, lab(k), x, y, CRa(0.75f), AL_LEFT, 1.4f);
  const float lw = measure(F_ON85B, lab(k), 1.4f);
  if (m.sen) fillText(F_JB85M, m.sen, x + lw + 3, y, CRa(0.60f));
  const char* wd = word(c);
  const float used = x + lw + (m.sen ? measure(F_JB85M, m.sen) + 3 : 0) + 5;   // слово состояния пишем, только если оно не наедет на подпись (цвет карточки и так его показывает)
  if (wd[0] && right > 0 && (wide || !same(c, NEU)) && right - measure(F_JB8B, wd, 0.8f) >= used) fillText(F_JB8B, wd, right, y, same(c, NEU) ? CRa(0.6f) : c, AL_RIGHT, 0.8f);
}
inline void bar(float x0, float x1, float y, int k) {
  const Mt& m = MT[k]; const Col c = col(k);
  auto X = [&](float q) { return x0 + (x1 - x0) * clampf((q - m.s0) / (m.s1 - m.s0), 0, 1); };
  if (S_()) {
    const float zs[5][2] = {{m.s0, m.m0}, {m.m0, m.g0}, {m.g0, m.g1}, {m.g1, m.m1}, {m.m1, m.s1}};
    const Col zc[5] = {RD, AM, TL, AM, RD};
    for (int z = 0; z < 5; z++) { float a = fmaxf(zs[z][0], m.s0), b = fminf(zs[z][1], m.s1); if (b > a) fillRect(X(a) + 0.5f, y, X(b) - X(a) - 1, 1.3f, solid(withA(zc[z], 0.3f))); }
  }
  if (D_() && ok[k]) {
    const float mx = X(val[k]);
    shadowCol = withA(c, 0.9f); shadowBlur = 4; rectShadow(mx - 0.6f, y - 2.5f, 1.2f, 6.3f); shadowBlur = 0;
    fillRect(mx - 0.6f, y - 2.5f, 1.2f, 6.3f, solid(CR));
  }
}
inline void halo(float x, float y, float r, Col c) { if (S_()) halo2(x, y, r, c); }

inline void thermo(float cx, float cy, float t) {
  const Col c = col(K_T); const float top = cy - 20, bot = cy + 10;
  halo(cx, cy + 4, 22, c);
  if (S_()) {
    { const Paint f = solid(rgba(12, 9, 7, 0.8f)); const Col sk = CRa(0.55f); fastRRect(cx - 4, top - 2, 8, bot - top + 6, 4, &f, &sk, 0.8f); }
    fastCircle(cx, bot + 6, 6.5f, rgba(12, 9, 7, 0.8f)); fastRing(cx, bot + 6, 6.5f, 0.8f, CRa(0.55f));
  }
  if (D_()) {
    const float lv = bot - (bot - top) * (ok[K_T] ? fr(K_T) : 0);
    shadowCol = withA(c, 0.8f); shadowBlur = 5;
    rectShadow(cx - 1.8f, lv, 3.6f, bot - lv + 4); shadowBlur = 0; fillRect(cx - 1.8f, lv, 3.6f, bot - lv + 4, solid(c));
    shadowBlur = 5; circle(cx, bot + 6, 4.6f); fill(solid(c)); shadowBlur = 0;
    fillRect(cx - 1.2f, lv + 1, 0.8f, bot - lv, solid(rgba(255, 255, 255, 0.35f)));
  }
  if (S_()) for (int j = 0; j < 5; j++) { float yy = top + 2 + j * (bot - top - 2) / 4; fillRect(cx + 6, yy, j % 2 ? 2 : 3.5f, 0.6f, solid(CRa(0.55f))); }
  if (D_() && ok[K_T] && val[K_T] > 25)
    for (int j = 0; j < 2; j++) {
      beginPath(); for (int s = 0; s < 10; s++) { float yy = top - 3 - s * 1.2f, xx = cx - 6 + j * 12 + sinf(t * 4 + s * 0.8f + j) * 1.3f; if (s) lineTo(xx, yy); else moveTo(xx, yy); }
      stroke(solid(withA(c, 0.5f)), 0.7f);
    }
}
inline void dropPath(float cx, float cy) {
  beginPath(); moveTo(cx, cy - 19); bezierCurveTo(cx + 6, cy - 9, cx + 13, cy - 1, cx + 13, cy + 6);
  arc(cx, cy + 6, 13, 0, PI_F); bezierCurveTo(cx - 13, cy - 1, cx - 6, cy - 9, cx, cy - 19); closePath();
}
inline void drop(float cx, float cy, float t) {
  const Col c = col(K_H); halo(cx, cy + 3, 22, c);
  if (S_()) { dropPath(cx, cy); fill(solid(rgba(12, 9, 7, 0.8f))); }
  if (!D_()) return;
  save(); dropPath(cx, cy); clipPath();
  const float lv = cy + 19 - 38 * (ok[K_H] ? fr(K_H) : 0);
  for (int L = 0; L < 2; L++) {
    beginPath(); moveTo(cx - 14, cy + 20);
    for (int x = -14; x <= 14; x++) lineTo(cx + x, lv + sinf(x * 0.35f + t * (2.2f + L) + L * 2) * 1.4f + L * 1.5f);
    lineTo(cx + 14, cy + 20); closePath(); fill(solid(L ? c : withA(c, 0.45f)));
  }
  beginPath(); ellipse(cx - 5, cy + 3, 1.6f, 4, 0.3f, 0, 2 * PI_F); closePath(); fill(solid(rgba(255, 255, 255, 0.25f)));
  restore();
  dropPath(cx, cy); stroke(solid(CRa(0.55f)), 0.8f);
}
inline void molecule(float cx, float cy, float t) {
  const Col c = col(K_C); halo(cx, cy, 24, c);
  if (!D_()) return;
  int n = ok[K_C] ? (int)floorf((val[K_C] - 400) / 60 + 0.5f) : 0; if (n < 3) n = 3; if (n > 22) n = 22;
  for (int i = 0; i < n; i++) {
    const Bubble& b = bubbles[i];
    const float x = cx - 17 + b.x * 34 + sinf(t * 2 + i) * 1.5f, y = cy - 20 + b.y * 42;
    fastRing(x, y, b.r, 0.6f, withA(c, 0.25f + 0.5f * (1 - b.y) * b.y * 2));
  }
  save(); translate(cx, cy); rotate(sinf(t * 0.8f) * 0.25f);
  for (int s = -1; s <= 1; s += 2) { beginPath(); moveTo(s * 3, -1.3f); lineTo(s * 8, -1.3f); moveTo(s * 3, 1.3f); lineTo(s * 8, 1.3f); stroke(solid(CRa(0.6f)), 0.8f); }
  auto ball = [&](float x, float r, Col cc) { circle(x, 0, r); fill(radial(x - r * 0.35f, -r * 0.35f, 0.3f, x, 0, r).stop(0, hex(0xFFF6E8)).stop(0.35f, cc).stop(1, hex(0x120c09))); };
  ball(-11, 5, c); ball(11, 5, c); ball(0, 3.8f, hex(0xB7A894));
  restore();
}
// струйки (NOx — газы от горения) и круги (VOC — летучая органика)
inline void wisps(int key, float cx, float cy, float t) {
  const Col c = col(key); const float k = 0.6f + (ok[key] ? fr(key) : 0) * 2.2f; halo(cx, cy, 20, c);
  if (!D_()) return;
  for (int j = 0; j < 3; j++) {
    beginPath();
    for (int s = 0; s <= 20; s++) { float u = s / 20.f, y = cy + 13 - u * 28, x = cx - 7 + j * 7 + sinf(u * 5 - t * 1.8f * k + j * 2) * (1.5f + u * 3 * k * 0.6f); if (s) lineTo(x, y); else moveTo(x, y); }
    stroke(linear(0, cy + 13, 0, cy - 15).stop(0, withA(c, 0.9f)).stop(1, withA(c, 0)), 1.3f, CAP_ROUND);
  }
  { const Paint f = solid(CRa(0.5f)); fastRRect(cx - 10, cy + 13, 20, 3, 1.5f, &f, nullptr); }
}
inline void ripples(int key, float cx, float cy, float t) {
  const Col c = col(key); const float sp = 0.5f + (ok[key] ? fr(key) : 0) * 2; halo(cx, cy, 20, c);
  if (!D_()) return;
  for (int j = 0; j < 3; j++) { float ph = fmodf(t * sp * 0.5f + j / 3.f, 1.f); fastRing(cx, cy, 3 + ph * 14, 1, withA(c, 0.8f * (1 - ph))); }
  shadowCol = withA(c, 0.9f); shadowBlur = 5; circle(cx, cy, 2.6f); fill(solid(c)); shadowBlur = 0;
}
// окно с пылью: маска скругления запоминается один раз (дальше пылинки умножаются на неё — без пересчёта пути)
inline uint8_t dustMask[120 * 60]; inline int dmX = 0, dmY = 0, dmW = 0, dmH = 0; inline bool dmOk = false;
inline void dustBox(float x, float y, float w, float h, float t) {
  if (S_()) {
    { const Paint f = solid(rgba(8, 6, 5, 0.75f)); const Col sk = CRa(0.30f); fastRRect(x, y, w, h, 6, &f, &sk, 0.6f); }
    save(); rrect(x, y, w, h, 6); clipPath();
    fillRect(x, y, w, h, linear(x, y, x + w, y + h).stop(0, rgba(255, 181, 74, 0.10f)).stop(0.5f, rgba(255, 181, 74, 0)));
    dmX = (int)st.cx0; dmY = (int)st.cy0; dmW = (int)st.cx1 - dmX; dmH = (int)st.cy1 - dmY;
    if (dmW > 120) dmW = 120; if (dmH > 60) dmH = 60;
    for (int yy = 0; yy < dmH; yy++) for (int xx = 0; xx < dmW; xx++) dustMask[yy * 120 + xx] = maskBuf[(dmY + yy) * W + dmX + xx];
    dmOk = true;
    restore();
  }
  if (!D_()) return;
  const int cnt[3] = {ok[K_P1] ? (int)fminf(50, floorf(val[K_P1] * 1.4f + 0.5f)) : 0, ok[K_P] ? (int)fminf(50, floorf(val[K_P] + 0.5f)) : 0, ok[K_P10] ? (int)fminf(50, floorf(val[K_P10] * 0.6f + 0.5f)) : 0};
  const int keys[3] = {K_P1, K_P, K_P10}; const float sz[3] = {0.6f, 1.0f, 1.6f}; int idx[3] = {0, 0, 0};
  save();
  if (dmOk && maskBuf) {
    for (int yy = 0; yy < dmH; yy++) for (int xx = 0; xx < dmW; xx++) maskBuf[(dmY + yy) * W + dmX + xx] = dustMask[yy * 120 + xx];
    st.mask = true; st.cx0 = fmaxf(st.cx0, dmX); st.cy0 = fmaxf(st.cy0, dmY); st.cx1 = fminf(st.cx1, dmX + dmW); st.cy1 = fminf(st.cy1, dmY + dmH);
  }
  for (auto& d : dust) {
    if (idx[d.cls]++ >= cnt[d.cls]) continue;
    const Col c = col(keys[d.cls]); const float px_ = x + d.x * w, py_ = y + d.y * h, tw = 0.55f + 0.45f * sinf(t * 3 + d.p);
    fastCircle(px_, py_, sz[d.cls], same(c, TL) ? CRa(0.35f + 0.5f * tw) : withA(c, 0.4f + 0.55f * tw));
  }
  restore();
  fillText(F_JB8M, L("ВОЗДУХ ×1000", "AIR ×1000"), x + 5, y + h - 4, CRa(0.60f));
}

inline void drawAll(float t, float e) {
  auto ap = [&](int i) { float a = easeOut3((e - 80 * i) / 550); st.alpha = a; return (1 - a) * 5; };
  char b[24];
  const int topK[3] = {K_T, K_H, K_C}; const float topX[3] = {8, 111, 214};
  for (int i = 0; i < 3; i++) {
    const int k = topK[i]; const float x = topX[i], y = 24 + ap(i), w = 98, h = 80;
    if (st.alpha <= 0.002f) continue;
    cdyn = same(col(k), RD);
    save(); card(x, y, w, h, k, t); label(x + 9, y + 13, k, x + w - 8, false);
    if (i == 0) thermo(x + 25, y + 44, t); else if (i == 1) drop(x + 25, y + 44, t); else molecule(x + (val[K_C] >= 999.5f ? 19 : 25), y + 45, t);
    if (D_()) { fmt(k, b); fillText(strlen(b) >= 4 && k == K_C ? F_LI24 : F_LI27, b, x + w - 8, y + 52, ok[k] ? CR : CRa(0.45f), AL_RIGHT); }
    if (S_()) fillText(F_JB9B, unit(k), x + w - 8, y + 63, col(k), AL_RIGHT);
    bar(x + 9, x + w - 9, y + 72, k);
    restore();
  }
  const int midK[2] = {K_V, K_N}; const float midX[2] = {8, 163};
  for (int i = 0; i < 2; i++) {
    const int k = midK[i]; const float x = midX[i], y = 110 + ap(i + 3), w = 149;
    if (st.alpha <= 0.002f) continue;
    cdyn = same(col(k), RD);
    save(); card(x, y, w, 54, k, t);
    if (i == 0) ripples(k, x + 22, y + 27, t); else wisps(k, x + 22, y + 27, t);
    label(x + 44, y + 14, k, x + w - 8);
    fmt(k, b); const float vw = measure(F_LI25, b);
    if (D_()) { fillText(F_LI25, b, x + 43, y + 40, ok[k] ? CR : CRa(0.45f)); fillText(F_JB9B, unit(k), x + 47 + vw, y + 40, col(k)); }   // единица сдвигается вместе с числом
    bar(x + 44, x + w - 9, y + 47, k);
    restore();
  }
  {
    const float x = 8, y = 170 + ap(5), w = 304, h = 64;
    if (st.alpha > 0.002f) {
      save();
      const Col cs[3] = {col(K_P1), col(K_P), col(K_P10)};
      Col worst = TL; for (auto& c : cs) if (same(c, RD)) worst = RD; if (!same(worst, RD)) for (auto& c : cs) if (same(c, AM)) worst = AM;
      if (!ok[K_P]) worst = NEU;
      cdyn = false;   // нижняя карточка не пульсирует
      if (S_()) { const Paint f = solid(calm(worst) ? CRa(0.10f) : withA(worst, 0.18f)); const Col sk = calm(worst) ? CRa(0.30f) : withA(worst, 0.5f); fastRRect(x, y, w, h, 8, &f, &sk, 0.7f); }
      dustBox(x + 5, y + 5, 112, h - 10, t);
      if (S_()) {
        const char* pt = L("ЧАСТИЦЫ", "PARTICLES");
        fillText(F_ON85B, pt, x + 126, y + 13, CRa(0.75f), AL_LEFT, 1.4f);
        fillText(F_JB85M, L("мкг/м³", "µg/m³"), x + 126 + measure(F_ON85B, pt, 1.4f) + 4, y + 13, CRa(0.60f));
        const char* wd = word(worst); if (wd[0]) fillText(F_JB8B, wd, x + w - 8, y + 13, same(worst, NEU) ? CRa(0.6f) : worst, AL_RIGHT);
      }
      const int pk[3] = {K_P1, K_P, K_P10};
      for (int j = 0; j < 3; j++) {
        const int k = pk[j]; const float cx = x + 126 + j * 58; const Col c = col(k);
        if (S_()) { fastCircle(cx + 2, y + 23, 1.6f, c); fillText(F_JB85M, lab(k), cx + 7, y + 25.5f, CRa(0.6f)); }
        if (D_()) { fmt(k, b); fillText(F_LI24, b, cx - 1, y + 47, ok[k] ? CR : CRa(0.45f)); }
        bar(cx, cx + 48, y + 55, k);
      }
      restore();
    }
  }
  st.alpha = 1;
}

inline uint8_t* bgCache = nullptr; inline uint8_t* stCache = nullptr; inline uint32_t stKey = 0xFFFFFFFF;
inline void frame(float t, float now) {
  const float e = now - t0ms;
  uint32_t _pt = GX_US();
  resetState();
  // фон и шапка не зависят от данных
  if (!bgCache) {
    fillRect(0, 0, W, H, linear(0, 0, 0, H).stop(0, hex(0x1b130f)).stop(1, hex(0x0b0807)));
    fillRect(0, 0, W, H, radial(80, 20, 10, 120, 60, 260).stop(0, rgba(255, 181, 74, 0.07f)).stop(1, rgba(0, 0, 0, 0)));
    bgCache = (uint8_t*)GX_ALLOC(W * H * 3); if (bgCache) copyTo(bgCache);
  }
  if (e < 1000 || !bgCache) {   // появление: всё целиком
    if (bgCache) copyFrom(bgCache); MODE = 0; drawAll(t, e); stKey = 0xFFFFFFFF;
  } else {
    uint32_t key = 0; for (int k = 0; k < NK; k++) { Col c = col(k); key = key * 4 + (same(c, TL) ? 0 : same(c, AM) ? 1 : same(c, RD) ? 2 : 3); }
    if (!stCache) stCache = (uint8_t*)GX_ALLOC(W * H * 3);
    if (!stCache) { copyFrom(bgCache); MODE = 0; drawAll(t, e); }
    else {
      if (key != stKey) {   // зоны поменялись — пересобрать неподвижный слой
        copyFrom(bgCache); MODE = 1; drawAll(t, e);
        copyTo(stCache); stKey = key;
      } else copyFrom(stCache);
      DP(0)
      MODE = 2; drawAll(t, e);
      DP(1)
    }
  }
  {   // шапка: время и дата слева; справа место под индикатор батареи
    fillText(F_ON13B, hhmm, 10, 17, CR);
    const float tw = measure(F_ON13B, hhmm);
    fillText(F_ON10B, date, 10 + tw + 9, 17, withA(AM, 0.95f), AL_LEFT, 1.6f);
  }
  MODE = 0;
}

}  // namespace dash
