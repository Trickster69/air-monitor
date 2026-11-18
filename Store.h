// Общие данные между задачами. Правило: у каждого поля один писатель; читают копией под коротким замком.
//   датчики  -> live, hist          сеть -> wx, mk, mkNote
// Настройки (RuntimeConfig) читает и пишет только задача сети.
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"
#include "data.h"

struct Shared {
  app::Live live;
  uint32_t lastMs[4];            // последний удачный опрос: BME, SGP, CO2, PMS
  app::Hist hist; uint32_t histSeq; uint32_t histDirty;   // histDirty — слотов с последнего сохранения
  app::Wx wx; uint32_t wxSeq;
  app::Asset mk[app::ASSET_N]; uint32_t mkSeq; uint32_t mkAtMs; bool mkFresh;
  bool wifi; int rssi;
};
inline Shared G;
inline SemaphoreHandle_t gMux;
struct Lock { Lock() { xSemaphoreTake(gMux, portMAX_DELAY); } ~Lock() { xSemaphoreGive(gMux); } };

// экраны по порядку листания (тип объявлен здесь, а не в .ino: Arduino ставит прототипы функций раньше объявлений скетча)
enum Screen { SCR_FLIP, SCR_DASH, SCR_AIR, SCR_CHARTS, SCR_WEATHER, SCR_MARKETS, SCR_N, SCR_PORTAL };

// события кнопки и просьбы от экрана к сети
enum : uint8_t { EV_SHORT = 1, EV_LONG = 2, EV_DOUBLE = 3 };
inline QueueHandle_t gBtnQ;
inline volatile bool gPortalWanted = false, gPortalActive = false, gRefreshWanted = false, gRestartWanted = false, gMemProbe = false;
inline volatile uint32_t gFrames = 0, gWorstFrameMs = 0;
inline volatile bool gMkEnabled = true;       // экран «Рынки» включён (копия cfg.mkEnabled для экрана)
inline TaskHandle_t hUi, hSens, hNet, hPush;

// ---------- настройки в NVS (ключи те же, что в старой прошивке) ----------
struct RuntimeConfig {
  char ssid[32] = "", password[64] = "", city[32] = "", timezone[48] = "";
  bool telegramEnabled = false;
  char telegramToken[96] = "", telegramChatId[24] = "";
  bool telegramAlertPM = true, telegramAlertCO2 = true, telegramAlertVOC = true;
  uint32_t telegramMuteUntilEpoch = 0;
  uint16_t thPM25 = 16, thCO2 = 1000, thVOC = 250;   // пороги уведомлений (VOC — индекс SGP41)
  bool mkEnabled = true;                             // показывать экран «Рынки»
  uint8_t lang = LANG_RU;                            // язык экранов, страницы настроек и Telegram
  struct MkCfg { char sym[20], name[28], tick[10]; char kind; } mk[app::ASSET_N] = {};   // пять активов, первый — на большой карточке
  // кэш геокодинга и часового пояса
  char geoCity[32] = "", cityShown[32] = ""; float lat = 0, lon = 0; int32_t utcOff = Config::gmtOffset_sec;
};
inline RuntimeConfig cfg;
inline Preferences cfgPrefs;

