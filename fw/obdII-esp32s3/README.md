# OBD-II: port PIC32 na ESP32-S3

Portace pro **ESP32-S3-WROOM-1-N16R8**, ESP-IDF 5.5.1,
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
Ve stejném rytmu svítí zelená LED na GPIO10; červená LED na GPIO11
zůstává zhasnutá. Obě externí LED jsou aktivní v logické jedničce,
zatímco D1 je aktivní v nule.
FreeRTOS zůstává systémovou součástí ESP-IDF.

**Celá OBD aplikace se zatím nespouští.** `carmon_platform_ready()` vrací false
odděleně od aplikace, takže její inicializace se kontroluje při linkování,
ale nespouští se nad nepřenesenými ovladači. Konzole tento stav výslovně vypíše.
Neprobíhá komunikace s ECU ani zápisy do SD nebo EEPROM. TCAN330 je držený
v silent/shutdown režimu. Samostatně se spouští Wi-Fi podle nastavení
v menuconfig a TCP přenos původního protokolu. Staré hardwarové testy se automaticky nespouštějí.

## Wi-Fi + TCP: protokol CarMon

Modul pracuje jako stanice (STA), připojuje se k routeru a získá IPv4 adresu
přes DHCP. Výchozí SSID je `UPC5254179`, TCP port `3333`. Vlastní přístupový
bod nevytváří. Před prvním nahráním nastav v adresáři projektu:

```bash
idf.py menuconfig
```

V nabídce **CarMon network** vyplň **Wi-Fi station SSID** a **Wi-Fi station
password**; případně změň **TCP server port**. Heslo musí mít 8 až 63 bajtů,
SSID 1 až 32 bajtů. Výchozí heslo je prázdné: bez platného nastavení zůstane síť
vypnutá a konzole vypíše pokyn k nastavení; RTL s heartbeat běží dál.

Potom sestav a nahraj firmware:

```bash
idf.py build
idf.py -p /dev/cu.usbmodem1301 flash monitor
```

Po každém zapnutí se modul automaticky připojuje; USB ani zadávání hesla
nepotřebuje. Monitor je jen pro sledování logu. Změna sítě vyžaduje nové nastavení,
sestavení a nahrání firmwaru.

Heslo se nevypisuje do provozního logu, ale je uložené v lokálním `sdkconfig`
a je součástí firmwaru. `sdkconfig` i `sdkconfig.old` jsou v `.gitignore`;
skutečné heslo nepřidávej do verzovaného `sdkconfig.defaults`. Wi-Fi ovladač
neukládá další kopii přihlašovacích údajů do NVS.

Po připojení očekávej například následující zprávy; adresa závisí na DHCP:

```text
carmon_wifi: Connected: IP=192.168.0.132, gateway=192.168.0.1
carmon_tcp: CarMon server listening on port 3333
```

Mac musí být ve stejné dostupné síti. V jiném terminálu spusť s aktuální IP:

```bash
nc 192.168.0.132 3333
```

Zadej `<PC00>` a Enter. Odpověď `<PA>` potvrzuje zpracování příkazu
původním protokolem (vypnutí CRC). Samotný text se již nevrací jako echo.
Paket `<X>` vrátí `<MN01>` (neznámý příkaz). Odpovědi nemají konec řádku.
Server přijímá jednoho klienta; další spojení uzavře.

TCP je proud bajtů: jeden paket může přijít po částech a jeden příjem může
obsahovat více paketů. Při odpojení (včetně uzavření vysílací poloviny socketu)
se relace ukončí; před odpojením tedy vyčkej na odpověď. Nové spojení začíná
s prázdnými FIFO, resetovaným parserem a vypnutým CRC.

`wifi.c` udržuje připojení, vypisuje přidělenou adresu a znovu se připojuje
po výpadku. Každý pokus včetně DHCP má limit 30 s, prodleva mezi pokusy je
nejméně 2 s. NVS se při chybě inicializace automaticky nemaže.

