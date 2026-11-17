// Данные прибора: показатели, погода, рынки, история. Чистый C++ без Arduino — тот же код собирается на ПК (sim).
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace app {

// порядок показателей — один на всю прошивку (экраны, история, Telegram)
enum { M_T, M_H, M_CO2, M_VOC, M_NOX, M_PM1, M_PM25, M_PM10, M_N };

struct Live {            // текущие показания; ok=false — датчик молчит, на экране прочерк
  float v[M_N];
  bool ok[M_N];
};

struct Clock { bool ok; int hh, mm, wday, mday, mon; };   // wday 0 — воскресенье, mon 0..11

struct Wx {              // погода с Open-Meteo
  bool ok;
  uint32_t atMs;         // millis() последнего удачного ответа
  char city[32];
  float temp, feels, hi, lo, wind;
  int windDir, hum, code;
  bool day;
  float hr[7]; int pr[7]; int hour0;   // 7 часов вперёд: температура, вероятность осадков, первый час
  float uv; int sunrise, sunset;        // УФ-индекс сейчас (−1 — нет); рассвет и закат, минуты от полуночи (−1 — нет)
};

constexpr int SPARK_N = 26;
struct Asset {
  char tick[10]; char name[28]; char sym[20];   // тикер для экрана, название (до 12 букв), символ Yahoo
  char kind; char cur[5];                        // тип: c крипта, e акция, t фонд, i индекс, f сырьё, x валюта; валюта цены
  bool ok; float price, d7, d30, y1;
  float sp[SPARK_N]; int nsp;          // полгода по неделям — мини-график
};
constexpr int ASSET_N = 5;
// набор по умолчанию; свой выбирается на странице настроек (раздел «Рынки»)
struct AssetDef { const char* sym; const char* name; const char* tick; char kind; };
inline const AssetDef ASSET_DEF[ASSET_N] = {{"BTC-USD", "БИТКОИН", "BTC", 'c'}, {"ETH-USD", "ЭФИР", "ETH", 'c'}, {"GC=F", "ЗОЛОТО", "XAU", 'f'}, {"CL=F", "НЕФТЬ", "WTI", 'f'}, {"^GSPC", "S&P 500", "SPX", 'i'}};
inline void assetSet(Asset& a, const char* sym, const char* name, const char* tick, char kind) {
  memset(&a, 0, sizeof a);
  snprintf(a.sym, sizeof a.sym, "%s", sym); snprintf(a.name, sizeof a.name, "%s", name); snprintf(a.tick, sizeof a.tick, "%s", tick); a.kind = kind;
}

// ---------- история: кольцо 12 часов по 5 минут ----------
constexpr int HN = 144, SLOT_S = 300, PTS = 25;
constexpr int16_t H_NONE = INT16_MIN;
inline float hScale(int m) { return (m == M_T || m == M_H) ? 10.f : 1.f; }

struct Hist {
  int16_t s[HN][M_N];    // s[HN-1] — самый свежий слот
  uint32_t lastSlot;     // номер последнего записанного слота (epoch / SLOT_S), 0 — время было неизвестно
  float acc[M_N]; uint16_t cnt[M_N];
};

inline void histClear(Hist& h) {
  for (int i = 0; i < HN; i++) for (int m = 0; m < M_N; m++) h.s[i][m] = H_NONE;
  h.lastSlot = 0; memset(h.acc, 0, sizeof h.acc); memset(h.cnt, 0, sizeof h.cnt);
}
inline void histShift(Hist& h, int n) {            // n пустых слотов в конец
  if (n <= 0) return;
  if (n >= HN) { for (int i = 0; i < HN; i++) for (int m = 0; m < M_N; m++) h.s[i][m] = H_NONE; return; }
  memmove(h.s[0], h.s[n], sizeof(h.s[0]) * (HN - n));
  for (int i = HN - n; i < HN; i++) for (int m = 0; m < M_N; m++) h.s[i][m] = H_NONE;
}
inline void histAdd(Hist& h, const Live& l) {      // раз в секунду-две
  for (int m = 0; m < M_N; m++) if (l.ok[m] && h.cnt[m] < 60000) { h.acc[m] += l.v[m]; h.cnt[m]++; }
}
inline void histCommit(Hist& h) {                  // закрыть слот: среднее за 5 минут
  histShift(h, 1);
  for (int m = 0; m < M_N; m++) {
    if (h.cnt[m]) { float v = h.acc[m] / h.cnt[m] * hScale(m); if (v > 32000) v = 32000; if (v < -32000) v = -32000; h.s[HN - 1][m] = (int16_t)lroundf(v); }
    h.acc[m] = 0; h.cnt[m] = 0;
  }
}
// 25 точек с шагом 30 минут для графика; последняя — текущее значение. Возвращает число точек с настоящими данными.
// hasOut (если нужен) — где данные настоящие: по нему график показывает разрыв (прибор был выключен).
inline int histSeries(const Hist& h, int m, float cur, bool curOk, float out[PTS], bool* hasOut = nullptr) {
  bool has[PTS]; int n = 0;
  for (int p = 0; p < PTS - 1; p++) {
    float sum = 0; int c = 0;
    for (int j = 0; j < 6; j++) { int16_t v = h.s[p * 6 + j][m]; if (v != H_NONE) { sum += v; c++; } }
    has[p] = c > 0; out[p] = c ? sum / c / hScale(m) : 0; n += has[p];
  }
  has[PTS - 1] = curOk; out[PTS - 1] = curOk ? cur : 0;
  if (!curOk) for (int p = PTS - 2; p >= 0; p--) if (has[p]) { out[PTS - 1] = out[p]; break; }
  n += curOk;
  if (hasOut) for (int p = 0; p < PTS; p++) hasOut[p] = has[p];
  if (!n) return 0;
  // пропуски: тянем ближайшее известное значение (сначала вперёд, потом назад)
  has[PTS - 1] = true;
  int first = 0; while (!has[first]) first++;
  for (int p = 0; p < first; p++) out[p] = out[first];
  float last = out[first];
  for (int p = first; p < PTS; p++) { if (has[p]) last = out[p]; else out[p] = last; }
  return n;
}

// ---------- сохранение истории (два файла по очереди, заголовок + CRC) ----------
struct HistFile { uint32_t magic, ver, seq, lastSlot, crc; int16_t s[HN][M_N]; };
constexpr uint32_t HIST_MAGIC = 0x41495248, HIST_VER = 2;   // "HRIA"
inline uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
  return ~c;
}
inline void histPack(const Hist& h, uint32_t seq, HistFile& f) {
  f.magic = HIST_MAGIC; f.ver = HIST_VER; f.seq = seq; f.lastSlot = h.lastSlot; memcpy(f.s, h.s, sizeof f.s);
  f.crc = crc32((const uint8_t*)f.s, sizeof f.s) ^ seq ^ h.lastSlot;
}
inline bool histValid(const HistFile& f) {
  return f.magic == HIST_MAGIC && f.ver == HIST_VER && f.crc == (crc32((const uint8_t*)f.s, sizeof f.s) ^ f.seq ^ f.lastSlot);
}

