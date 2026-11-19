# OBD-II ESP32-S3: Hello World

Jednoduchý test pro ESP32-S3-WROOM-1-N16R8 a ESP-IDF 5.5.1.
Konzole používá interní USB Serial/JTAG (GPIO19 = D−, GPIO20 = D+).
Není potřeba externí převodník USB/UART.

## Sestavení

```bash
source ~/.espressif/tools/activate_idf_v5.5.1.sh
cd ~/projects/CarMon/fw/obdII-esp32s3/examples/hardware_test
idf.py build
```

Cíl ESP32-S3, 16MB flash, 1500KiB aplikační oddíl a USB konzole jsou nastaveny v `sdkconfig.defaults`.
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
Po startu se provede test I²C, potom SD karty a pak začne blikat D1.

## Test I²C: EEPROM a RTC

Test používá SDA na GPIO2, SCL na GPIO1, externí pull-up rezistory a 100 kHz.
Spustí se jednou před SD a Wi-Fi; není potřeba zadávat heslo.
Nejprve vypíše odpovídající 7bitové adresy v rozsahu 0x08–0x77.

- **M24C02, 0x54** (podle původního ovladače): při prvním pokusu zapíše
  do každé adresy 0x00–0xFF právě jeden bajt s hodnotou této adresy.
  Potom přečte všech 256 bajtů dvakrát, porovná obě kopie i očekávaný vzor
  a vypíše obsah hexadecimálně, po 16 bajtech na řádek.
- **PCF8563, 0x51**: dvakrát přečte řídicí a kalendářní registry
  s odstupem 1,2 sekundy a zkontroluje postup sekund.

**Zápis přepíše celý původní obsah EEPROM.** Před prvním zápisem se do NVS
ve flash ESP32 uloží příznak `hw_test/eep_addr_once`. Při dalších startech
se již do EEPROM nezapisuje, pouze se čte a ověřuje adresní vzor.
Zápisy se neopakují ani po chybě nebo resetu během testu: příznak znamená
zahájený pokus, nikoli úspěšné dokončení. Test NVS při chybě nemaže.
Ochrana platí, dokud příznak zůstane zachovaný; smazání NVS nebo celé flash
umožní nový pokus. Po každém bajtovém zápisu se čeká na dokončení interního
cyklu (bez opakování datového zápisu).

`EEPROM PASS` a `RTC PASS` potvrzují komunikaci; `RTC TICK PASS` také
postup sekund. `VL=1` znamená nespolehlivý čas po nízkém napájení,
`STOP=1` zastavené hodiny. Při úvodním I²C testu se tyto stavy pouze vypíší.
Po připojení Wi-Fi a úspěšném NTP se RTC nastaví podle popisu níže. Rok je zobrazen dvouciferně a stoletní bit samostatně.
Test nepředpokládá, že dosud nenastavený kalendář obsahuje platný čas.

Tyto obvody neposkytují identifikátor typu: ACK a čtení registrů samy
neprokazují model čipu. Test s adresním vzorem ověřuje zápis a čtení 256 bajtů,
nikoli dlouhodobou retenci nebo životnost paměti. Při jiném zapojení adresních
vstupů EEPROM je třeba upravit `I2C_TEST_EEPROM_ADDR` v `main/i2c_test.c` (0x50–0x57);
0x51 je již obsazena RTC a zařízení nesmí sdílet stejnou adresu.
Pro zopakování čtení a ověření stačí reset; zápis se neopakuje.
Při chybě I²C ostatní testy pokračují.

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

1. Synchronizace systémových hodin přes `pool.ntp.org`, nejvýše 15 sekund.
2. Zápis systémového času v UTC do PCF8563 a ověření zpětným čtením.
3. DNS překlad `dl.espressif.com`.
4. HTTPS GET malého testovacího souboru používaného v příkladu Espressifu:
   `https://dl.espressif.com/dl/esp-idf/ci/esp_http_client_demo.txt`.

RTC ukládá **UTC**, bez posunu na český čas nebo letní čas. Po úspěšném
NTP se zapíše datum, čas a den týdne; podporovány jsou roky 2000–2099
se stoletním bitem C=0. Při zápisu se krátce zastaví čítače, ověří všech
sedm kalendářních registrů a hodiny se opět spustí v normálním režimu.
Následně se ověří VL=0 a postup sekund. Výsledek označuje `RTC SET PASS`
s nastavovaným časem v UTC, nebo `RTC SET FAIL` s důvodem chyby.
Alarm, časovač a nastavení CLKOUT se nemění. Při selhání zápisu se program
pokusí hodiny znovu spustit, ale platnost částečně zapsaného času nezaručuje.

Pokud NTP selže, do RTC se nezapisuje. Nastavení RTC probíhá před DNS/HTTPS,
aby nezáviselo na dostupnosti HTTPS serveru. Chyba RTC nezastaví test HTTPS;
jeho `INTERNET PASS` nepotvrzuje úspěšné nastavení RTC.
Synchronizace se provede jednou po každém připojení Wi-Fi, ne periodicky.

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
