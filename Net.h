// Задача сети (ядро 0): Wi-Fi, время, погода Open-Meteo, рынки Yahoo, сохранение истории, веб-настройки, Telegram.
// Всё медленное и всё, что может зависнуть на сервере, живёт здесь — экран и датчики от сети не зависят.
// У каждого запроса свой тайм-аут; ответ разбирается потоком с фильтром (лишнее в память не попадает).
#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "Store.h"

namespace net {

// JSON-документы — в PSRAM, чтобы не дробить основную память
struct PsAlloc : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
inline PsAlloc psAlloc;

inline uint32_t reconnects = 0, httpFails = 0;
inline uint32_t lastWifiTry = 0, wxTry = 0, wxOkMs = 0, mkTry = 0;
inline bool wasConnected = false, wxEver = false, mkEver = false;
inline int mkNext = app::ASSET_N;              // какой инструмент качать следующим (ASSET_N — круг закончен)
inline bool mkAnyOk = false;

// Задача сети не стоит под общим сторожем 30 с: поиск адреса + соединение + TLS на медленной сети законно идут дольше,
// и сторож перезагружал весь прибор (16.11.2026, Yahoo). Вместо этого — отметка «жива»; экранная задача перезагрузит
// прибор, только если сеть молчит дольше NET_STUCK_MS (настоящее зависание).
inline volatile uint32_t beatMs = 0;
constexpr uint32_t NET_STUCK_MS = 180000;
inline void feed() { beatMs = millis(); }

// GET + разбор JSON потоком. tls — https без проверки сертификата (в запросах нет секретов)
inline bool getJson(const char* url, bool tls, JsonDocument& doc, const JsonDocument& filter) {
  feed();
  HTTPClient http;
  WiFiClient plain; WiFiClientSecure sec;
  http.setConnectTimeout(5000); http.setTimeout(6000);
  http.setReuse(false);
  http.setUserAgent("Mozilla/5.0 (ESP32; AirMonitor)");
  bool ok;
  if (tls) { sec.setInsecure(); sec.setHandshakeTimeout(8); ok = http.begin(sec, url); } else ok = http.begin(plain, url);
  if (!ok) { httpFails++; return false; }
  const uint32_t g0 = millis();
  const int code = http.GET();
  feed();
  if (millis() - g0 > 8000) Serial.printf("[сеть] медленный ответ: %lu с, код %d <- %.50s\n", (unsigned long)((millis() - g0) / 1000), code, url);
  if (code != 200) { Serial.printf("[сеть] %d <- %.60s\n", code, url); http.end(); httpFails++; return false; }
  // тело читаем целиком с жёстким пределом по времени и размеру (в PSRAM), потом разбираем с фильтром:
  // чтение JSON прямо из потока на https могло зависнуть дольше сторожа
  static char* body = nullptr; constexpr size_t CAP = 96 * 1024;
  if (!body) body = (char*)heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!body) { http.end(); httpFails++; return false; }
  size_t n = 0;
  { struct Sink : Stream {                      // HTTPClient сам разбирает chunked и пишет сюда
      char* b; size_t cap; size_t* n;
      size_t write(uint8_t c) override { if (*n < cap - 1) b[(*n)++] = c; return 1; }
      size_t write(const uint8_t* d, size_t len) override { const size_t k = min(len, cap - 1 - *n); memcpy(b + *n, d, k); *n += k; feed(); return len; }
      int available() override { return 0; } int read() override { return -1; } int peek() override { return -1; }
    } sink; sink.b = body; sink.cap = CAP; sink.n = &n;
    http.writeToStream(&sink); }
  body[n] = 0;
  http.end();
  const DeserializationError e = deserializeJson(doc, (const char*)body, n, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(12));
  feed();
  if (e) { Serial.printf("[сеть] JSON: %s <- %.60s\n", e.c_str(), url); httpFails++; return false; }
  return true;
}

