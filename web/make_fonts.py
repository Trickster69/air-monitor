# Шрифты страницы настроек, встроенные в саму страницу (в режиме точки доступа интернета нет):
# Onest (текст), Literata (заголовки и цифры), JetBrains Mono (данные) — урезаны до русских, латинских и испанских букв.
# python web/make_fonts.py  ->  web/fonts.css (@font-face с woff2 в base64); make_page.py вставляет его вместо <!--FONTS-->.
# Исходники — graphics/_style/fonts (OFL).
import base64, io, os
from fontTools.ttLib import TTFont
from fontTools.subset import Subsetter, Options
from fontTools.varLib import instancer

D = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(D, '..', '..', '..', 'graphics', '_style', 'fonts')
TEXT = (''.join(chr(c) for c in range(0x20, 0x7F)) + 'АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя'
        + 'áéíóúñüÁÉÍÓÚÑÜ¿¡«»“”‘’—–…·•₂³µ°×→←↑↓✓№ ')
FONTS = [  # файл, семейство, оси (None — убрать ось, (a,b) — оставить диапазон), вес в CSS
    ('Onest-Variable.ttf', 'Onest', {'wght': (400, 600)}, '400 600'),
    ('Literata-Variable.ttf', 'Literata', {'opsz': 24, 'wght': 400}, '400'),
    ('JetBrainsMono-Variable.ttf', 'JB Mono', {'wght': 500}, '500'),
]
css = []
for fn, fam, axes, w in FONTS:
    f = TTFont(os.path.join(SRC, fn))
    f = instancer.instantiateVariableFont(f, axes)
    o = Options(); o.flavor = 'woff2'; o.layout_features = ['kern', 'liga', 'tnum', 'lnum']; o.name_IDs = []; o.notdef_outline = True
    s = Subsetter(o); s.populate(text=TEXT); s.subset(f)
    b = io.BytesIO(); f.flavor = 'woff2'; f.save(b)
    data = b.getvalue()
    css.append("@font-face{font-family:'%s';font-weight:%s;font-display:swap;src:url(data:font/woff2;base64,%s) format('woff2')}" % (fam, w, base64.b64encode(data).decode()))
    print('%-28s %6d байт' % (fn, len(data)))
open(os.path.join(D, 'fonts.css'), 'w', encoding='utf-8').write('\n'.join(css) + '\n')
