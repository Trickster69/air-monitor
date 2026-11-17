// Веб-настройки. Страница — web/index.html (в прошивке лежит сжатой: web_page.h, собирается python web/make_page.py).
// Открывается двумя путями:
//   1) из домашней сети — http://air.local или по адресу прибора, без кнопки;
//   2) долгое нажатие (5 с) — точка доступа ESP32-TFT-AIR, 192.168.4.1, на экране QR-код (portal.h).
//      Если домашний Wi-Fi был подключён, он остаётся подключён (AP+STA): работают проверка города и тест Telegram.
// Всё выполняется в задаче сети (она же хозяин настроек cfg).
#pragma once
#include <ESPmDNS.h>
#include <Update.h>
#include "Net.h"
#include "portal.h"
#include "web_page.h"

// Telegram есть только в PRO (src/pro/Telegram.h); в бесплатной версии — пустышка
#if AIR_PRO
inline void tgStop() { tg::drop(); tg::inited = false; }
#else
inline void tgStop() {}
#endif

namespace web {

inline WebServer server(80);
inline DNSServer dns;
inline bool started = false, mdnsOn = false;

static inline void jstr(String& o, const char* s) {          // строка в JSON
  o += '"';
  for (; *s; s++) { const uint8_t c = (uint8_t)*s; if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) o += ' '; else o += (char)c; }
  o += '"';
}
static inline void json(int code, const String& body) { server.sendHeader("Cache-Control", "no-store"); server.send(code, "application/json; charset=utf-8", body); }
static inline void reply(bool ok, const char* key = nullptr, const char* text = nullptr) {
  String p = ok ? "{\"ok\":1" : "{\"ok\":0";
  if (key) { p += ",\""; p += key; p += "\":"; jstr(p, text); }
  p += '}'; json(200, p);
}
static inline void restartSoon() { delay(800); net::histSave(); ESP.restart(); }

inline void hPage() {
  server.sendHeader("Content-Encoding", "gzip"); server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", (PGM_P)PAGE_GZ, PAGE_GZ_LEN);
}
inline void hNotFound() {
  if (gPortalActive) { server.sendHeader("Location", String("http://") + portal::AP_IP + "/"); server.send(302, "text/plain", ""); }   // телефон сам откроет страницу после подключения
  else server.send(404, "text/plain", "not found");
}

