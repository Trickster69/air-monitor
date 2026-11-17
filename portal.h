// Экран «Настройки» (долгое нажатие): QR-код подключает телефон к точке доступа прибора, справа — то же самое словами.
// Код посчитан заранее (segno, версия 3, уровень M) для строки WIFI:T:WPA;S:ESP32-TFT-AIR;P:12345678;;
// Если поменять имя или пароль точки доступа (AP_SSID / AP_PASS) — пересчитать QR:
//   python -c "import segno; ..."  (см. CLAUDE.md, раздел «Веб-настройки»)
#pragma once
#include "canvas.h"

namespace portal {
using namespace cvs;

constexpr const char* AP_SSID = "ESP32-TFT-AIR";
constexpr const char* AP_PASS = "12345678";
constexpr const char* AP_IP = "192.168.4.1";

constexpr int QN = 29;
constexpr uint32_t QR[QN] = {
  0x1FCE7D7F, 0x10574541, 0x175CA65D, 0x1751B95D, 0x1742BA5D, 0x10491641, 0x1FD5557F, 0x000F8D00, 0x1A4C04ED, 0x0D566712,
  0x0F5379C3, 0x021C6B98, 0x07918166, 0x0006CD2E, 0x1FFB38D9, 0x02158335, 0x17394078, 0x10678008, 0x01A7F2F9, 0x1CFEFA84,
  0x1DFC71DA, 0x05180B00, 0x095A517F, 0x0919EF41, 0x03F7805D, 0x16AC9B5D, 0x14EA715D, 0x0B3CDE41, 0x0E91217F};

inline void draw() {
  resetState();
  const Col CR = hex(0xF4E7D4), AM = hex(0xFFB54A), TL = hex(0x4FB0B8), INK = hex(0x16100D);
  fillRect(0, 0, gx::W, gx::H, linear(0, 0, 0, gx::H).stop(0, hex(0x1b130f)).stop(1, hex(0x0b0807)));
  fillText(F_ON10B, L("НАСТРОЙКИ", "SETUP"), 12, 22, withA(CR, 0.92f), AL_LEFT, 2.4f);
  fillText(F_JB85M, L("с телефона", "from your phone"), 282, 22, withA(CR, 0.55f), AL_RIGHT);

  // QR: кремовая плашка с полями в 2 модуля, модуль 5 px
  constexpr int M = 5, QX = 12, QY = 34, PAD = 10, QS = QN * M + PAD * 2;
  { const Paint f = solid(CR); fastRRect(QX, QY, QS, QS, 8, &f, nullptr, 0); }
  { const Paint ink = solid(INK);
    for (int y = 0; y < QN; y++) for (int x = 0; x < QN;) {
      if (!(QR[y] >> x & 1)) { x++; continue; }
      int e = x; while (e < QN && (QR[y] >> e & 1)) e++;
      fillRect(QX + PAD + x * M, QY + PAD + y * M, (e - x) * M, M, ink); x = e;
    } }

  const float X = QX + QS + 14;
  fillText(F_LI14I, L("наведите камеру", "scan the"), X, 50, CR);
  fillText(F_LI14I, L("на код", "QR code"), X, 67, CR);
  fillRectAA(X, 79, 308 - X, 1, 0xF4, 0xE7, 0xD4, 0.16f);
  fillText(F_JB8M, L("ИЛИ ВРУЧНУЮ · WI-FI", "OR MANUALLY · WI-FI"), X, 96, withA(CR, 0.5f));
  fillText(F_ON105B, AP_SSID, X, 111, AM);
  fillText(F_JB8M, L("ПАРОЛЬ", "PASSWORD"), X, 133, withA(CR, 0.5f));
  fillText(F_JB105B, AP_PASS, X, 148, CR);
  fillText(F_JB8M, L("АДРЕС В БРАУЗЕРЕ", "OPEN IN BROWSER"), X, 170, withA(CR, 0.5f));
  fillText(F_JB105B, AP_IP, X, 185, TL);
  fillText(F_JB85M, L("держать кнопку 5 с — выйти без изменений", "hold the button 5 s — exit without changes"), 12, 228, withA(CR, 0.55f));
}

}  // namespace portal
