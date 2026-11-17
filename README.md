# Монитор воздуха на ESP32-S3

Домашний монитор качества воздуха: CO₂, пыль PM1/PM2.5/PM10, VOC и NOx, температура, влажность — на цветном TFT-экране 2.8".

**Прошить прибор из браузера:** https://trickster69.github.io/air-monitor/ (Chrome или Edge на компьютере).

## Железо
- ESP32-S3 N16R8 (16 МБ flash, 8 МБ PSRAM)
- экран 2.8" 320×240 — ILI9341 или ST7789V (`SCREEN_ST7789` в `Config.h`)
- BME280 (температура, влажность), SGP41 (VOC, NOx), MH-Z19C (CO₂), PMS5003 (пыль), кнопка

Пины — в `Config.h`.

## Версии
- **Бесплатная** (этот репозиторий): экран «Показатели» со всеми датчиками, страница настроек (`http://air.local`), настройка Wi-Fi по QR-коду, обновление прошивки по Wi-Fi.
- **PRO** (файл прошивки для подписчиков на Бусти): ещё перекидные часы, индекс воздуха, графики за 12 часов, погода, курсы валют и акций, уведомления в Telegram.

## Сборка
Arduino IDE или `arduino-cli`, ядро esp32 3.x: плата ESP32S3 Dev Module · PSRAM **OPI PSRAM** · Flash **16MB** · Partition Scheme **Custom** (`partitions.csv` из папки скетча).

Библиотеки: Adafruit ILI9341 / Adafruit ST7735 and ST7789, Adafruit GFX, Adafruit BME280, Sensirion I2C SGP41, Sensirion Gas Index Algorithm, ArduinoJson 7.

```
arduino-cli compile -b esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom .
```

Страница настроек лежит в `web/index.html`; после правки — `python web/make_page.py` (пересобирает `web_page.h`).

## Первое включение
Новый прибор сам открывает точку доступа **ESP32-TFT-AIR** (пароль `12345678`) и показывает QR-код на экране. Подключитесь телефоном — откроется страница настроек, там выбирается Wi-Fi. Потом настройки открываются из домашней сети по адресу `http://air.local`; держать кнопку 5 с — снова точка доступа.

## Лицензия
MIT — см. `LICENSE`.