// ---------- город -> координаты (один раз на город, хранится в NVS) ----------
static inline void utf8Upper(char* s) {         // латиница и кириллица
  for (uint8_t* p = (uint8_t*)s; *p; p++) {
    if (*p >= 'a' && *p <= 'z') *p -= 32;
    else if (*p == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) { p[1] -= 0x20; p++; }               // а..п
    else if (*p == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) { *p = 0xD0; p[1] += 0x20; p++; }    // р..я
    else if (*p == 0xD1 && p[1] == 0x91) { *p = 0xD0; p[1] = 0x81; p++; }                     // ё
    else if (*p >= 0xC0) { p++; }
  }
}
static inline void urlEnc(const char* in, char* out, size_t n) {
  static const char hx[] = "0123456789ABCDEF"; size_t o = 0;
  for (const uint8_t* p = (const uint8_t*)in; *p && o + 4 < n; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') out[o++] = *p;
    else { out[o++] = '%'; out[o++] = hx[*p >> 4]; out[o++] = hx[*p & 15]; }
  }
  out[o] = 0;
}
// поиск города: 1 — найден, 0 — такого нет, -1 — нет связи. Настройки не трогает (им же пользуется страница настроек)
inline int geoLookup(const char* city, char* name, size_t nn, char* country, size_t cn, float& lat, float& lon) {
  char q[100], url[220]; urlEnc(city, q, sizeof q);
  snprintf(url, sizeof url, "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=ru&format=json", q);
  JsonDocument filter(&psAlloc), doc(&psAlloc);
  filter["results"][0]["latitude"] = true; filter["results"][0]["longitude"] = true; filter["results"][0]["name"] = true; filter["results"][0]["country"] = true;
  if (!getJson(url, false, doc, filter)) return -1;
  JsonObject r = doc["results"][0];
  if (r.isNull()) return 0;
  lat = r["latitude"] | 0.0f; lon = r["longitude"] | 0.0f;
  if (lat == 0 && lon == 0) return 0;
  strlcpy(name, r["name"] | city, nn); strlcpy(country, r["country"] | "", cn);
  return 1;
}
inline bool geocode() {
  if (cfg.geoCity[0] && !strcmp(cfg.geoCity, cfg.city) && (cfg.lat != 0 || cfg.lon != 0)) return true;
  char name[48], country[48]; float la, lo;
  const int r = geoLookup(cfg.city, name, sizeof name, country, sizeof country, la, lo);
  if (r <= 0) { if (!r) Serial.println("[погода] город не найден"); return false; }
  cfg.lat = la; cfg.lon = lo;
  strlcpy(cfg.cityShown, name, sizeof cfg.cityShown); utf8Upper(cfg.cityShown);
  strlcpy(cfg.geoCity, cfg.city, sizeof cfg.geoCity);
  cfgSaveGeo();
  Serial.printf("[погода] %s: %.4f, %.4f\n", cfg.cityShown, cfg.lat, cfg.lon);
  return true;
}

inline bool fetchWeather() {
  if (!geocode()) return false;
  char url[520];   // часовой пояс Open-Meteo определяет сам по координатам (timezone=auto)
  snprintf(url, sizeof url,
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,weather_code,relative_humidity_2m,wind_speed_10m,wind_direction_10m,is_day"
           "&hourly=temperature_2m,precipitation_probability,uv_index&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset"
           "&wind_speed_unit=ms&timezone=auto&forecast_days=2", cfg.lat, cfg.lon);
  JsonDocument filter(&psAlloc), doc(&psAlloc);
  filter["utc_offset_seconds"] = true; filter["current"] = true;
  filter["hourly"]["temperature_2m"] = true; filter["hourly"]["precipitation_probability"] = true;
  filter["daily"]["temperature_2m_max"] = true; filter["daily"]["temperature_2m_min"] = true;
  filter["hourly"]["uv_index"] = true; filter["daily"]["sunrise"] = true; filter["daily"]["sunset"] = true;
  bool ok = getJson(url, false, doc, filter);
  if (!ok) { memmove(url + 5, url + 4, strlen(url + 4) + 1); url[4] = 's'; doc.clear(); ok = getJson(url, true, doc, filter); }   // запасной путь — https
  if (!ok) return false;
  JsonObject cur = doc["current"];
  if (cur.isNull() || cur["temperature_2m"].isNull()) return false;
  app::Wx w{};
  w.ok = true; w.atMs = millis();
  strlcpy(w.city, cfg.cityShown[0] ? cfg.cityShown : cfg.city, sizeof w.city);
  w.temp = cur["temperature_2m"] | 0.0f; w.feels = cur["apparent_temperature"] | w.temp;
  w.code = cur["weather_code"] | 0; w.hum = cur["relative_humidity_2m"] | 0;
  w.wind = cur["wind_speed_10m"] | 0.0f; w.windDir = cur["wind_direction_10m"] | 0; w.day = (cur["is_day"] | 1) != 0;
  w.hi = doc["daily"]["temperature_2m_max"][0] | w.temp; w.lo = doc["daily"]["temperature_2m_min"][0] | w.temp;
  const char* ct = cur["time"] | "";            // "2026-11-16T14:45"
  int hour = strlen(ct) >= 13 ? atoi(ct + 11) : 0; if (hour < 0 || hour > 23) hour = 0;
  w.hour0 = hour;
  JsonArray ht = doc["hourly"]["temperature_2m"], hp = doc["hourly"]["precipitation_probability"];
  for (int i = 0; i < 7; i++) {
    w.hr[i] = (i == 0 || ht[hour + i].isNull()) ? (i == 0 ? w.temp : w.hr[i - 1]) : ht[hour + i].as<float>();
    w.pr[i] = hp[hour + i] | 0;
  }
  { JsonVariant u = doc["hourly"]["uv_index"][hour]; w.uv = u.isNull() ? -1.f : u.as<float>(); }
  auto hm = [](const char* s) { return (s && strlen(s) >= 16) ? atoi(s + 11) * 60 + atoi(s + 14) : -1; };   // "2026-11-16T07:42"
  w.sunrise = hm(doc["daily"]["sunrise"][0] | ""); w.sunset = hm(doc["daily"]["sunset"][0] | "");
  const int32_t off = doc["utc_offset_seconds"] | cfg.utcOff;
  if (off != cfg.utcOff && off > -50400 && off < 50400) { cfg.utcOff = off; cfgPrefs.putInt("utc_off", off); configTime(off, 0, Config::ntpServer); }
  { Lock lk; G.wx = w; G.wxSeq++; }
  Serial.printf("[погода] %s %.1f°, код %d\n", w.city, w.temp, w.code);
  return true;
}

