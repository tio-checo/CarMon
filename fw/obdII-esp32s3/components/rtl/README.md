# rtl

Původní RTL: registrace služeb, softwarové časovače, heartbeat a statistiky. Hardwarový časovač PIC32 bude později nahrazen GPTimer na ESP32-S3 při zachování periody 500 µs.

Zdrojové a hlavičkové soubory byly převzaty beze změny bajtů. Původ souborů a SHA-256 jsou v `../../IMPORT_MANIFEST.json`.

Tento adresář zatím nemá `CMakeLists.txt`, takže se nepřekládá jako komponenta ESP-IDF. Sestavení bude doplněno až při vlastní portaci.
