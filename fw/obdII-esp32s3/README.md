# OBD-II: port PIC32 na ESP32-S3

Příprava portace CarMon na ESP32-S3-WROOM-1-N16R8 s ESP-IDF 5.5.1.
Tento výchozí stav pouze organizuje a kopíruje soubory; portace ještě nezačala.
Převzatý aplikační kód ani RTL nebyly upraveny.

## Struktura

| Adresář | Obsah |
| --- | --- |
| `main/` | Dosavadní funkční test ESP32-S3 a nepřekládaná kopie `pic32_main.c` |
| `components/rtl/` | Původní RTL, softwarové časovače a plánování služeb |
| `components/carmon/` | CarMon, OBD a CAN snooping |
| `components/communication/` | Protokol s počítačem, Bluetooth a monitor |
| `components/storage/` | Vlastní souborový systém DLFS |
| `components/devices/` | EEPROM, RTC, LCD terminál a GPS |
| `components/platform/` | Hardwarové a systémové části k přizpůsobení ESP32-S3 |
| `examples/hardware_test/` | Samostatný snímek ověřeného testovacího projektu |
| `reference/pic32/` | Původní Makefile a vymezení importu |

## Co se nyní překládá

Kořenový `CMakeLists.txt` a `main/CMakeLists.txt` zůstaly beze změn.
`idf.py build` proto stále sestavuje původní test ESP32-S3: LED D1,
nedestruktivní čtení SD přes 4bitové SDMMC, Wi-Fi, HTTPS a výpis RSSI.
`main/pic32_main.c` není uveden v seznamu zdrojů. Importované adresáře
`components/*` nemají `CMakeLists.txt` a do sestavení zatím nevstupují.
Nejde zatím o funkční port původní OBD aplikace.

## Zachovaný hardwarový test

Přesný snímek testovacího projektu před importem je v `examples/hardware_test/`.
Jeho README obsahuje zapojení, popis testů a práci s USB konzolí. Kopie
`sdkconfig.reference` uchovává skutečnou ověřenou konfiguraci, nikoliv jen defaults.
Pro samostatné sestavení použij:

```bash
source ~/.espressif/tools/activate_idf_v5.5.1.sh
cd ~/projects/CarMon/fw/obdII-esp32s3/examples/hardware_test
# Pouze při prvním sestavení, pokud zde ještě není sdkconfig:
cp -n sdkconfig.reference sdkconfig
idf.py build
```

## Výchozí stav a původ souborů

`IMPORT_MANIFEST.json` uvádí zdrojovou cestu v CarMon, cílovou cestu,
SHA-256 každé kopie a stav zdrojového souboru vůči Mercurialu.
Import vychází z aktuálního pracovního stromu, nikoliv automaticky z čisté
revize; odlišnosti jsou v manifestu výslovně zaznamenány.
Import vznikl v původním Mercurial repozitáři CarMon; další vývoj pokračuje
v Git repozitáři [tio-checo/CarMon](https://github.com/tio-checo/CarMon).

Při prvním portu zachováme původní RTL a periodu 500 µs. Následovat bude
náhrada hardwarového časovače a periferních backendů. Zachováme protokol
s počítačem, obsah EEPROM a formát DLFS. Převod aplikačních služeb na
samostatné FreeRTOS úlohy je oddělený případný další krok.
