# OBD-II ESP32-S3: Hello World

Jednoduchý test pro ESP32-S3-WROOM-1-N16R8 a ESP-IDF 5.5.1.
Konzole používá interní USB Serial/JTAG (GPIO19 = D−, GPIO20 = D+).
Není potřeba externí převodník USB/UART.

## Sestavení

```bash
source ~/.espressif/tools/activate_idf_v5.5.1.sh
cd ~/projects/CarMon/fw/obdII-esp32s3
idf.py build
```

Cíl ESP32-S3, 16MB flash a USB konzole jsou nastaveny v `sdkconfig.defaults`.
Pokud změníš tyto výchozí hodnoty poté, co již vznikl `sdkconfig`, uprav také
konfiguraci přes `idf.py menuconfig`; defaults existující nastavení nepřepisují.

## Nahrání a sledování výstupu

Tento příkaz nahraje testovací firmware do flash:

```bash
idf.py -p /dev/cu.usbmodem1301 flash monitor
```

Aktuální port lze zjistit pomocí `ls /dev/cu.usbmodem*`.
Monitor ukončíš klávesami **Ctrl+]**. Již nahraný program lze sledovat samostatně:

```bash
idf.py -p /dev/cu.usbmodem1301 monitor
```

Pokud se nedaří připojit k bootloaderu, podrž FW/BOOT (GPIO0 na GND), krátce
stiskni a uvolni RESET/EN a pak uvolni FW/BOOT. Zkontroluj znovu název portu.

## Očekávaný výsledek

Při startu se vypíše revize procesoru a detekovaná velikost flash.
Dioda **D1** bliká: 500 ms svítí, 500 ms je zhasnutá (perioda 1 sekunda).
Její katoda je podle schématu připojena přes síť `LED_HB` na **GPIO9**
(vývod 17 modulu U2); anoda je přes R11 (1 kΩ) připojena k 3,3 V.
Dioda proto svítí při logické nule na GPIO9.
Po připojení Wi-Fi konzole každou sekundu vypíše RSSI v dBm, kanál a dobu
od startu. Výpisy Hello World každou sekundu zůstávají vypnuté; D1 dále bliká.

Program se sám nerestartuje. Test ověřuje spuštění aplikace z flash,
základní běh FreeRTOS a výstup přes interní USB.

## PSRAM

8MB Octal PSRAM na 40 MHz zůstává zapnutá pro běžné použití aplikací.
Vlastní test se šesti paměťovými vzory i bootovací test ESP-IDF jsou vypnuté.
Po startu se rovnou provede test SD karty a pak začne blikat D1.

## Test microSD

Po startu se jednou spustí nedestruktivní test **4bitového SDMMC na 20 MHz**.
Zapojení podle schématu a netlistu PCB:

| Signál | GPIO | Vývod U2 | Kontakt J1 |
| --- | --- | --- | --- |
| CLK | 14 | 22 | 5 |
| CMD | 21 | 23 | 3 |
| DAT0 | 13 | 21 | 7 |
| DAT1 | 12 | 20 | 8 |
| DAT2 | 48 | 25 | 1 |
| DAT3 | 47 | 24 | 2 |

Používají se externí 10k pull-up rezistory R4–R8; interní pull-up jsou vypnuté.
Test inicializuje kartu, vypíše její identifikaci a kapacitu a ověří 4bitový režim.
Na 32 místech rozložených od začátku do konce karty dvakrát přečte blok
8 sektorů a porovná obě kopie. Celkem přečte 256 KiB a na závěr ověří stav karty.
DMA buffery jsou v interní RAM. Zásobník hlavní úlohy má pro testy 8192 bajtů.

Úspěch označuje `SD PASS: ... bus=4-bit, clock=20000 kHz ...`.
Při chybě se objeví `SD FAIL` s fází a kódem chyby; ostatní testy pokračují.
Karta se neformátuje, nic se na ni nezapisuje a souborový systém se nepřipojuje.
Test proto nevyžaduje FAT ani jiný konkrétní souborový systém. Zápis souborů
ani dlouhodobou spolehlivost tímto testem neověřujeme.
Pro opakování stačí reset; SD test nevyžaduje připojení Wi-Fi ani heslo.