inline void cfgDefaults() {
  if (!cfg.ssid[0]) { strlcpy(cfg.ssid, Config::ssid, sizeof cfg.ssid); strlcpy(cfg.password, Config::password, sizeof cfg.password); }
  if (!cfg.city[0]) strlcpy(cfg.city, Config::Weather::CITY_QUERY, sizeof cfg.city);
  if (!cfg.timezone[0]) strlcpy(cfg.timezone, Config::Weather::TIMEZONE, sizeof cfg.timezone);
  for (int i = 0; i < app::ASSET_N; i++) if (!cfg.mk[i].sym[0]) {
    const app::AssetDef& d = app::ASSET_DEF[i];
    strlcpy(cfg.mk[i].sym, d.sym, sizeof cfg.mk[i].sym); strlcpy(cfg.mk[i].name, L(d.name, d.nameEn), sizeof cfg.mk[i].name); strlcpy(cfg.mk[i].tick, d.tick, sizeof cfg.mk[i].tick); cfg.mk[i].kind = d.kind;
  }
}
static inline void getStr(const char* k, char* dst, size_t n) { dst[0] = 0; if (cfgPrefs.isKey(k)) cfgPrefs.getBytes(k, dst, n); dst[n - 1] = 0; }
inline void cfgLoad() {
  cfgPrefs.begin("cfg", false);
  getStr("ssid", cfg.ssid, sizeof cfg.ssid); getStr("pass", cfg.password, sizeof cfg.password);
  getStr("city", cfg.city, sizeof cfg.city); getStr("tz", cfg.timezone, sizeof cfg.timezone);
  cfg.telegramEnabled = cfgPrefs.getBool("tg_en", false);
  getStr("tg_tok", cfg.telegramToken, sizeof cfg.telegramToken); getStr("tg_chat", cfg.telegramChatId, sizeof cfg.telegramChatId);
  cfg.telegramAlertPM = cfgPrefs.getBool("tg_pm", true); cfg.telegramAlertCO2 = cfgPrefs.getBool("tg_co2", true); cfg.telegramAlertVOC = cfgPrefs.getBool("tg_voc", true);
  cfg.telegramMuteUntilEpoch = cfgPrefs.getUInt("tg_mute", 0);
  cfg.thPM25 = cfgPrefs.getUShort("tg_th_pm", 16); cfg.thCO2 = cfgPrefs.getUShort("tg_th_co2", 1000); cfg.thVOC = cfgPrefs.getUShort("tg_th_voi", 250);
  getStr("geo_city", cfg.geoCity, sizeof cfg.geoCity); getStr("geo_name", cfg.cityShown, sizeof cfg.cityShown);
  cfg.mkEnabled = cfgPrefs.getBool("mk_en", true);
  if (cfgPrefs.getBytesLength("mk_list") == sizeof cfg.mk) cfgPrefs.getBytes("mk_list", cfg.mk, sizeof cfg.mk);
  for (auto& m : cfg.mk) { m.sym[sizeof m.sym - 1] = 0; m.name[sizeof m.name - 1] = 0; m.tick[sizeof m.tick - 1] = 0; }
  gMkEnabled = cfg.mkEnabled;
  cfg.lang = cfgPrefs.getUChar("lang", LANG_RU); if (cfg.lang >= LANG_N) cfg.lang = LANG_RU; gLang = cfg.lang;   // до cfgDefaults: имена активов по умолчанию — на этом языке
  cfg.lat = cfgPrefs.getFloat("geo_lat", 0); cfg.lon = cfgPrefs.getFloat("geo_lon", 0); cfg.utcOff = cfgPrefs.getInt("utc_off", Config::gmtOffset_sec);
  cfgDefaults();
}
inline void cfgSave() {
  cfgPrefs.putBytes("ssid", cfg.ssid, sizeof cfg.ssid); cfgPrefs.putBytes("pass", cfg.password, sizeof cfg.password);
  cfgPrefs.putBytes("city", cfg.city, sizeof cfg.city); cfgPrefs.putBytes("tz", cfg.timezone, sizeof cfg.timezone);
  cfgPrefs.putBool("tg_en", cfg.telegramEnabled);
  cfgPrefs.putBytes("tg_tok", cfg.telegramToken, sizeof cfg.telegramToken); cfgPrefs.putBytes("tg_chat", cfg.telegramChatId, sizeof cfg.telegramChatId);
  cfgPrefs.putBool("tg_pm", cfg.telegramAlertPM); cfgPrefs.putBool("tg_co2", cfg.telegramAlertCO2); cfgPrefs.putBool("tg_voc", cfg.telegramAlertVOC);
  cfgPrefs.putUInt("tg_mute", cfg.telegramMuteUntilEpoch);
  cfgPrefs.putUShort("tg_th_pm", cfg.thPM25); cfgPrefs.putUShort("tg_th_co2", cfg.thCO2); cfgPrefs.putUShort("tg_th_voi", cfg.thVOC);
  cfgPrefs.putBool("mk_en", cfg.mkEnabled); cfgPrefs.putBytes("mk_list", cfg.mk, sizeof cfg.mk);
  cfgPrefs.putUChar("lang", cfg.lang);
}
// выбранные активы -> общие данные (котировки обнуляются и качаются заново)
inline void mkApply() {
  Lock lk;
  for (int i = 0; i < app::ASSET_N; i++) app::assetSet(G.mk[i], cfg.mk[i].sym, cfg.mk[i].name, cfg.mk[i].tick, cfg.mk[i].kind);
  G.mkSeq++; G.mkAtMs = 0; G.mkFresh = false;
}
inline void cfgSaveGeo() {
  cfgPrefs.putBytes("geo_city", cfg.geoCity, sizeof cfg.geoCity); cfgPrefs.putBytes("geo_name", cfg.cityShown, sizeof cfg.cityShown);
  cfgPrefs.putFloat("geo_lat", cfg.lat); cfgPrefs.putFloat("geo_lon", cfg.lon);
}

// время: верим только после синхронизации по NTP
inline bool timeOk() { return time(nullptr) > 1700000000; }
inline app::Clock clockNow() {
  app::Clock c{}; time_t t = time(nullptr);
  if (t <= 1700000000) return c;
  struct tm tmv; localtime_r(&t, &tmv);
  c.ok = true; c.hh = tmv.tm_hour; c.mm = tmv.tm_min; c.wday = tmv.tm_wday; c.mday = tmv.tm_mday; c.mon = tmv.tm_mon;
  return c;
}