// уровень для цвета плитки: 0 — норма, 1 — внимание, 2 — плохо
static inline int lvl(int m, float v) {
  switch (m) {
    case app::M_CO2: return v < 1000 ? 0 : v < 1500 ? 1 : 2;
    case app::M_PM25: return v <= 15 ? 0 : v <= 35 ? 1 : 2;
    case app::M_VOC: return v < 150 ? 0 : v < 250 ? 1 : 2;
    case app::M_NOX: return v < 20 ? 0 : v < 150 ? 1 : 2;
    case app::M_H: return v >= 30 && v <= 60 ? 0 : 1;
    default: return 0;
  }
}
inline void hState() {
  app::Live l; { Lock lk; l = G.live; }
  const bool up = WiFi.status() == WL_CONNECTED;
  String p; p.reserve(900);
  p += "{\"mode\":\""; p += gPortalActive ? "ap" : "sta"; p += "\",\"fw\":\""; p += Config::FW_VERSION; p += " \\u00b7 " __DATE__ "\",\"pro\":"; p += AIR_PRO ? "true" : "false"; p += ",\"proUrl\":"; jstr(p, Config::PRO_URL); p += ",\"up\":"; p += (unsigned long)(millis() / 60000);
  p += ",\"wifi\":{\"ok\":"; p += up ? "true" : "false"; p += ",\"ssid\":"; jstr(p, cfg.ssid); p += ",\"rssi\":"; p += up ? (int)WiFi.RSSI() : 0;
  p += ",\"ip\":\""; if (up) p += WiFi.localIP().toString(); p += "\"},\"live\":{";
  auto mv = [&](const char* k, int m, int dec, bool last = false) {
    p += '"'; p += k; p += "\":[";
    if (l.ok[m]) { char b[16]; snprintf(b, sizeof b, "%.*f", dec, l.v[m]); p += b; p += ','; p += lvl(m, l.v[m]); } else p += "null,0";
    p += last ? "]" : "],";
  };
  mv("co2", app::M_CO2, 0); mv("pm", app::M_PM25, 0); mv("t", app::M_T, 1); mv("h", app::M_H, 0); mv("voc", app::M_VOC, 0); mv("nox", app::M_NOX, 0, true);
  p += "},\"cfg\":{\"ssid\":"; jstr(p, cfg.ssid); p += ",\"hasPass\":"; p += cfg.password[0] ? "true" : "false";
  p += ",\"city\":"; jstr(p, cfg.city); p += ",\"cityShown\":"; jstr(p, !strcmp(cfg.geoCity, cfg.city) ? cfg.cityShown : "");
  p += ",\"tg\":"; p += cfg.telegramEnabled ? "true" : "false"; p += ",\"hasTok\":"; p += cfg.telegramToken[0] ? "true" : "false"; p += ",\"chat\":"; jstr(p, cfg.telegramChatId);
  p += ",\"thCO2\":"; p += cfg.thCO2; p += ",\"thPM\":"; p += cfg.thPM25; p += ",\"thVOC\":"; p += cfg.thVOC;
  p += ",\"alCO2\":"; p += cfg.telegramAlertCO2 ? "true" : "false"; p += ",\"alPM\":"; p += cfg.telegramAlertPM ? "true" : "false"; p += ",\"alVOC\":"; p += cfg.telegramAlertVOC ? "true" : "false";
  p += "},\"mk\":{\"on\":"; p += cfg.mkEnabled ? "true" : "false"; p += ",\"list\":[";
  float mp[app::ASSET_N]; char mc[app::ASSET_N][5];              // текущая цена — чтобы на странице было видно, что актив нашёлся
  { Lock lk; for (int i = 0; i < app::ASSET_N; i++) { const bool same = !strcmp(G.mk[i].sym, cfg.mk[i].sym) && G.mk[i].ok; mp[i] = same ? G.mk[i].price : 0; memcpy(mc[i], G.mk[i].cur, 5); mc[i][4] = 0; } }
  for (int i = 0; i < app::ASSET_N; i++) {
    const auto& m = cfg.mk[i]; const char k[2] = {m.kind ? m.kind : 'e', 0};
    if (i) p += ',';
    p += "{\"s\":"; jstr(p, m.sym); p += ",\"n\":"; jstr(p, m.name); p += ",\"t\":"; jstr(p, m.tick); p += ",\"k\":"; jstr(p, k);
    if (mp[i] > 0) { char b[24]; snprintf(b, sizeof b, "%.6g", mp[i]); p += ",\"p\":"; p += b; p += ",\"c\":"; jstr(p, mc[i]); }
    p += '}';
  }
  p += "]}}";
  json(200, p);
}

// поиск актива по названию или тикеру (Yahoo Finance): акции, крипта, фонды, индексы, сырьё, валюты
inline void hMsearch() {
  if (WiFi.status() != WL_CONNECTED) { server.send(503, "text/plain", "offline"); return; }
  String q = server.arg("q"); q.trim();
  if (!q.length()) { json(200, "[]"); return; }
  char e[120], url[260]; net::urlEnc(q.c_str(), e, sizeof e);
  snprintf(url, sizeof url, "https://query1.finance.yahoo.com/v1/finance/search?q=%s&quotesCount=8&newsCount=0&listsCount=0", e);
  JsonDocument filter(&net::psAlloc), doc(&net::psAlloc);
  JsonObject f = filter["quotes"][0].to<JsonObject>();
  f["symbol"] = true; f["shortname"] = true; f["longname"] = true; f["quoteType"] = true; f["exchDisp"] = true;
  tgStop();                              // два защищённых соединения сразу — слишком много памяти
  if (!net::getJson(url, true, doc, filter)) { server.send(503, "text/plain", "offline"); return; }
  String p = "["; int cnt = 0;
  for (JsonObject o : doc["quotes"].as<JsonArray>()) {
    const char* sym = o["symbol"] | ""; const char* t = o["quoteType"] | "";
    char k = 0;
    if (!strcmp(t, "CRYPTOCURRENCY")) k = 'c'; else if (!strcmp(t, "EQUITY")) k = 'e'; else if (!strcmp(t, "ETF")) k = 't';
    else if (!strcmp(t, "INDEX")) k = 'i'; else if (!strcmp(t, "FUTURE")) k = 'f'; else if (!strcmp(t, "CURRENCY")) k = 'x';
    if (!k || !sym[0] || strlen(sym) > 19) continue;           // опционы и паевые фонды не берём
    const char* nm = o["shortname"] | ""; if (!nm[0]) nm = o["longname"] | sym;
    const char kk[2] = {k, 0};
    if (cnt++) p += ',';
    p += "{\"s\":"; jstr(p, sym); p += ",\"n\":"; jstr(p, nm); p += ",\"k\":"; jstr(p, kk); p += ",\"e\":"; jstr(p, o["exchDisp"] | ""); p += '}';
  }
  p += ']';
  json(200, p);
}

