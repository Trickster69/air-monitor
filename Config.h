// Все пины, интервалы и значения по умолчанию — только здесь.
#pragma once
#include <Arduino.h>

// какой экран подключён: 0 — старая ILI9341 (TN), 1 — красная плата ST7789V (IPS, без инверсии)
#define SCREEN_ST7789 0

// Версия: есть папка src/pro (закрытый репозиторий air-monitor-pro) — полная, нет — бесплатная (только «Показатели»)
#if __has_include("src/pro/pro.h")
#define AIR_PRO 1
#else
#define AIR_PRO 0
#endif

namespace Config {

constexpr const char* FW_VERSION = "2.8";             // видна на странице настроек; менять при каждой заметной правке
constexpr const char* PRO_URL = "";                   // ссылка «Купить PRO» на странице настроек бесплатной версии (Бусти); пусто — без ссылки

// ---- Wi-Fi и погода по умолчанию (меняются в веб-настройках: держать кнопку 5 с) ----
// Сеть по умолчанию пустая: новый прибор сам открывает точку доступа и QR для настройки. Свою сеть сюда НЕ вписывать — код публичный.
inline const char* ssid = "";
inline const char* password = "";
inline const char* ntpServer = "pool.ntp.org";
inline const long gmtOffset_sec = 3 * 3600;        // до первого ответа погоды; дальше смещение берётся из Open-Meteo
namespace Weather {
constexpr const char* CITY_QUERY = "Moscow";
constexpr const char* TIMEZONE = "Europe/Moscow";
}

namespace Pins {
// экран (SPI)
constexpr int TFT_CS = 7, TFT_DC = 3, TFT_RST = 10, TFT_SCK = 4, TFT_MISO = 5, TFT_MOSI = 6;
// датчики
constexpr int I2C_SDA = 8, I2C_SCL = 9;            // BME280 + SGP41
constexpr int MHZ_RX = 17, MHZ_TX = 18;            // MH-Z19C: TXD датчика -> 17, RXD датчика -> 18
constexpr int PMS_RX = 15, PMS_TX = 16;            // PMS5003: TX датчика -> 15, RX датчика -> 16
constexpr int BTN = 12;                            // кнопка на GND
// авто-яркость (пока не припаяно — см. AUTO_BRIGHTNESS): LED экрана -> 14, фоторезистор -> 2
constexpr int TFT_LED = 14;
constexpr int LDR = 2;                             // только ADC1 (GPIO 1..10): ADC2 мешает Wi-Fi
}

// 1 — подсветка через пин TFT_LED по фоторезистору LDR; 0 — пины не трогаем (LED экрана сидит на 3V3)
#define AUTO_BRIGHTNESS 1

constexpr uint32_t SPI_HZ = 40000000;              // 80 МГц на макетке даёт сдвиги и битые цвета
constexpr float TEMP_OFFSET_C = 0.0f;              // поправка температуры BME280 (плата греет датчик) — подобрать по градуснику

namespace Intervals {                              // мс
constexpr uint32_t WEATHER = 20UL * 60000, WEATHER_RETRY = 60000;
constexpr uint32_t MARKETS = 15UL * 60000, MARKETS_RETRY = 60000;
constexpr uint32_t HIST_SAVE_SLOTS = 2;            // сохранять историю раз в 2 слота (10 мин): при отключении питания теряется не больше 10 минут
constexpr uint32_t WIFI_RETRY = 15000;
}

constexpr uint32_t BUTTON_DEBOUNCE_MS = 25, BUTTON_LONG_PRESS_MS = 5000;
constexpr uint32_t BUTTON_DOUBLE_MS = 350;   // второе нажатие не позже этого после отпускания — двойной клик (одиночный срабатывает с этой задержкой)
constexpr uint32_t WDT_TIMEOUT_MS = 30000;
// Плановый перезапуск раз в сутки, ночью: страховка от всего, что копится неделями (память, точность счётчика времени
// анимаций, сбой экрана от помехи). История перед этим сохраняется, прибор возвращается на тот же экран. -1 — выключить.
constexpr int DAILY_RESTART_HOUR = 4;

}  // namespace Config
