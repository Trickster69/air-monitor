# Страница настроек web/index.html -> web_page.h (gzip в памяти прошивки). Запускать после каждой правки страницы:
#   python web/make_page.py
import gzip, os, re
d = os.path.dirname(os.path.abspath(__file__))
s = open(os.path.join(d, 'index.html'), encoding='utf-8').read()
s = re.sub(r'\n\s+', '\n', s)                      # отступы не нужны
g = gzip.compress(s.encode('utf-8'), 9, mtime=0)
out = ['// СОБРАНО АВТОМАТИЧЕСКИ из web/index.html — не править, запускать python web/make_page.py', '#pragma once', '#include <Arduino.h>',
       'constexpr size_t PAGE_GZ_LEN = %d;' % len(g), 'const uint8_t PAGE_GZ[] PROGMEM = {']
for i in range(0, len(g), 32): out.append(','.join(str(b) for b in g[i:i + 32]) + ',')
out.append('};')
open(os.path.join(d, '..', 'web_page.h'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print('страница %d байт -> gzip %d байт' % (len(s.encode('utf-8')), len(g)))