inline void hScan() {
  net::feed();
  const int n = WiFi.scanNetworks(false, false, false, 150);   // ~2 с на все каналы
  net::feed();
  String p = "["; int cnt = 0;
  for (int i = 0; i < n && cnt < 14; i++) {                    // список уже отсортирован по силе сигнала
    const String s = WiFi.SSID(i); if (!s.length()) continue;
    bool dup = false; for (int j = 0; j < i && !dup; j++) dup = WiFi.SSID(j) == s;   // одна сеть с нескольких точек — один раз
    if (dup) continue;
    if (cnt++) p += ',';
    p += "{\"s\":"; jstr(p, s.c_str()); p += ",\"r\":"; p += (int)WiFi.RSSI(i); p += ",\"l\":"; p += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1; p += '}';
  }
  p += ']';
  WiFi.scanDelete();
  Serial.printf("[настройки] поиск сетей: %d\n", cnt);
  json(200, p);
}

inline void hCity() {
  if (WiFi.status() != WL_CONNECTED) { server.send(503, "text/plain", "offline"); return; }
  String q = server.arg("q"); q.trim();
  char name[48], country[48]; float la, lo;
  const int r = net::geoLookup(q.c_str(), name, sizeof name, country, sizeof country, la, lo);
  if (r < 0) { server.send(503, "text/plain", "offline"); return; }
  if (!r) { reply(false); return; }
  String full = name; if (country[0]) { full += ", "; full += country; }
  reply(true, "name", full.c_str());
}

// тест Telegram по тому, что сейчас вписано в форму (ещё не сохранено); пустой токен — берётся сохранённый
inline void hTgTest() {
#if !AIR_PRO
  reply(false, "err", "Уведомления в Telegram есть в PRO-версии"); return;
#else
  if (WiFi.status() != WL_CONNECTED) { reply(false, "err", "Прибор не в интернете — сначала подключите Wi-Fi"); return; }
  String tok = server.arg("telegram_token"), chat = server.arg("telegram_chat_id"); tok.trim(); chat.trim();
  if (!tok.length()) tok = cfg.telegramToken;
  if (!tok.length()) { reply(false, "err", "Впишите токен бота"); return; }
  char keepTok[sizeof cfg.telegramToken]; strlcpy(keepTok, cfg.telegramToken, sizeof keepTok);
  strlcpy(cfg.telegramToken, tok.c_str(), sizeof cfg.telegramToken);
  tg::drop(); tg::client.setInsecure(); tg::client.setHandshakeTimeout(8);
  bool ok; const char* key = nullptr; const char* text = nullptr;
  if (chat.length()) {
    tg::T.clear(); tg::T.add("Монитор воздуха на связи. Уведомления будут приходить сюда.");
    ok = tg::send(chat.c_str());
    if (!ok) { key = "err"; text = "Telegram не принял сообщение — проверьте токен и Chat ID"; }
  } else {                                                     // чата ещё нет — проверяем хотя бы токен
    net::feed();
    tg::http.setConnectTimeout(5000); tg::http.setTimeout(5000);
    ok = tg::http.begin(tg::client, tg::api("getMe")) && tg::http.GET() == 200;
    tg::http.end(); net::feed();
    key = ok ? "msg" : "err"; text = ok ? "Токен верный. Сохраните и отправьте боту /bind" : "Telegram не принял токен — проверьте его";
  }
  tg::http.end(); tg::client.stop(); tg::inited = false;
  strlcpy(cfg.telegramToken, keepTok, sizeof cfg.telegramToken);
  Serial.printf("[настройки] тест Telegram: %s\n", ok ? "ок" : "ошибка");
  reply(ok, key, text);
#endif
}

