// Монитор воздуха: ESP32-S3 N16R8 + TFT 2.8" (ILI9341 или ST7789V) + BME280 + SGP41 + MH-Z19C + PMS5003.
// Функции старой прошивки на новом дизайне (dizain_ish/01–05 + экран «Рынки»).
//
// Arduino IDE → Инструменты:
//   Плата: ESP32S3 Dev Module
//   PSRAM: OPI PSRAM                  (обязательно — кадры лежат в PSRAM)
//   Flash Size: 16MB (128Mb)
//   Partition Scheme: Custom          (берётся partitions.csv из папки скетча: 6 МБ под прошивку, 3,9 МБ под историю)
// Библиотеки: Adafruit ILI9341 / Adafruit ST7735 and ST7789, Adafruit GFX, Adafruit BME280, Sensirion I2C SGP41,
//             Sensirion Gas Index Algorithm, ArduinoJson 7.
//
// Кнопка (GPIO 12): коротко — следующий экран (часы → показатели → индекс → графики → погода → рынки),
//                   держать 5 с — веб-настройки (точка доступа ESP32-TFT-AIR), ещё раз 5 с — перезагрузка.
// Веб-настройки (Web.h, страница web/index.html): из домашней сети — http://air.local, без кнопки; держать 5 с — точка доступа и QR на экране.
// Монитор порта 115200: раз в минуту строка здоровья (память, стеки, к/с, ошибки датчиков); команды: n — следующий экран, d — показания и к/с, r — обновить погоду и рынки, p — веб-настройки.
//
// Кто что делает (у каждой железки один хозяин):
//   loop (ядро 1)      — экран: рисует кадры, кнопка, яркость
//   push (ядро 0)      — отправка готового кадра по SPI
//   sensors (ядро 1)   — I2C и UART датчиков, история
//   net (ядро 0)       — Wi-Fi, время, погода, рынки, Telegram, настройки, запись истории на флеш

#include <SPI.h>
#include <Adafruit_GFX.h>
#include "Config.h"
#if SCREEN_ST7789
#include <Adafruit_ST7789.h>
#else
#include <Adafruit_ILI9341.h>
#endif
#include <esp_task_wdt.h>

#define GX_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define GX_US() micros()
#include "core.h"
#if AIR_PRO
#include "src/pro/pro.h"                         // часы, погода, индекс, графики, рынки — только в PRO
#endif
#include "dash.h"
#include "Store.h"
#include "Sensors.h"
#include "Net.h"
#if AIR_PRO
#include "src/pro/Telegram.h"
#endif
#include "Web.h"

#if SCREEN_ST7789
Adafruit_ST7789 tft(&SPI, Config::Pins::TFT_CS, Config::Pins::TFT_DC, Config::Pins::TFT_RST);
#else
Adafruit_ILI9341 tft(Config::Pins::TFT_CS, Config::Pins::TFT_DC, Config::Pins::TFT_RST);
#endif

// ================= отправка кадров на экран отдельной задачей =================
struct Job { uint16_t* buf; int16_t x, y, w, h; uint8_t idx; };
static uint16_t* outBuf[2];
static SemaphoreHandle_t freeSem[2];
static QueueHandle_t jobs;
static int backIdx = 0;