// ---------- рынки: Yahoo Finance, год по неделям ----------
inline bool fetchAsset(int i) {
  char url[200], raw[20], sym[64];
  { Lock lk; memcpy(raw, G.mk[i].sym, sizeof raw); }
  urlEnc(raw, sym, sizeof sym);
  snprintf(url, sizeof url, "https://query1.finance.yahoo.com/v8/finance/chart/%s?range=1y&interval=1wk", sym);
  JsonDocument filter(&psAlloc), doc(&psAlloc);
  filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true; filter["chart"]["result"][0]["meta"]["currency"] = true;
  filter["chart"]["result"][0]["timestamp"] = true;
  filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;
  if (!getJson(url, true, doc, filter)) return false;
  JsonObject r = doc["chart"]["result"][0];
  JsonArray ts = r["timestamp"], cl = r["indicators"]["quote"][0]["close"];
  if (ts.isNull() || cl.isNull()) return false;
  static float c[64]; static long t[64]; int n = 0;       // только задача сети
  const int total = min((int)ts.size(), (int)cl.size());
  for (int k = max(0, total - 64); k < total; k++) { if (cl[k].isNull()) continue; const float v = cl[k].as<float>(); if (!(v > 0)) continue; c[n] = v; t[n] = ts[k].as<long>(); n++; }
  if (n < 1) return false;                       // у редких валютных пар Yahoo отдаёт одну точку: цена есть, графика и процентов нет
  float price = r["meta"]["regularMarketPrice"] | c[n - 1]; if (!(price > 0)) price = c[n - 1];
  auto pct = [&](int days) {
    const long target = t[n - 1] - days * 86400L; int j = 0;
    for (int k = n - 1; k >= 0; k--) if (t[k] <= target) { j = k; break; }
    return (price - c[j]) / c[j] * 100.f;
  };
  app::Asset a; { Lock lk; a = G.mk[i]; }
  strlcpy(a.cur, r["meta"]["currency"] | "", sizeof a.cur);
  a.ok = true; a.price = price; a.d7 = pct(7); a.d30 = pct(30); a.y1 = (price - c[0]) / c[0] * 100.f;
  if (n < 2) a.d7 = a.d30 = a.y1 = NAN;
  a.nsp = min(n, app::SPARK_N); for (int k = 0; k < a.nsp; k++) a.sp[k] = c[n - a.nsp + k];
  a.sp[a.nsp - 1] = price;
  { Lock lk; G.mk[i] = a; G.mkSeq++; }
  return true;
}

// ---------- история на LittleFS: два файла по очереди ----------
inline bool fsOk = false; inline uint32_t histSeqNo = 0;
inline app::HistFile* hf = nullptr;            // рабочий буфер (PSRAM), один на всю жизнь
inline void histLoad() {
  fsOk = LittleFS.begin(true);                 // первый запуск: форматирование занимает несколько секунд — замок в это время не держим
  hf = (app::HistFile*)ps_malloc(sizeof(app::HistFile));
  if (!fsOk || !hf) { Serial.println("[история] нет файловой системы — история только в памяти"); return; }
  bool found = false; uint32_t best = 0;
  static app::HistFile tmp;                    // читаем кандидата сюда, лучший копируем в hf
  for (const char* p : {"/hist_a.bin", "/hist_b.bin"}) {
    File f = LittleFS.open(p, "r"); if (!f) continue;
    const bool ok = f.size() == sizeof tmp && f.read((uint8_t*)&tmp, sizeof tmp) == sizeof tmp && app::histValid(tmp);
    f.close();
    if (ok && (!found || tmp.seq > best)) { *hf = tmp; best = tmp.seq; found = true; }
  }
  if (found) { Lock lk; memcpy(G.hist.s, hf->s, sizeof G.hist.s); G.hist.lastSlot = hf->lastSlot; histSeqNo = best; G.histSeq++; }
  Serial.println(found ? "[история] загружена" : "[история] пусто, начинаем заново");
}
inline void histSave() {
  if (!fsOk || !hf) return;
  { Lock lk; app::histPack(G.hist, ++histSeqNo, *hf); G.histDirty = 0; }
  File f = LittleFS.open(histSeqNo & 1 ? "/hist_a.bin" : "/hist_b.bin", "w");
  if (!f) return;
  f.write((const uint8_t*)hf, sizeof *hf); f.close();
}

inline void wifiBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.ssid, cfg.password);
  lastWifiTry = millis();
}
inline void wifiReconnect() { WiFi.disconnect(); delay(100); WiFi.begin(cfg.ssid, cfg.password); lastWifiTry = millis(); }

}  // namespace net
