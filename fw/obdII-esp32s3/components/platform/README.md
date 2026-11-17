# platform

esp32s3/ obsahuje inicializaci desky a aplikace a výslovně nepřenesené backendy. utils.c používá softwarové CRC; helpers.c používá čas a libc ESP-IDF. Původní can_hal.c, i2c_hal.c a i2c_sw.c se zatím nepřekládají.

Podrobný stav a postup sestavení je v `../../README.md`.
Historický původ kopií zachycuje `../../IMPORT_MANIFEST.json`.
