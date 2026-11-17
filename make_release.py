"""Выпуск версии: собирает бесплатную и PRO прошивку и раскладывает файлы.

    python make_release.py            # собрать обе и разложить
    python make_release.py --no-build # только разложить уже собранное

Бесплатная  -> docs/firmware/ + docs/manifest.json (сайт-прошивальщик на GitHub Pages)
PRO         -> release/air-pro-<версия>.bin (файл для Бусти; папка release в git не попадает)
Бесплатная собирается из копии скетча без папки src/pro — так же, как её соберёт любой, кто скачал код с GitHub.
"""
import json, os, re, shutil, subprocess, sys, tempfile, datetime

HERE = os.path.dirname(os.path.abspath(__file__))
FQBN = 'esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom'
TMP = tempfile.gettempdir()
B_FREE, B_PRO = os.path.join(TMP, 'air_build_free'), os.path.join(TMP, 'air_build_pro')
NAME = 'esp32_TFT_air'


def version():
    s = open(os.path.join(HERE, 'Config.h'), encoding='utf-8').read()
    return re.search(r'FW_VERSION\s*=\s*"([^"]+)"', s).group(1)


def build():
    sub = os.path.join(TMP, 'air_free', NAME)
    shutil.rmtree(os.path.dirname(sub), ignore_errors=True); os.makedirs(sub)
    for f in os.listdir(HERE):
        if f.endswith(('.ino', '.h')) or f == 'partitions.csv':
            shutil.copy2(os.path.join(HERE, f), sub)
    # пути с именем пользователя Windows не должны попадать в публичный файл прошивки (они там из assert'ов библиотек)
    home = os.path.expanduser('~')
    pm = f'-ffile-prefix-map={home}=~'   # путь проекта с пробелами в флаг не ставим: его в прошивке и так нет
    props = ['--build-property', f'compiler.c.extra_flags={pm}', '--build-property', f'compiler.cpp.extra_flags={pm}']
    jobs = [subprocess.Popen(['arduino-cli', 'compile', '-b', FQBN, '--build-path', B_FREE, *props, sub]),
            subprocess.Popen(['arduino-cli', 'compile', '-b', FQBN, '--build-path', B_PRO, *props, HERE])]
    if any(j.wait() for j in jobs):
        sys.exit('сборка не прошла')


def place():
    v = version()
    fw = os.path.join(HERE, 'docs', 'firmware'); os.makedirs(fw, exist_ok=True)
    cp = lambda a, b: shutil.copy2(os.path.join(B_FREE, a), os.path.join(fw, b))
    cp(NAME + '.ino.bootloader.bin', 'bootloader.bin')
    cp(NAME + '.ino.partitions.bin', 'partitions.bin')
    cp(NAME + '.ino.bin', 'air-free.bin')
    boot = os.path.join(B_FREE, 'boot_app0.bin')
    if not os.path.exists(boot):   # в папке сборки его может не быть — берём из ядра esp32
        import glob
        boot = sorted(glob.glob(os.path.expandvars(r'%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\*\tools\partitions\boot_app0.bin')))[-1]
    shutil.copy2(boot, os.path.join(fw, 'boot_app0.bin'))
    man = {
        'name': 'Монитор воздуха (бесплатная)', 'version': v,
        'date': datetime.date.today().strftime('%d.%m.%Y'),
        'new_install_prompt_erase': True,
        'builds': [{'chipFamily': 'ESP32-S3', 'parts': [
            {'path': 'firmware/bootloader.bin', 'offset': 0x0},
            {'path': 'firmware/partitions.bin', 'offset': 0x8000},
            {'path': 'firmware/boot_app0.bin', 'offset': 0xE000},
            {'path': 'firmware/air-free.bin', 'offset': 0x10000}]}]}
    json.dump(man, open(os.path.join(HERE, 'docs', 'manifest.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    rel = os.path.join(HERE, 'release'); os.makedirs(rel, exist_ok=True)
    pro = os.path.join(rel, f'air-pro-{v}.bin')
    shutil.copy2(os.path.join(B_PRO, NAME + '.ino.bin'), pro)
    print('бесплатная ->', fw, f'({os.path.getsize(os.path.join(fw, "air-free.bin")) // 1024} КБ)')
    print('PRO        ->', pro, f'({os.path.getsize(pro) // 1024} КБ)')


if __name__ == '__main__':
    if '--no-build' not in sys.argv:
        build()
    place()
