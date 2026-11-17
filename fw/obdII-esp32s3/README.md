# OBD-II: port PIC32 na ESP32-S3

První sestavitelná etapa portace pro **ESP32-S3-WROOM-1-N16R8**, ESP-IDF 5.5.1,
16MB flash, 8MB Octal PSRAM a interní USB Serial/JTAG konzoli.

## Sestavení

```bash
source ~/.espressif/tools/activate_idf_v5.5.1.sh
cd ~/projects/CarMon/fw/obdII-esp32s3
idf.py build
```

Pro nový pracovní adresář se použije `sdkconfig.defaults`. Existující `sdkconfig`
se automaticky nepřepisuje; zkontroluj jej pomocí `idf.py menuconfig`.
Firmware a ELF vzniknou v `build/obdII_esp32s3.bin` a `build/obdII_esp32s3.elf`.
Nahrání do desky je samostatný krok: `idf.py -p /dev/cu.usbmodem1301 flash monitor`.

## Co tato etapa dělá

Překládá původní aplikační logiku CarMon, OBD, CAN snoopingu,
protokolu s počítačem, DLFS, EEPROM, RTC, LCD a GPS. Používá přenesené RTL:
pooly služeb a softwarových časovačů, jejich původní pořadí a heartbeat.

Při startu se vytvoří úloha připnutá na CPU1. Ta inicializuje GPTimer s krokem
1 µs a periodickým přerušením po 500 µs. Obsluha RTL běží přímo v přerušení,
nikoliv jako FreeRTOS úloha. D1 na GPIO9 používá původní heartbeat RTL.
FreeRTOS zůstává systémovou součástí ESP-IDF.

**Celá OBD aplikace se zatím nespouští.** `carmon_platform_ready()` vrací false
odděleně od aplikace, takže její inicializace se kontroluje při linkování,
ale nespouští se nad nepřenesenými ovladači. Konzole tento stav výslovně vypíše.
Neprobíhá komunikace s ECU ani zápisy do SD nebo EEPROM. TCAN330 je držený
v silent/shutdown režimu. Wi-Fi ani staré hardwarové testy se automaticky nespouštějí.

## Stav částí portu

| Část | Stav |
| --- | --- |
| Sestavení a vstup aplikace | ESP-IDF CMake, nový `app_main`, inicializace na CPU1 |
| RTL | Přeneseno na GPTimer/GPIO, zachováno plánování v ISR; na desce zatím neověřeno |
| CarMon, OBD, CAN snooping, master protocol | Překládají se; aplikační služby zatím nejsou spuštěné |
| RTC, EEPROM, LCD, GPS parser | Překládají se nad původním neblokujícím API; ovladače sběrnic chybějí |
| CAN/TWAI včetně transportu OBD | Původní `can_hal.c` zatím mimo sestavení; API vrací `ENOTSUP` |
| I²C, GPS UART, externí BTM-112 | Zatím `ENOTSUP`; původní PIC32 ovladače zůstávají uložené |
| USB protokol a diagnostický monitor | Zatím `ENOTSUP`; systémová textová USB konzole funguje prostřednictvím ESP-IDF |
| DLFS | Překládá se; médium se hlásí jako nepřítomné, čtení/zápis vrací neúspěch |
| CRC | Původní tabulkový algoritmus, softwarová náhrada pevného PIC32 DMA CRC |
| Čas a libc | Používá se ESP-IDF; vlastní `time`, `settimeofday`, `write`, `memcpy` a `memset` se nepřenášejí |

Neimplementované backendy jsou soustředěné v `components/platform/esp32s3/unported.c`.
Žádný z nich nevrací falešný úspěch I/O. Ochranu spuštění aplikace odstranit až
po jejich skutečném přenesení a ověření celého volacího řetězce z ISR.
Původní CAN HAL obsahuje i skládání vícerámcových OBD zpráv; samotný TWAI driver
jej nenahradí. DMA diagnostický příkaz PIC32 je zatím odmítnut, softwarový CRC příkaz zůstává.

## Omezení RTL v této etapě

- Inicializace a API RTL patří CPU1. Služby musí být neblokující a vhodné pro ISR;
  registraci z úlohy provádět se zastaveným časovačem.
- `CONFIG_GPTIMER_ISR_CACHE_SAFE` musí zůstat vypnuté: celý aplikační řetězec
  zatím není v IRAM. Při nedostupné flash cache se přerušení může opozdit.
- Obsluha obsahuje jeden průchod službami bez neomezené doháněcí smyčky.
  Statistiky zachovávají původní význam: časy (tick = 1 µs), překročení
  periody a maximální počet průchodů obsluhou (`nloops`, nyní nejvýše 1).
  Nejde o přesný čítač ztracených period. Softwarové časovače zatím odpočítávají
  callbacky; při opoždění se mohou prodloužit. Reálné latence je nutné změřit.
- Zápis médií a blokující operace bude potřeba obsluhovat mimo ISR.
- Před zapnutím zápisu DLFS je nutné ověřit původní binární layout, zarovnání
  zejména 64bitového času inode a CRC nad existujícími obrazy karet. Tato etapa
  nemění struktury uložených dat a neprohlašuje jejich kompatibilitu za ověřenou.

## Struktura

`components/carmon/CMakeLists.txt` nyní sdružuje přenášené zdroje do jedné
komponenty kvůli jejich vzájemným závislostem. Zdrojové adresáře `rtl`,
`communication`, `storage`, `devices` a `platform` zůstávají oddělené.
Původní `main/pic32_main.c`, PIC32 ovladače a staré testovací soubory v `main/`
nejsou v seznamu překladu. Aktivní vstup je `main/app_main.c`.

## Zachovaný hardwarový test

`examples/hardware_test/` je nezměněný samostatný projekt se všemi ověřenými
zkouškami LED, SDMMC, Wi-Fi a HTTPS. Jeho README popisuje zapojení a obsluhu:

```bash
cd ~/projects/CarMon/fw/obdII-esp32s3/examples/hardware_test
# Pouze při prvním sestavení, pokud zde ještě není sdkconfig:
cp -n sdkconfig.reference sdkconfig
idf.py build
```

## Kontroly

Překlad přes ESP-IDF ověřuje cílové typy, API a linkování. Hostitelské testy:

```bash
python3 tests/run_host_tests.py
```

Test RTL ověřuje původní implementaci s testovacími náhradami IDF API:
vyčerpání poolu, expiraci časovačů, pořadí a filtrování služeb, heartbeat,
výběr CPU a konfiguraci alarmu. Neověřuje skutečnou latenci přerušení.
Test CRC obsahuje standardní vektory a nezávislou bitovou referenci pro délky
používané EEPROM a sektory DLFS. Shodu s původním PIC32 DMA nad skutečným
obsahem EEPROM je ještě potřeba ověřit na uložených datech.

## Původ

`IMPORT_MANIFEST.json` zůstává historickým záznamem prvního importu z Mercurialu:
zdrojové cesty, tehdejší pracovní stav a SHA-256. U nyní upravených souborů se
hash aktuálního obsahu přirozeně liší. Snímek hardwarového testu zůstává přesný.
Další vývoj pokračuje v [Git repozitáři CarMon](https://github.com/tio-checo/CarMon).
