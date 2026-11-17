// Язык интерфейса прибора: 0 — русский, 1 — английский, 2 — испанский. Ставится при старте из настроек; смена языка — с перезагрузкой
// (готовые кадры экранов собраны с надписями, поэтому язык на лету не меняем).
// В коде строки пишутся парой L("рус", "eng"); испанский берётся из таблицы lang_es.h по английской строке
// (нет в таблице — останется английская). Проверка, что всё переведено: python lang_check.py
#pragma once
#include <stdint.h>
#include <string.h>

inline uint8_t gLang = 0;
enum : uint8_t { LANG_RU = 0, LANG_EN = 1, LANG_ES = 2, LANG_N = 3 };
inline bool en() { return gLang == LANG_EN; }
inline bool es() { return gLang == LANG_ES; }
inline bool decComma() { return gLang != LANG_EN; }   // 22,8 по-русски и по-испански, 22.8 по-английски
inline const char* langCode(uint8_t l) { return l == LANG_EN ? "en" : l == LANG_ES ? "es" : "ru"; }

#include "lang_es.h"
inline const char* esOf(const char* en_) {
  if (!en_) return en_;
  for (const auto& p : LANG_ES_TABLE) if (!strcmp(p[0], en_)) return p[1];
  return en_;
}
// строка на текущем языке: L("ПОГОДА", "WEATHER")
inline const char* L(const char* ru, const char* en_) { return gLang == LANG_RU ? ru : gLang == LANG_ES ? esOf(en_) : en_; }