static void pushTask(void*) {
  esp_task_wdt_add(nullptr);
  Job j;
  for (;;) {
    esp_task_wdt_reset();
    if (xQueueReceive(jobs, &j, pdMS_TO_TICKS(2000)) == pdTRUE) {
      tft.startWrite();
      tft.setAddrWindow(j.x, j.y, j.w, j.h);
      tft.writePixels(j.buf, (uint32_t)j.w * j.h, true, false);
      tft.endWrite();
      xSemaphoreGive(freeSem[j.idx]);
      vTaskDelay(1);   // дать ядру 0 вздохнуть (там же живёт Wi-Fi)
    }
  }
}
// прямоугольник кадра -> RGB565 -> в очередь на экран
static void present(int x, int y, int w, int h) {
  const int i = backIdx;
  if (xSemaphoreTake(freeSem[i], pdMS_TO_TICKS(1000)) != pdTRUE) return;   // экран занят дольше секунды — пропускаем кадр, не виснем
  gx::toRGB565(x, y, w, h, outBuf[i]);
  Job j{outBuf[i], (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (uint8_t)i};
  if (xQueueSend(jobs, &j, pdMS_TO_TICKS(1000)) != pdTRUE) { xSemaphoreGive(freeSem[i]); return; }
  backIdx ^= 1;
}
static void presentAll() { present(0, 0, gx::W, gx::H); }

// ================= кнопка: опрос раз в 5 мс, события в очередь =================
static void buttonTask(void*) {
  bool raw = digitalRead(Config::Pins::BTN), stable = raw; uint32_t lastChange = millis(), pressAt = 0; bool longSent = false;
  uint32_t pendAt = 0; bool pend = false, second = false;   // pend — ждём, не будет ли второго нажатия
  for (;;) {
    const uint32_t now = millis(); const bool r = digitalRead(Config::Pins::BTN);
    if (r != raw) { raw = r; lastChange = now; }
    if (now - lastChange >= Config::BUTTON_DEBOUNCE_MS && raw != stable) {
      stable = raw;
      Serial.printf("[кнопка] %s\n", stable == LOW ? "нажата" : "отпущена");
      if (stable == LOW) {
        pressAt = now; longSent = false;
        if (pend) { pend = false; second = true; uint8_t e = EV_DOUBLE; xQueueSend(gBtnQ, &e, 0); }   // двойной — сразу на втором нажатии
      } else {
        if (second) second = false;                                   // отпускание после двойного — не событие
        else if (!longSent && pressAt) { pend = true; pendAt = now; }
        pressAt = 0;
      }
    }
    if (pend && now - pendAt >= Config::BUTTON_DOUBLE_MS) { pend = false; uint8_t e = EV_SHORT; xQueueSend(gBtnQ, &e, 0); }
    if (stable == LOW && !longSent && pressAt && now - pressAt >= Config::BUTTON_LONG_PRESS_MS) { uint8_t e = EV_LONG; xQueueSend(gBtnQ, &e, 0); longSent = true; }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ================= яркость по фоторезистору =================
static float brightness = 40, ldrF = 0.5f;
static void backlightBegin() {
#if AUTO_BRIGHTNESS
  ledcAttach(Config::Pins::TFT_LED, 20000, 8);   // 20 кГц — без полос при съёмке на телефон
  ledcWrite(Config::Pins::TFT_LED, 120);
  analogReadResolution(12);
#endif
}
static void backlightTick() {
#if AUTO_BRIGHTNESS
  static uint32_t last = 0; if (millis() - last < 40) return; last = millis();
  const float norm = constrain(analogRead(Config::Pins::LDR) / 4095.0f, 0.0f, 1.0f);
  ldrF = ldrF * 0.9f + norm * 0.1f;
  const float target = 6.0f + ldrF * ldrF * 249.0f;
  brightness = brightness * 0.92f + target * 0.08f;
  ledcWrite(Config::Pins::TFT_LED, (int)brightness);
#endif
}

// ================= задача сети =================
static void netTask(void*) {
  net::feed();                                   // под общим сторожем не стоит — см. net::feed
  net::histLoad();
  net::wifiBegin();
  uint32_t lastHealth = 0;
  for (;;) {
    net::feed();
    if (gPortalActive) { web::dns.processNextRequest(); web::server.handleClient(); vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    if (gPortalWanted) { gPortalWanted = false; net::histSave(); web::portalStart(); continue; }
    if (gRestartWanted) { net::histSave(); Serial.println("[старт] история сохранена, перезапуск"); delay(100); ESP.restart(); }

    const uint32_t now = millis();
    const bool up = WiFi.status() == WL_CONNECTED;
    if (up && !net::wasConnected) {
      net::reconnects++;
      configTime(cfg.utcOff, 0, Config::ntpServer);
      net::wxTry = 0; net::mkTry = 0;             // сеть вернулась — сразу обновить
      Serial.printf("[Wi-Fi] подключено, %s, %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      web::onWifiUp();                            // страница настроек в домашней сети: http://air.local
    }
    if (!up && net::wasConnected) Serial.println("[Wi-Fi] связь потеряна");
    if (!up && now - net::lastWifiTry > Config::Intervals::WIFI_RETRY) {
      static bool scanned = false;
      Serial.printf("[Wi-Fi] нет связи с \"%s\" (код %d), пробую снова\n", cfg.ssid, (int)WiFi.status());
      if (!scanned) {                            // один раз показать, какие сети прибор вообще видит
        scanned = true; net::feed();
        WiFi.disconnect(); delay(200);            // во время попытки подключения поиск сетей не работает
        const int n = WiFi.scanNetworks();
        net::feed();
        for (int i = 0; i < n && i < 12; i++) Serial.printf("[Wi-Fi]   вижу: \"%s\" %d dBm, канал %d\n", WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i), (int)WiFi.channel(i));
        if (n <= 0) Serial.println("[Wi-Fi]   сетей не видно");
        WiFi.scanDelete();
      }
      net::wifiReconnect();
    }
    net::wasConnected = up;
    { Lock lk; G.wifi = up; G.rssi = up ? WiFi.RSSI() : 0; }

    if (gRefreshWanted) { gRefreshWanted = false; net::wxTry = 0; net::mkTry = 0; net::wxOkMs = 0; net::mkNext = app::ASSET_N; { Lock lk; G.mkAtMs = 0; } }

#if AIR_PRO
    if (up) {
      // погода
      const bool wxDue = !net::wxEver || now - net::wxOkMs > Config::Intervals::WEATHER;
      if (wxDue && (!net::wxTry || now - net::wxTry > Config::Intervals::WEATHER_RETRY)) {
        net::wxTry = millis() | 1;
        if (net::fetchWeather()) { net::wxEver = true; net::wxOkMs = millis(); }
      }
      // рынки: по одному инструменту за проход, чтобы не занимать сеть надолго
      uint32_t mkAt; { Lock lk; mkAt = G.mkAtMs; }
      if (net::mkNext >= app::ASSET_N) {
        const bool due = gMkEnabled && (!net::mkEver || !mkAt || now - mkAt > Config::Intervals::MARKETS);   // выключенные «Рынки» не качаем
        if (due && (!net::mkTry || now - net::mkTry > Config::Intervals::MARKETS_RETRY)) { net::mkTry = millis() | 1; net::mkNext = 0; net::mkAnyOk = false; }
      } else {
        tg::drop();
        if (net::fetchAsset(net::mkNext)) net::mkAnyOk = true;
        if (++net::mkNext >= app::ASSET_N) { net::mkEver = true; Lock lk; G.mkFresh = net::mkAnyOk; if (net::mkAnyOk) G.mkAtMs = millis() | 1; G.mkSeq++; }
      }
      tg::tick();
    }
#endif

    // история на флеш — раз в полчаса
    uint32_t dirty; { Lock lk; dirty = G.histDirty; }
    if (dirty >= Config::Intervals::HIST_SAVE_SLOTS) net::histSave();

    // строка здоровья раз в минуту: по ней видно утечки и переполнения стека задолго до сбоя
    if (now - lastHealth >= 60000) {
      lastHealth = now;
      Serial.printf("[здоровье] %lu мин | память %u КБ (мин %u, кусок %u) | PSRAM %u КБ | стеки ui %u, датчики %u, сеть %u, экран %u | %lu к/с, худший кадр %lu мс | Wi-Fi %d dBm, подключений %lu, ошибок сети %lu | ошибки BME %lu SGP %lu CO2 %lu PMS %lu\n",
                    (unsigned long)(now / 60000), (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(ESP.getFreePsram() / 1024),
                    (unsigned)uxTaskGetStackHighWaterMark(hUi), (unsigned)uxTaskGetStackHighWaterMark(hSens), (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)uxTaskGetStackHighWaterMark(hPush),
                    (unsigned long)gFrames, (unsigned long)gWorstFrameMs, up ? WiFi.RSSI() : 0, (unsigned long)net::reconnects, (unsigned long)net::httpFails,
                    (unsigned long)sens::errBme, (unsigned long)sens::errSgp, (unsigned long)sens::errCo2, (unsigned long)sens::errPms);
      gWorstFrameMs = 0;
    }
    for (int i = 0; i < 20; i++) { web::handle(); vTaskDelay(pdMS_TO_TICKS(10)); }   // страница настроек отвечает и в обычной работе
  }
}

// ================= экраны =================
static Screen screen = AIR_PRO ? SCR_FLIP : SCR_DASH;
static app::Live live;
static app::Clock clk;
static double lastT = 0;

// время анимаций. Раньше было micros() — 32 бита, обнуляется каждые 71,6 минуты: все «прошло с момента…» становились отрицательными
static inline double nowMs() { return (double)esp_timer_get_time() / 1000.0; }
static void hhmm(char* o) { if (clk.ok) snprintf(o, 6, "%02d:%02d", clk.hh, clk.mm); else strcpy(o, "--:--"); }
static void hdate(char* o) { if (clk.ok) snprintf(o, 40, "%s · %d %s", app::wday(clk.wday), clk.mday, app::month(clk.mon)); else o[0] = 0; }

#if AIR_PRO   // ---- всё, что нужно экранам PRO ----
static uint32_t seenWx = 0xFFFFFFFF, seenMk = 0xFFFFFFFF, chartsAt = 0;
static char footDate[40] = "", footOut[40] = "", footKey[160] = ""; static flip::Cell footCell[5];
static int flipMin = -1; static bool flipDim = false;

// Стрелки «растёт / падает»: значения каждые 30 с, сравнение с тем, что было 10 минут назад (после включения — с самым старым)
static const int TR_K[5] = {app::M_T, app::M_H, app::M_CO2, app::M_VOC, app::M_PM25};
static const float TR_MIN[5] = {0.3f, 2, 40, 15, 3};   // меньше этого — «ровно»
static float trBuf[5][20]; static bool trHas[5][20]; static int trN = 0; static uint32_t trAt = 0;
static void trendTick() {
  if (trN && millis() - trAt < 30000UL) return;
  trAt = millis();
  if (trN == 20) { for (int i = 0; i < 5; i++) { memmove(trBuf[i], trBuf[i] + 1, 19 * sizeof(float)); memmove(trHas[i], trHas[i] + 1, 19); } trN = 19; }
  for (int i = 0; i < 5; i++) { trBuf[i][trN] = live.v[TR_K[i]]; trHas[i][trN] = live.ok[TR_K[i]]; }
  trN++;
}
static int trendOf(int i) {
  if (trN < 5 || !live.ok[TR_K[i]] || !trHas[i][0]) return 0;   // первые 2 минуты стрелок нет
  const float d = live.v[TR_K[i]] - trBuf[i][0];
  return d > TR_MIN[i] ? 1 : d < -TR_MIN[i] ? -1 : 0;
}
static int wxIconOf(int code, bool day) {
  if (code <= 1) return day ? 0 : 1;
  if (code == 2) return day ? 2 : 3;
  if (code == 3) return 3;
  if (code == 45 || code == 48) return 4;
  if (code >= 95) return 7;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 6;
  return 5;
}
static int footIcon = -1, footScore = -1; static cvs::Col footScol;
static bool footerUpdate() {             // true — надписи вокруг часов изменились
  if (clk.ok) snprintf(footDate, sizeof footDate, "%s  ·  %d %s", app::wday(clk.wday), clk.mday, app::month(clk.mon)); else strcpy(footDate, L("ЖДУ ВРЕМЯ ПО WI-FI", "WAITING FOR TIME · WI-FI"));
  { app::Wx w; { Lock lk; w = G.wx; }      // улица: «21° ясно» (если погода свежее 3 часов)
    if (w.ok && millis() - w.atMs < 3UL * 3600000UL) { const int t = (int)lroundf(w.temp); snprintf(footOut, sizeof footOut, "%s%d°  %s", t < 0 ? "−" : "", abs(t), app::wxDesc(w.code)); footIcon = wxIconOf(w.code, w.day); }
    else { footOut[0] = 0; footIcon = -1; } }
  static const int mi[5] = {0, 1, 2, 3, 5};   // строки air::M: температура, влажность, CO₂, VOC, PM2.5
  const char* lab[5] = {L("ТЕМП.", "TEMP"), L("ВЛАГА", "HUMID"), "CO₂", "VOC", "PM2.5"};
  for (int i = 0; i < 5; i++) {
    const air::Metric& m = air::M[mi[i]]; flip::Cell& c = footCell[i]; const float v = live.v[m.k];
    c.lab = lab[i]; c.ok = live.ok[m.k]; c.col = air::col(m, v); c.tr = trendOf(i);
    if (i == 0) { snprintf(c.val, sizeof c.val, "%.1f°", v); for (char* p = c.val; *p; p++) if (*p == '.' && !en()) *p = ','; }
    else if (i == 1) snprintf(c.val, sizeof c.val, "%d%%", (int)lroundf(v));
    else snprintf(c.val, sizeof c.val, "%d", (int)lroundf(v));
  }
  { float iv[air::NK]; air::indexValues(live, iv); const air::Score sc = air::scoreOf(iv, live.ok); footScore = sc.score;   // PM2.5 — среднее за 10 мин
    footScol = sc.score >= 75 ? cvs::hex(0x4FB0B8) : sc.score >= 50 ? cvs::hex(0xFFB54A) : cvs::hex(0xE5483B); }   // те же границы, что шкала «Индекса»
  char k[160]; int n = snprintf(k, sizeof k, "%s|%s|%d|%d", footDate, footOut, footIcon, footScore);
  for (int i = 0; i < 5 && n < (int)sizeof k - 20; i++) n += snprintf(k + n, sizeof k - n, "|%s%d%d%d", footCell[i].val, footCell[i].ok, (int)footCell[i].col.r, footCell[i].tr);
  if (!strcmp(k, footKey)) return false;
  strcpy(footKey, k);
  return true;
}
static void footerDraw() { flip::info(footDate, footOut, footIcon, footCell, 5, footScore, footScol); }
static int wxSceneOf(const app::Wx& w) {  // сцена по коду погоды, дню/ночи и близости рассвета или заката
  bool dusk = false;
  if (clk.ok && w.sunrise >= 0 && w.sunset >= 0) { const int m = clk.hh * 60 + clk.mm; dusk = abs(m - w.sunrise) <= 40 || abs(m - w.sunset) <= 40; }
  return wx::sceneFor(w.code, w.day, dusk);
}
static void enterFlip() {
  screen = SCR_FLIP;
  flipDim = !clk.ok; flip::dim = flipDim; flipMin = clk.ok ? clk.hh * 60 + clk.mm : 0;
  flip::begin(flipMin, (float)nowMs());
  footerUpdate(); footerDraw();
  presentAll();
}
static void chartsFeed() {               // история -> 25 точек на график
  static float d[app::PTS]; static bool has[app::PTS];
  static const int src[charts::NM] = {app::M_T, app::M_H, app::M_CO2, app::M_VOC, app::M_PM25};
  Lock lk;
  for (int i = 0; i < charts::NM; i++) { const int m = src[i]; const int n = app::histSeries(G.hist, m, live.v[m], live.ok[m], d, has); charts::setData(i, d, n > 0, has); }
  charts::noxOk = app::histSeries(G.hist, app::M_NOX, live.v[app::M_NOX], live.ok[app::M_NOX], charts::NOX) > 0;
  const bool a = app::histSeries(G.hist, app::M_PM1, live.v[app::M_PM1], live.ok[app::M_PM1], charts::PM1) > 0;
  const bool b = app::histSeries(G.hist, app::M_PM10, live.v[app::M_PM10], live.ok[app::M_PM10], charts::PM10) > 0;
  charts::pmSideOk = a && b;
}
static void chartsTime() { if (clk.ok) charts::setEnd(clk.hh, clk.mm); else charts::setEnd(-1, 0); }
static int wxFill() {                    // погода из общего хранилища -> надписи; возвращает нужную сцену
  app::Wx w; uint32_t seq; { Lock lk; w = G.wx; seq = G.wxSeq; }
  seenWx = seq;
  wx::Data& d = wx::D;
  if (!w.ok) { d.ok = false; strcpy(d.city, L("ПОГОДА", "WEATHER")); d.sub[0] = 0; strcpy(d.desc, L("нет данных — жду Wi-Fi", "no data — waiting for Wi-Fi")); return WS_CLOUDY; }
  d.ok = true; strlcpy(d.city, w.city, sizeof d.city);
  const uint32_t ageMin = (millis() - w.atMs) / 60000;
  if (ageMin > 180) snprintf(d.sub, sizeof d.sub, L("— %lu ч назад", "— %lu h ago"), (unsigned long)(ageMin / 60));
  else if (clk.ok) snprintf(d.sub, sizeof d.sub, "— %s %02d:00", app::wday(clk.wday), w.hour0);
  else d.sub[0] = 0;
  d.t = (int)lroundf(w.temp); d.feels = (int)lroundf(w.feels); d.hi = (int)lroundf(w.hi); d.lo = (int)lroundf(w.lo);
  d.wind = (int)lroundf(w.wind); d.hum = w.hum; d.wa = w.windDir; strlcpy(d.wd, app::windName(w.windDir), sizeof d.wd);
  strlcpy(d.desc, app::wxDesc(w.code), sizeof d.desc);
  for (int i = 0; i < 7; i++) { d.hrs[i] = (int)lroundf(w.hr[i]); d.pr[i] = w.pr[i]; }
  d.hr0 = w.hour0;
  d.uv = w.uv >= 0 ? (int)lroundf(w.uv) : -1;
  if (w.sunrise >= 0 && w.sunset >= 0) { snprintf(d.rise, sizeof d.rise, "%02d:%02d", w.sunrise / 60, w.sunrise % 60); snprintf(d.set, sizeof d.set, "%02d:%02d", w.sunset / 60, w.sunset % 60); }
  else d.rise[0] = d.set[0] = 0;
  return wxSceneOf(w);
}
static void mkFill() {
  uint32_t at; bool fresh; bool wifi;
  static uint32_t drawnSeq = 0xFFFFFFFF; uint32_t seq;
  { Lock lk; memcpy(markets::A, G.mk, sizeof markets::A); seq = seenMk = G.mkSeq; at = G.mkAtMs; fresh = G.mkFresh; wifi = G.wifi; }
  { bool same = true; const char* c0 = nullptr;   // валюта в шапке — только если у всех одна
    for (const app::Asset& a : markets::A) if (a.cur[0]) { if (!c0) c0 = a.cur; else if (strcmp(c0, a.cur)) same = false; }
    snprintf(markets::cur, sizeof markets::cur, "%s", same ? (c0 ? c0 : "USD") : ""); }
  if (!wifi) strcpy(markets::note, L("нет сети", "offline"));
  else if (!at) strcpy(markets::note, fresh ? "" : L("загружаю…", "loading…"));
  else { const uint32_t m = (millis() - at) / 60000; if (m < 1) strcpy(markets::note, L("только что", "just now")); else snprintf(markets::note, sizeof markets::note, L("%lu мин назад", "%lu min ago"), (unsigned long)m); }
  if (seq != drawnSeq) { drawnSeq = seq; markets::dataChanged(); }   // новые котировки — пересобрать карточки
  else markets::timeChanged();                                        // иначе поменялась только подпись в шапке
}
// ---- сцена погоды собирается фоновой задачей (≈2 с), экран в это время живёт ----
static volatile bool wxBuilding = false; static volatile int wxBuildIdx = WS_CLOUDY;
static TaskHandle_t hWx;
static bool wxPendingShow = false; static uint32_t bgWxSeq = 0xFFFFFFFF; static int wxWant = WS_CLOUDY;
static void wxTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const uint32_t t0 = millis();
    wx::build(wxBuildIdx);
    Serial.printf("[погода] сцена %s собрана в фоне за %lu мс\n", WSCENES[wxBuildIdx].name, (unsigned long)(millis() - t0));
    wxBuilding = false;
  }
}
static bool wxReady(int sc) { return wx::built && wx::stateIdx == sc; }
static void wxStartBuild(int sc) { wxBuildIdx = sc; wxBuilding = true; xTaskNotifyGive(hWx); }
static void wxWait() { while (wxBuilding) { esp_task_wdt_reset(); delay(10); } }
#endif  // AIR_PRO
static void drawPortal() { portal::draw(); presentAll(); }   // QR-код и адрес — portal.h

// ---- плавный переход между экранами: старый кадр растворяется в новом ----
static uint8_t* xfPrev = nullptr; static float xfT = -1e9f; static bool xfFlip = false, started = false, xfArm = false;
constexpr float XF_MS = 380;
static bool xfBlend(float ms) {
  // первый кадр нового экрана бывает тяжёлым (собирается неподвижный слой): переход стартует, когда он уже готов
  if (xfArm) { xfArm = false; xfT = (float)nowMs(); ms = xfT; }
  const float ff = 1 - gx::easeOut3((ms - xfT) / XF_MS);
  if (ff <= 0 || !xfPrev) return false;
  const int a = (int)(ff * 255.f + .5f);
  for (int y = 0; y < gx::H; y++) { uint8_t* p = gx::rows[y]; const uint8_t* q = xfPrev + y * gx::W * 3; for (int x = 0; x < gx::W; x++, p += 3, q += 3) gx::blend(p, q[0], q[1], q[2], a); }
  return true;
}

// экран, на котором был прибор, переживает перезапуск (плановый ночной, сторож) — память RTC не стирается без снятия питания
RTC_NOINIT_ATTR static uint32_t rtcScreenMagic; RTC_NOINIT_ATTR static uint32_t rtcScreen;
static void enter(Screen s) {
  const uint32_t e0 = millis();
  if (s < SCR_N) { rtcScreen = s; rtcScreenMagic = 0xA1B2C3D4u; }
  const bool fade = started && xfPrev && screen < SCR_N && s < SCR_N;
  if (fade) gx::copyTo(xfPrev);                  // последний показанный кадр старого экрана
  const float now = (float)nowMs();
  screen = s;
  switch (s) {
    case SCR_DASH: dash::setLive(live.v, live.ok); dash::begin(now); break;
    case SCR_PORTAL: drawPortal(); break;
#if AIR_PRO
    case SCR_FLIP: if (fade) { flipDim = !clk.ok; flip::dim = flipDim; flipMin = clk.ok ? clk.hh * 60 + clk.mm : 0; footerUpdate(); xfFlip = true; } else enterFlip(); break;
    case SCR_AIR: { float iv[air::NK]; air::indexValues(live, iv); air::setLive(iv, live.ok); } air::begin(now); break;
    case SCR_CHARTS:
      // данные свежее минуты — готовые кадры годятся; иначе берём новые сразу (первый кадр считается до начала перехода)
      charts::begin(now);
      if (!charts::ready() || millis() - chartsAt > 60000) { chartsFeed(); chartsTime(); chartsAt = millis(); }
      break;
    case SCR_WEATHER: {
      wxWait();                                   // обычно сцена уже готова: её собирают заранее, как только приходит погода
      const int sc = wxFill();
      if (!wxReady(sc)) { wxStartBuild(sc); wxWait(); }
      wxPendingShow = false;
      wx::setState(sc, (float)nowMs(), (float)(nowMs() / 1000.0), false);
    } break;
    case SCR_MARKETS: mkFill(); markets::begin(now); break;
#endif
    default: break;
  }
  if (fade) { xfArm = true; xfT = (float)nowMs(); }
  started = true;
  gWorstFrameMs = 0;                             // худший кадр считаем заново для каждого экрана
  static const char* nm[] = {"часы", "показатели", "индекс", "графики", "погода", "рынки", "", "настройки"};
  Serial.printf("[экран] %s, вход %lu мс\n", nm[s], (unsigned long)(millis() - e0));
}

static void showFatal(const char* a, const char* b) {
  tft.fillScreen(0x0000); tft.setTextColor(0xF800); tft.setTextSize(2);
  tft.setCursor(10, 90); tft.print(a); tft.setCursor(10, 120); tft.print(b);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[старт] причина перезапуска %d, flash %u МБ, PSRAM %u КБ\n", (int)esp_reset_reason(), (unsigned)(ESP.getFlashChipSize() >> 20), (unsigned)(ESP.getPsramSize() / 1024));

  // сторож: зависшая задача = перезагрузка через 30 с, прибор сам возвращается в строй
  esp_task_wdt_config_t wc = {.timeout_ms = Config::WDT_TIMEOUT_MS, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wc);
  esp_task_wdt_add(nullptr);

  pinMode(Config::Pins::BTN, INPUT_PULLUP);
  backlightBegin();
  SPI.begin(Config::Pins::TFT_SCK, Config::Pins::TFT_MISO, Config::Pins::TFT_MOSI, Config::Pins::TFT_CS);
#if SCREEN_ST7789
  tft.init(240, 320); tft.setSPISpeed(Config::SPI_HZ); tft.invertDisplay(false);
#else
  tft.begin(Config::SPI_HZ);
#endif
  tft.setRotation(1);   // 320x240
  pinMode(Config::Pins::BTN, INPUT_PULLUP);
  tft.fillScreen(0x0000);
  tft.setTextColor(0xC618); tft.setTextSize(1); tft.setCursor(124, 116); tft.print("air monitor");

  if (!psramFound()) {   // без PSRAM кадру негде жить — честно пишем, что не так
    Serial.println("НЕТ PSRAM! Инструменты → PSRAM → OPI PSRAM");
    showFatal("No PSRAM!", "Tools > PSRAM > OPI");
    for (;;) { esp_task_wdt_reset(); delay(1000); }
  }
  // вся память выделяется здесь, один раз. Кадр RGB888 — кусками по 30 строк: быстрая внутренняя память, пока её остаётся с запасом для Wi-Fi, дальше PSRAM
  for (int y0 = 0; y0 < gx::H; y0 += 30) {
    uint8_t* blk = nullptr;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > 230 * 1024) blk = (uint8_t*)heap_caps_malloc(gx::W * 30 * 3, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!blk) blk = (uint8_t*)GX_ALLOC(gx::W * 30 * 3);
    if (!blk) { showFatal("Malloc fail", "frame"); for (;;) { esp_task_wdt_reset(); delay(1000); } }
    for (int y = 0; y < 30; y++) gx::rows[y0 + y] = blk + y * gx::W * 3;
  }
  outBuf[0] = (uint16_t*)GX_ALLOC(gx::W * gx::H * 2);
  outBuf[1] = (uint16_t*)GX_ALLOC(gx::W * gx::H * 2);
  if (!outBuf[0] || !outBuf[1]) { showFatal("Malloc fail", "out"); for (;;) { esp_task_wdt_reset(); delay(1000); } }
  gx::initDither();
  cvs::ensureBufs();
  xfPrev = (uint8_t*)GX_ALLOC(gx::W * gx::H * 3);
#if AIR_PRO
  air::prebuild();                              // шкала экрана «Индекс» — заранее, чтобы первый вход не тормозил
#endif
  cvs::initPhi();
  dash::begin(0); dash::frame(0, 0);             // и фон экрана показателей (кадр на экран не отправляется)

  gMux = xSemaphoreCreateMutex();
  gBtnQ = xQueueCreate(8, sizeof(uint8_t));
  freeSem[0] = xSemaphoreCreateBinary(); freeSem[1] = xSemaphoreCreateBinary();
  xSemaphoreGive(freeSem[0]); xSemaphoreGive(freeSem[1]);
  jobs = xQueueCreate(1, sizeof(Job));

  cfgLoad();
  memset(&G, 0, sizeof G);
  mkApply();                                     // активы экрана «Рынки» — из настроек
  app::histClear(G.hist);
  sens::begin();

  hUi = xTaskGetCurrentTaskHandle();
  xTaskCreatePinnedToCore(pushTask, "push", 4096, nullptr, 2, &hPush, 0);
  xTaskCreatePinnedToCore(buttonTask, "button", 2048, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(sens::task, "sensors", 6144, nullptr, 3, &hSens, 1);
  // Приоритет как у отправки кадров (2): на живых экранах она занимает ядро 0 почти целиком, и при приоритете 1 сети
  // доставалось ~2 % времени — TLS-рукопожатие шло 40–130 с и сервер рвал соединение (рынки не обновлялись, 16.11.2026).
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 2, &hNet, 0);
#if AIR_PRO
  xTaskCreatePinnedToCore(wxTask, "wxbuild", 6144, nullptr, 2, &hWx, 0);
#endif

  Serial.printf("[старт] %s версия %s, свободно: внутренней %u КБ, PSRAM %u КБ\n", AIR_PRO ? "PRO" : "бесплатная", Config::FW_VERSION, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  clk = clockNow();
#if AIR_PRO
  const bool back = esp_reset_reason() != ESP_RST_POWERON && rtcScreenMagic == 0xA1B2C3D4u && rtcScreen < SCR_N;
  enter(back && !(rtcScreen == SCR_MARKETS && !gMkEnabled) ? (Screen)rtcScreen : SCR_FLIP);
#else
  enter(SCR_DASH);
#endif
  lastT = nowMs() / 1000.0;
  if (!cfg.ssid[0]) { Serial.println("[настройки] сеть не задана — точка доступа и QR"); gPortalWanted = true; enter(SCR_PORTAL); }   // новый прибор
}

// Основные экраны (часы, показатели, индекс, погода, рынки) листаются ДВОЙНЫМ нажатием.
// Одиночное: с основного — на графики; на графиках — следующий показатель, после пятого — назад на тот же основной экран.
// Двойное на графиках — сразу назад на основной экран.
#if !AIR_PRO
static void pressSingle() {}               // в бесплатной версии один экран — листать нечего
static void pressDouble() {}
#else
RTC_NOINIT_ATTR static uint32_t rtcMain;   // с какого основного экрана ушли на графики (переживает перезапуск)
static Screen mainScr() { return rtcMain < SCR_N && rtcMain != SCR_CHARTS && !(rtcMain == SCR_MARKETS && !gMkEnabled) ? (Screen)rtcMain : SCR_FLIP; }
static void pressSingle() {
  if (screen != SCR_CHARTS) { rtcMain = screen; enter(SCR_CHARTS); charts::autoCycle = false; return; }   // вошли кнопкой — сами не листаются
  if (charts::next((float)nowMs())) { static const char* nm[charts::NM] = {"температура", "влажность", "CO2", "VOC и NOx", "частицы"}; Serial.printf("[графики] %s\n", nm[charts::sel]); return; }
  enter(mainScr());
}
static void pressDouble() {
  if (screen == SCR_CHARTS) { enter(mainScr()); return; }
  Screen s = (Screen)((screen + 1) % SCR_N); if (s == SCR_CHARTS) s = (Screen)((s + 1) % SCR_N);
  if (s == SCR_MARKETS && !gMkEnabled) s = (Screen)((s + 1) % SCR_N);   // «Рынки» выключены в настройках
  enter(s);
}
#endif

void loop() {
  esp_task_wdt_reset();
  if (Config::DAILY_RESTART_HOUR >= 0 && !gPortalActive && screen != SCR_PORTAL) {
    static uint32_t askedAt = 0;
    const uint32_t upMin = (uint32_t)(esp_timer_get_time() / 60000000LL);
    if (!askedAt && ((upMin > 20 * 60 && clk.ok && clk.hh == Config::DAILY_RESTART_HOUR) || upMin > 30 * 60)) {
      Serial.printf("[старт] плановый перезапуск, работал %lu ч\n", (unsigned long)(upMin / 60)); gRestartWanted = true; askedAt = millis() | 1;
    }
    if (askedAt && millis() - askedAt > 20000) { delay(50); ESP.restart(); }   // сеть не ответила — перезапуск без сохранения
  }
  if (!gPortalActive && millis() - net::beatMs > net::NET_STUCK_MS && millis() > net::NET_STUCK_MS) {
    Serial.printf("[сеть] задача молчит %lu с — перезагрузка\n", (unsigned long)((millis() - net::beatMs) / 1000)); delay(100); ESP.restart();
  }
  backlightTick();
  const uint32_t f0 = millis();

  // кнопка и монитор порта
  uint8_t ev;
  while (xQueueReceive(gBtnQ, &ev, 0) == pdTRUE) {
    if (ev == EV_LONG) {
      if (screen == SCR_PORTAL) { Serial.println("[настройки] выход — перезагрузка"); delay(100); ESP.restart(); }
      gPortalWanted = true; enter(SCR_PORTAL);
    } else if (screen != SCR_PORTAL) { if (ev == EV_DOUBLE) pressDouble(); else pressSingle(); }
  }
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'n' && screen != SCR_PORTAL) pressSingle();
    else if (c == 'N' && screen != SCR_PORTAL) pressDouble();
    else if (c == 'r') gRefreshWanted = true;
    else if (c == 'p' && screen != SCR_PORTAL) { gPortalWanted = true; enter(SCR_PORTAL); }   // веб-настройки без кнопки
    else if (c == 'd') {                         // показания сейчас
      static const char* nm[app::M_N] = {"T", "H", "CO2", "VOC", "NOx", "PM1", "PM2.5", "PM10"};
      Serial.print("[датчики]");
      for (int m = 0; m < app::M_N; m++) { if (live.ok[m]) Serial.printf(" %s %.1f", nm[m], live.v[m]); else Serial.printf(" %s --", nm[m]); }
      Serial.printf(" | время %s | Wi-Fi %s\n", clk.ok ? "есть" : "нет", WiFi.status() == WL_CONNECTED ? "есть" : "нет");
      Serial.printf("[кнопка] пин %d сейчас %d (1 — отпущена)\n", Config::Pins::BTN, (int)digitalRead(Config::Pins::BTN));
      Serial.printf("[экран] %lu к/с, худший кадр %lu мс, внутренней памяти %u КБ\n", (unsigned long)gFrames, (unsigned long)gWorstFrameMs, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
      Serial.printf("[память] PSRAM свободно %u КБ\n", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    }
  }
  if (screen == SCR_PORTAL) { delay(50); return; }
#if AIR_PRO
  if (screen == SCR_MARKETS && !gMkEnabled) enter(SCR_FLIP);   // «Рынки» выключили со страницы настроек, пока экран был открыт
#endif

  { Lock lk; live = G.live; }
  clk = clockNow();
#if AIR_PRO
  trendTick();
#endif
  const double ms = nowMs(), t = ms / 1000.0;
  const float dt = (float)fmax(0.0, fmin(0.1, t - lastT));
  lastT = t;
  bool drew = true;

#if AIR_PRO
  // пришла новая погода — заранее собрать её сцену в фоне, пока пользователь на другом экране
  { app::Wx w; { Lock lk; w = G.wx; }     // сцена зависит и от времени (закат), поэтому считаем каждый проход
    wxWant = w.ok ? wxSceneOf(w) : WS_CLOUDY;
    if (screen != SCR_WEATHER && !wxBuilding && !wxReady(wxWant)) wxStartBuild(wxWant); }
#endif

  switch (screen) {
    case SCR_DASH:
      dash::setLive(live.v, live.ok); hhmm(dash::hhmm); hdate(dash::date);
      dash::step((float)ms, dt); dash::frame((float)t, (float)ms); xfBlend((float)ms); presentAll();
      break;
#if AIR_PRO
    case SCR_FLIP: {
      if (xfFlip) {                                                   // переход на часы: кадр целиком, пока старый экран растворяется
        flip::begin(flipMin, (float)ms); footerDraw();
        if (!xfBlend((float)ms)) xfFlip = false;
        presentAll(); break;
      }
      if (flipDim != !clk.ok) { enterFlip(); break; }                 // время появилось (или пропало) — перерисовать целиком
      if (clk.ok && clk.hh * 60 + clk.mm != flipMin) { flipMin = clk.hh * 60 + clk.mm; flip::setM(flipMin, (float)ms, false); }
      int x0, x1; drew = false;
      if (flip::frame((float)ms, x0, x1)) { present(x0, flip::RY0, x1 - x0, flip::RY1 - flip::RY0); drew = true; }
      if (footerUpdate()) { footerDraw(); present(0, flip::TY0, gx::W, flip::TY1 - flip::TY0); present(0, flip::FY0, gx::W, flip::FY1 - flip::FY0); drew = true; }
      if (flip::colonStep((float)ms)) { present(flip::CX0, flip::CY0, flip::CX1 - flip::CX0, flip::CY1 - flip::CY0); drew = true; }
      if (!drew) delay(20);
    } break;
    case SCR_AIR:
      { float iv[air::NK]; air::indexValues(live, iv); air::setLive(iv, live.ok); } hhmm(air::hhmm); hdate(air::date);
      air::step((float)ms, dt); air::frame((float)t, (float)ms); xfBlend((float)ms); presentAll();
      break;
    case SCR_CHARTS:
      if (millis() - chartsAt > 60000 && ms - charts::t0ms > 2000 && ms - charts::fadeT > 800) { chartsFeed(); chartsTime(); chartsAt = millis(); }   // раз в минуту — свежее «сейчас»
      charts::frame((float)t, (float)ms); xfBlend((float)ms); presentAll();
      break;
    case SCR_WEATHER: {
      if (wxBuilding) { drew = false; delay(15); break; }             // новая сцена собирается в фоне: на экране стоит прежний кадр
      if (wxPendingShow) { wxPendingShow = false; wx::setState(wx::stateIdx, (float)ms, (float)t, false); if (wx::prev) wx::fadeT = (float)ms; }   // готово — плавно сменить
      uint32_t seq; { Lock lk; seq = G.wxSeq; }
      if (seq != seenWx || !wxReady(wxWant)) {                       // пришла свежая погода или наступил закат/рассвет
        const int sc = wxFill();
        if (!wxReady(sc)) {                                          // погода сменила тип: запомнить кадр, собрать новую сцену
          if (!wx::prev) wx::prev = (uint8_t*)GX_ALLOC(gx::W * gx::H * 3);
          if (wx::prev) gx::copyTo(wx::prev);
          wxStartBuild(sc); wxPendingShow = true; drew = false; break;
        }
        wx::dataChanged();
      }
      wx::step(dt, (float)t); wx::frame((float)t, (float)ms); xfBlend((float)ms); presentAll();
    } break;
    case SCR_MARKETS: {
      uint32_t seq; { Lock lk; seq = G.mkSeq; }
      static uint32_t noteAt = 0;
      if (seq != seenMk || millis() - noteAt > 30000) { noteAt = millis(); mkFill(); }
      hhmm(markets::hhmm); hdate(markets::date);
      static char lastHm[6] = ""; if (strcmp(lastHm, markets::hhmm)) { strcpy(lastHm, markets::hhmm); markets::timeChanged(); }
      markets::frame((float)t, (float)ms); xfBlend((float)ms); presentAll();
    } break;
#endif
    default: break;
  }

  // статистика кадров для строки здоровья
  static uint32_t frames = 0, statAt = 0;
  if (drew) { frames++; const uint32_t d = millis() - f0; if (d > gWorstFrameMs) gWorstFrameMs = d; }
  if (millis() - statAt >= 2000) { gFrames = frames * 1000 / max<uint32_t>(1, millis() - statAt); frames = 0; statAt = millis(); }
}