`tcp.c` přesouvá bajty mezi socketem a dvěma FIFO, každé o velikosti
1024 bajtů. `tcp_getchar()` a `tcp_putchar()` jsou neblokující: vracejí 0 při
úspěchu a `EAGAIN` při prázdném RX nebo plném TX. Odpojený TX znaky zahazuje
stejně jako původní `bt_putchar()`. Argument `block` se ignoruje.
Indexy FIFO používají lock-free atomické operace pro předávání mezi CPU.
Při plném RX se pozastaví čtení socketu; částečně odeslaná TX data se zachovají.

**`mcom_svc()` i `pcore_svc()` nadále volá RTL z přerušení každých 500 µs
na CPU1.** TCP ani Wi-Fi úloha je nevolá. TCP běží na CPU0 a kontroluje FIFO
s timeoutem `select()` 10 ms. Při nové relaci nejprve počká na reset stavů
potvrzený RTL, potom vyprázdní FIFO a zpřístupní spojení. Příjem v jednom
průchodu `mcom_svc()` je omezen na 64 bajtů, stejně jako původní vysílání.

Aktivní jsou konfigurace CRC (`PC`), softwarový výpočet CRC (`Pcs...`),
neznámé příkazy s NAK a požadavek restartu (`q`). Příkazy CAN, OBD a loggeru
zatím vrací NAK; neaktivují nepřenesené služby. Čtení/nastavení času, EEPROM,
I²C a teplota zatím vrací `<PN04>`: vedle chybějících ovladačů je třeba
vyřešit časové funkce libc, které nepatří do ISR. Délku ISR je nutné změřit
na desce, zejména u dlouhých CRC paketů. `rt_light.c` zůstává beze změny.

## Stav částí portu

| Část | Stav |
| --- | --- |
| Sestavení a vstup aplikace | ESP-IDF CMake, nový `app_main`, inicializace na CPU1 |
| RTL | Přeneseno na GPTimer/GPIO, zachováno plánování v ISR; na desce zatím neověřeno |
| Wi-Fi + TCP | STA, SSID a heslo z menuconfig, DHCP, reconnect a jedno klientské spojení s protokolem CarMon; nový firmware zatím bez ověření na desce |
| Master protocol, core | Běží pod RTL; přenos přes TCP FIFO, omezená sada příkazů |
| CarMon, OBD, CAN snooping | Překládají se; aplikační služby zatím nejsou spuštěné |
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
`communication/wifi.c` a `communication/tcp.c` poskytují síťové rozhraní;
jejich veřejné hlavičky jsou v `communication/include/`. Spuštění síťového transportu
s údaji z menuconfig je oddělené v `main/network.c`.
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

Síťové testy překládají produkční `wifi.c`, `tcp.c` a `main/network.c` proti
náhradám API ESP-IDF. Ověřují obnovu spojení, DHCP timeout, úklid při chybě
inicializace a generační čítač spojení. TCP test používá skutečné localhost
sockety a vynucuje částečné odeslání i `EAGAIN`; kontroluje skutečné rámce master protokolu, FIFO wrap, CRC, krátké chybné
pakety, odmítnutí druhého klienta a reset rozpracované RX/TX relace.
Stavové automaty v testu volá samostatné vlákno simulující periodické RTL;
neověřuje skutečnou latenci přerušení na ESP32-S3.
Test inicializace ověřuje předání údajů z konfigurace, start bez USB vstupu
a zpracování chyb. Nejde o simulaci rádia ani DHCP serveru; tyto části je
nutné ověřit na desce. Hostitelský TCP test potřebuje povolené lokální sockety.

## Původ

`IMPORT_MANIFEST.json` zůstává historickým záznamem prvního importu z Mercurialu:
zdrojové cesty, tehdejší pracovní stav a SHA-256. U nyní upravených souborů se
hash aktuálního obsahu přirozeně liší. Snímek hardwarového testu zůstává přesný.
Další vývoj pokračuje v [Git repozitáři CarMon](https://github.com/tio-checo/CarMon).