inline void hSave() {
  auto arg = [&](const char* name) { String v = server.arg(name); v.trim(); return v; };
  const String ssid = arg("ssid"), pass = server.arg("password"), city = arg("city"), tok = arg("telegram_token");
  if (!ssid.length()) { reply(false, "err", "Укажите сеть Wi-Fi"); return; }
  bool wifiChanged = false;
  if (ssid != cfg.ssid) { wifiChanged = true; strlcpy(cfg.ssid, ssid.c_str(), sizeof cfg.ssid); strlcpy(cfg.password, pass.c_str(), sizeof cfg.password); }   // новая сеть: пустой пароль = открытая
  else if (pass.length() && pass != cfg.password) { wifiChanged = true; strlcpy(cfg.password, pass.c_str(), sizeof cfg.password); }                    // та же сеть: пустое поле — пароль не трогаем
  const bool cityChanged = city.length() && city != cfg.city;
  if (cityChanged) strlcpy(cfg.city, city.c_str(), sizeof cfg.city);
  if (AIR_PRO) cfg.telegramEnabled = server.hasArg("telegram_enabled");   // в бесплатной блок Telegram заблокирован и не приходит — не трогаем
  if (tok.length()) strlcpy(cfg.telegramToken, tok.c_str(), sizeof cfg.telegramToken);
  if (server.hasArg("telegram_chat_id")) strlcpy(cfg.telegramChatId, arg("telegram_chat_id").c_str(), sizeof cfg.telegramChatId);
  if (server.hasArg("th_co2")) {                               // пороги приходят только вместе с блоком Telegram
    cfg.thCO2 = constrain(arg("th_co2").toInt(), 400, 5000); cfg.thPM25 = constrain(arg("th_pm").toInt(), 1, 500); cfg.thVOC = constrain(arg("th_voc").toInt(), 50, 500);
    cfg.telegramAlertCO2 = server.hasArg("al_co2"); cfg.telegramAlertPM = server.hasArg("al_pm"); cfg.telegramAlertVOC = server.hasArg("al_voc");
  }
  if (server.hasArg("mk_s0")) {                                // раздел «Рынки»
    cfg.mkEnabled = server.hasArg("mk_on"); gMkEnabled = cfg.mkEnabled;
    bool ch = false;
    for (int i = 0; i < app::ASSET_N; i++) {
      const String id(i); auto& m = cfg.mk[i];
      const String sy = arg(("mk_s" + id).c_str()), nm = arg(("mk_n" + id).c_str()), tk = arg(("mk_t" + id).c_str()), kd = arg(("mk_k" + id).c_str());
      if (!sy.length() || sy.length() >= sizeof m.sym) continue;
      const String name = nm.length() ? nm : tk; const char kind = kd.length() ? kd[0] : 'e';
      if (sy != m.sym || name != m.name || tk != m.tick || kind != m.kind) {
        ch = true; strlcpy(m.sym, sy.c_str(), sizeof m.sym); strlcpy(m.name, name.c_str(), sizeof m.name); strlcpy(m.tick, tk.length() ? tk.c_str() : sy.c_str(), sizeof m.tick); m.kind = kind;
      }
    }
    if (ch) { mkApply(); net::mkNext = app::ASSET_N; net::mkEver = false; Serial.println("[настройки] набор активов изменён"); }
    net::mkTry = 0;                                            // котировки — сразу
  }
  cfgDefaults(); cfgSave();
  const bool restart = wifiChanged || gPortalActive;           // из точки доступа выходим перезагрузкой; дома перезагрузка нужна только при смене сети
  Serial.printf("[настройки] сохранено%s%s%s\n", wifiChanged ? ", сеть изменена" : "", cityChanged ? ", город изменён" : "", restart ? ", перезагрузка" : "");
  json(200, restart ? "{\"ok\":1,\"restart\":true}" : "{\"ok\":1,\"restart\":false}");
  if (restart) restartSoon();
  tgStop();                              // Telegram подхватит новые настройки на следующем проходе
  if (cityChanged) { net::wxTry = 0; net::wxOkMs = 0; net::wxEver = false; }   // погода — сразу по новому городу
}

