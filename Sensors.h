// Задача датчиков: раз в секунду опрашивает BME280, SGP41 (VOC + NOx), MH-Z19C, PMS5003.
// Нигде не ждёт датчик: нет ответа — показатель помечается «нет данных», остальное работает. Датчик вернули — подхватывается сам.
// Только эта задача трогает I2C и оба UART датчиков.
#pragma once
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <SensirionI2CSgp41.h>
#include <VOCGasIndexAlgorithm.h>
#include <NOxGasIndexAlgorithm.h>
#include <esp_task_wdt.h>
#include "Store.h"

namespace sens {

inline Adafruit_BME280 bme;
inline SensirionI2CSgp41 sgp;
inline VOCGasIndexAlgorithm vocAlg;
inline NOxGasIndexAlgorithm noxAlg;
inline bool bmeOk = false; inline uint8_t bmeAddr = 0x76;
inline int sgpWarm = 10;                       // секунд прогрева SGP41 (не больше 10 — так требует датчик)
inline uint32_t errBme = 0, errSgp = 0, errCo2 = 0, errPms = 0;

// сколько можно жить без свежего ответа, мс
constexpr uint32_t STALE_BME = 10000, STALE_SGP = 6000, STALE_CO2 = 25000, STALE_PMS = 10000;
constexpr uint32_t CO2_PREHEAT_MS = 60000, PMS_PREHEAT_MS = 30000;

static inline bool i2cHas(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
inline bool findBme() {
  for (uint8_t a : {0x76, 0x77}) if (i2cHas(a) && bme.begin(a, &Wire)) { bmeAddr = a; return true; }
  return false;
}

// ---- MH-Z19C: команда на одном тике, ответ читаем на следующем ----
inline bool co2Asked = false;
inline void co2Ask() {
  static const uint8_t cmd[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};
  while (Serial1.available()) Serial1.read();
  Serial1.write(cmd, 9); co2Asked = true;
}
inline int co2Take() {                          // ppm или -1
  if (!co2Asked) return -1;
  co2Asked = false;
  if (Serial1.available() < 9) { errCo2++; return -1; }
  uint8_t r[9]; Serial1.readBytes(r, 9);
  if (r[0] != 0xFF || r[1] != 0x86) { errCo2++; return -1; }
  uint8_t sum = 0; for (int i = 1; i < 8; i++) sum += r[i];
  if ((uint8_t)(0xFF - sum + 1) != r[8]) { errCo2++; return -1; }
  return r[2] * 256 + r[3];
}

// ---- PMS5003: разбор потока по байту, с пересинхронизацией ----
inline uint8_t pmBuf[32]; inline uint8_t pmIdx = 0;
inline bool pmsParse(int& pm1, int& pm25, int& pm10) {
  bool got = false;
  int n = Serial2.available(); if (n > 256) n = 256;
  while (n-- > 0) {
    const uint8_t b = Serial2.read();
    if (pmIdx == 0) { if (b == 0x42) pmBuf[pmIdx++] = b; continue; }
    if (pmIdx == 1) { if (b == 0x4D) pmBuf[pmIdx++] = b; else { pmIdx = 0; if (b == 0x42) pmBuf[pmIdx++] = b; } continue; }
    pmBuf[pmIdx++] = b;
    if (pmIdx < 32) continue;
    pmIdx = 0;
    uint16_t calc = 0; for (int i = 0; i < 30; i++) calc += pmBuf[i];
    if (calc != (((uint16_t)pmBuf[30] << 8) | pmBuf[31])) { errPms++; continue; }
    pm1 = (pmBuf[10] << 8) | pmBuf[11]; pm25 = (pmBuf[12] << 8) | pmBuf[13]; pm10 = (pmBuf[14] << 8) | pmBuf[15];
    got = true;
  }
  return got;
}

inline void begin() {
  Wire.begin(Config::Pins::I2C_SDA, Config::Pins::I2C_SCL, 100000);
  Wire.setTimeOut(50);
  bmeOk = findBme();
  sgp.begin(Wire);
  Serial1.begin(9600, SERIAL_8N1, Config::Pins::MHZ_RX, Config::Pins::MHZ_TX);
  Serial2.begin(9600, SERIAL_8N1, Config::Pins::PMS_RX, Config::Pins::PMS_TX);
  static const uint8_t abcOff[9] = {0xFF, 0x01, 0x79, 0x00, 0, 0, 0, 0, 0x86};   // автокалибровку MH-Z19 выключаем, как в старой прошивке
  Serial1.write(abcOff, 9);
  Serial.printf("[датчики] BME280: %s (0x%02X)\n", bmeOk ? "есть" : "не найден", bmeAddr);
}

inline void task(void*) {
  esp_task_wdt_add(nullptr);
  float v[app::M_N]; uint32_t last[4] = {0, 0, 0, 0}; bool ever[4] = {false, false, false, false};
  for (float& x : v) x = 0;
  uint32_t tick = 0, slotStartMs = millis();
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    esp_task_wdt_reset();
    const uint32_t now = millis();

    // BME280 — раз в 2 с
    if (tick % 2 == 0) {
      if (bmeOk && !i2cHas(bmeAddr)) { bmeOk = false; errBme++; }
      if (!bmeOk) bmeOk = findBme();
      if (bmeOk) {
        const float t = bme.readTemperature() + Config::TEMP_OFFSET_C, h = bme.readHumidity();
        if (!isnan(t) && !isnan(h) && t > -40 && t < 85 && h >= 0 && h <= 100) { v[app::M_T] = t; v[app::M_H] = h; last[0] = now; ever[0] = true; }
        else errBme++;
      }
    }
    const bool thOk = ever[0] && now - last[0] < STALE_BME;

    // SGP41 — строго раз в секунду (так считает индекс алгоритм Sensirion), с поправкой на температуру и влажность
    {
      const uint16_t rh = thOk ? (uint16_t)(v[app::M_H] * 65535 / 100) : 0x8000;
      const uint16_t tc = thOk ? (uint16_t)((v[app::M_T] + 45) * 65535 / 175) : 0x6666;
      uint16_t sVoc = 0, sNox = 0;
      if (sgpWarm > 0) { if (sgp.executeConditioning(rh, tc, sVoc) == 0) sgpWarm--; else errSgp++; }
      else if (sgp.measureRawSignals(rh, tc, sVoc, sNox) == 0) {
        const int32_t vi = vocAlg.process(sVoc), ni = noxAlg.process(sNox);
        if (vi > 0 && ni > 0) { v[app::M_VOC] = vi; v[app::M_NOX] = ni; last[1] = now; ever[1] = true; }   // 0 — алгоритм ещё разгоняется
      } else { errSgp++; sgpWarm = 10; }
    }

    // MH-Z19C — раз в 5 с
    if (tick % 5 == 1) {
      const int ppm = co2Take();
      if (ppm >= 300 && ppm <= 10000 && now > CO2_PREHEAT_MS) { v[app::M_CO2] = ppm; last[2] = now; ever[2] = true; }
    }
    if (tick % 5 == 0) co2Ask();

    // PMS5003 — что пришло за секунду
    {
      int a, b, c;
      if (pmsParse(a, b, c) && now > PMS_PREHEAT_MS && a <= 2000 && b <= 2000 && c <= 2000) { v[app::M_PM1] = a; v[app::M_PM25] = b; v[app::M_PM10] = c; last[3] = now; ever[3] = true; }
    }

    app::Live l;
    const bool ok4[4] = {thOk, ever[1] && now - last[1] < STALE_SGP, ever[2] && now - last[2] < STALE_CO2, ever[3] && now - last[3] < STALE_PMS};
    static const uint8_t src[app::M_N] = {0, 0, 2, 1, 1, 3, 3, 3};
    for (int m = 0; m < app::M_N; m++) { l.v[m] = v[m]; l.ok[m] = ok4[src[m]]; }

    {
      Lock lk;
      G.live = l; memcpy(G.lastMs, last, sizeof last);
      app::histAdd(G.hist, l);
      // слот истории — 5 минут: по часам, если время известно, иначе по millis
      bool commit = false;
      if (timeOk()) {
        const uint32_t slot = (uint32_t)(time(nullptr) / app::SLOT_S);
        if (G.hist.lastSlot == 0) G.hist.lastSlot = slot;
        else if (slot > G.hist.lastSlot) { app::histShift(G.hist, (int)min<uint32_t>(slot - G.hist.lastSlot - 1, app::HN)); G.hist.lastSlot = slot; commit = true; }
        else if (slot + 2 < G.hist.lastSlot) G.hist.lastSlot = slot;   // часы перевели назад
      } else if (now - slotStartMs >= app::SLOT_S * 1000UL) commit = true;
      if (commit) { app::histCommit(G.hist); slotStartMs = now; G.histSeq++; G.histDirty++; }
    }
    tick++;
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
  }
}

}  // namespace sens