// ---------- погода: код Open-Meteo -> описание ----------
inline const char* wxDesc(int c) {
  if (c == 0) return "ясно";
  if (c == 1) return "малооблачно";
  if (c == 2) return "переменная облачность";
  if (c == 3) return "пасмурно";
  if (c == 45) return "туман";
  if (c == 48) return "туман, изморозь";
  if (c == 56 || c == 57) return "ледяная морось";
  if (c >= 51 && c <= 55) return "морось";
  if (c == 61) return "небольшой дождь";
  if (c == 65) return "сильный дождь";
  if (c == 66 || c == 67) return "ледяной дождь";
  if (c >= 61 && c <= 67) return "дождь";
  if (c == 71) return "небольшой снег";
  if (c == 75) return "сильный снег";
  if (c == 77) return "снежная крупа";
  if (c >= 71 && c <= 77) return "снег";
  if (c == 80) return "небольшой ливень";
  if (c == 82) return "сильный ливень";
  if (c >= 80 && c <= 82) return "ливень";
  if (c == 85 || c == 86) return "снегопад";
  if (c == 96 || c == 99) return "гроза с градом";
  if (c >= 95) return "гроза";
  return "облачно";
}
inline const char* windName(int deg) {
  static const char* n[8] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};
  return n[(((deg % 360) + 360) % 360 * 2 + 45) / 90 % 8];
}
inline const char* WDAY[7] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
inline const char* MONTH[12] = {"ЯНВ", "ФЕВ", "МАР", "АПР", "МАЯ", "ИЮН", "ИЮЛ", "АВГ", "СЕН", "ОКТ", "НОЯ", "ДЕК"};

}  // namespace app