// ---- обновление прошивки файлом .bin из браузера (второй слот, partitions.csv) ----
inline String otaErr; inline bool otaOk = false; inline size_t otaBytes = 0;
inline void hUpdateData() {
  HTTPUpload& u = server.upload(); net::feed();
  if (u.status == UPLOAD_FILE_START) {
    otaErr = ""; otaOk = false; otaBytes = 0; tgStop();
    Serial.printf("[прошивка] принимаю %s\n", u.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) otaErr = Update.errorString();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!otaBytes && u.currentSize && u.buf[0] != 0xE9) otaErr = "это не файл прошивки";   // у прошивки ESP32 первый байт всегда E9
    if (!otaErr.length() && Update.write(u.buf, u.currentSize) != u.currentSize) otaErr = Update.errorString();
    otaBytes += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END) {
    if (!otaErr.length()) { if (Update.end(true)) otaOk = true; else otaErr = Update.errorString(); }
  } else if (u.status == UPLOAD_FILE_ABORTED) { Update.abort(); otaErr = "загрузка прервана"; }
}
inline void hUpdateDone() {
  if (otaOk) { Serial.printf("[прошивка] записано %u байт, перезагрузка\n", (unsigned)otaBytes); server.send(200, "text/plain", "ok"); restartSoon(); }
  Serial.printf("[прошивка] ошибка: %s\n", otaErr.c_str());
  if (Update.isRunning()) Update.abort();
  server.send(500, "text/plain; charset=utf-8", otaErr.length() ? otaErr : String("файл не принят"));
}

inline void start() {
  if (started) return;
  server.on("/", HTTP_GET, hPage);
  server.on("/api/state", HTTP_GET, hState);
  server.on("/api/scan", HTTP_GET, hScan);
  server.on("/api/city", HTTP_GET, hCity);
  server.on("/api/msearch", HTTP_GET, hMsearch);
  server.on("/api/tgtest", HTTP_POST, hTgTest);
  server.on("/save", HTTP_POST, hSave);
  server.on("/update", HTTP_POST, hUpdateDone, hUpdateData);
  server.on("/api/restart", HTTP_POST, []() { reply(true); Serial.println("[настройки] перезагрузка со страницы"); restartSoon(); });
  server.on("/api/exit", HTTP_POST, []() { reply(true); Serial.println("[настройки] выход без изменений"); restartSoon(); });
  server.on("/api/reset", HTTP_POST, []() { reply(true); Serial.println("[настройки] сброс настроек"); cfgPrefs.clear(); restartSoon(); });
  server.onNotFound(hNotFound);
  server.begin();
  started = true;
}
// Wi-Fi подключился: страница доступна в домашней сети
inline void onWifiUp() {
  start();
  if (!mdnsOn && MDNS.begin("air")) { MDNS.addService("http", "tcp", 80); mdnsOn = true; }
  Serial.printf("[настройки] страница: http://air.local или http://%s\n", WiFi.localIP().toString().c_str());
}
// долгое нажатие: точка доступа прибора. Домашний Wi-Fi, если был подключён, остаётся
inline void portalStart() {
  tgStop();
  if (WiFi.status() != WL_CONNECTED) { WiFi.setAutoReconnect(false); WiFi.disconnect(false, false); delay(200); }   // иначе попытки подключиться сбивают точку доступа
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(portal::AP_SSID, portal::AP_PASS);
  dns.start(53, "*", WiFi.softAPIP());
  start();
  gPortalActive = true;
  Serial.printf("[настройки] точка доступа %s, пароль %s, адрес %s\n", portal::AP_SSID, portal::AP_PASS, portal::AP_IP);
}
inline void handle() { if (started) server.handleClient(); }

}  // namespace web