## Test Wi-Fi

Po testu SD karty se spustí Wi-Fi stanice. Vypíše počet nalezených 2,4GHz sítí
a až deset nejsilnějších sítí s RSSI a kanálem. Poté čeká na heslo k síti
**UPC5254179**. D1 během čekání pokračuje v blikání.

Otevři `idf.py -p /dev/cu.usbmodem1301 monitor`, napiš heslo a stiskni Enter.
Program za každý přijatý bajt hesla zobrazí `*`, skutečné heslo nevypisuje.
Backspace/Delete smaže poslední znak i jeho hvězdičku. Enter přijímá ve variantách
CR, LF i CRLF. Heslo není ve zdrojovém kódu a Wi-Fi konfigurace zůstává pouze
v RAM. Heslo se zadává po každém resetu znovu.
Příjem řádku potvrdí hláškou `WiFi: password line received (content hidden).`.
USB konzole používá ovladač s RX bufferem, aby fungovalo neblokující čtení
z `stdin` i v ESP-IDF 5.5.1.
Podporované jsou osobní sítě WPA2/WPA3 s heslem 8–63 bajtů; prázdné heslo
zvolí otevřenou síť. Podnikové přihlášení 802.1X tento test neřeší.

Po připojení a získání DHCP adresy se objeví:

```text
WiFi PASS: SSID=UPC5254179, IP=..., gateway=...
```

Po připojení a získání IP adresy hlavní úloha přibližně každou sekundu vypíše:

```text
WiFi RSSI: uptime=25 s, RSSI=-69 dBm, channel=11.
```

RSSI se čte z připojeného AP přes `esp_wifi_sta_get_ap_info()`; nejde o opakovaný
sken okolních sítí. Výpis běží i během testu internetu. Při odpojení se zastaví
a po novém připojení znovu pokračuje. Napájecí napětí firmware neměří.

Při odpojení program vypíše důvod a znovu vyzve k heslu.
Samotný sken (`WiFi SCAN PASS`) nepotvrzuje připojení. Wi-Fi test ověřuje
asociaci, přihlášení a získání IPv4 adresy. Při potížích po
nejvýše 30 sekundách pokus ukončí, aby bylo možné zadat heslo znovu.

Obsluha Wi-Fi/IP událostí pouze předává stav samostatné úloze `wifi_test`;
Test HTTPS a hlášky připojení provádí tato úloha se zásobníkem 16384 bajtů.
Pravidelný výpis RSSI provádí hlavní úloha společně s blikáním D1.
Systémová událostní úloha `sys_evt` má nastaveno 4096 bajtů zásobníku.

## Test internetu

Po každém úspěšném Wi-Fi připojení se jednou provede:

1. DNS překlad `dl.espressif.com`.
2. Synchronizace hodin přes `pool.ntp.org`, s čekáním nejvýše 15 sekund.
3. HTTPS GET malého testovacího souboru používaného v příkladu Espressifu:
   `https://dl.espressif.com/dl/esp-idf/ci/esp_http_client_demo.txt`.

HTTPS používá certifikační balík ESP-IDF a ověřuje řetězec, jméno serveru
i platnost certifikátu v čase. Časové kontroly jsou výslovně zapnuté pomocí
`CONFIG_MBEDTLS_HAVE_TIME_DATE`. HTTP přesměrování jsou vypnutá. Úspěch vyžaduje
dokončený přenos s HTTP 200 a neprázdné tělo odpovědi:

```text
INTERNET PASS: DNS + verified HTTPS, HTTP=200, received=... bytes.
```

Chyba DNS nebo HTTPS vypíše `INTERNET FAIL` s podrobnostmi.
Pokud neprojde synchronizace času, výsledek je `INTERNET TEST INCOMPLETE`;
blokovaný NTP provoz (UDP 123) sám o sobě neprokazuje nedostupný internet.
Timeout HTTPS je 10 sekund na síťovou operaci. Po testu pokračují pravidelné
výpisy RSSI; pro opakování testu internetu lze restartovat modul a zadat heslo znovu.

USB konzole: https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/usb-serial-jtag-console.html
