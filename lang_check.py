"""Проверка перевода: все английские строки L("рус", "eng") прибора; без ключей — каких нет в испанской таблице lang_es.h."""
import re, glob, os, sys
R = os.path.dirname(os.path.abspath(__file__))
S = r'"(?:[^"\\]|\\.)*"'
PAT = re.compile(r'\bL\(\s*((?:' + S + r'\s*)+),\s*((?:' + S + r'\s*)+)\)')
seen = []
for f in glob.glob(os.path.join(R, '*.h')) + glob.glob(os.path.join(R, '*.ino')) + glob.glob(os.path.join(R, 'src', 'pro', '*.h')):
    if 'assets' in f or 'web_page' in f or 'lang_es' in f or f.endswith('lang.h'): continue
    s = open(f, encoding='utf-8').read()
    for m in PAT.finditer(s):
        en = ''.join(x[1:-1] for x in re.findall(S, m.group(2)))
        if en not in seen: seen.append(en)
if '--list' not in sys.argv:
    t = open(os.path.join(R, 'lang_es.h'), encoding='utf-8').read()
    keys = set(''.join(x[1:-1] for x in re.findall(S, k)) for k in re.findall(r'\{\s*((?:' + S + r'\s*)+),', t))
    miss = [e for e in seen if e not in keys]
    print('всего', len(seen), 'нет перевода', len(miss))
    for e in miss: print('  ', e)
else:
    for e in seen: print(e)
