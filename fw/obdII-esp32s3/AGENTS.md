# Pravidla portace CarMon na ESP32-S3

Tento dokument určuje pravidla pro práci agenta v adresáři `fw/obdII-esp32s3`
a jeho podadresářích. Pravidla budeme průběžně doplňovat.

## 1. Dosavadní dohody

### Rozsah změn

- Prováděj co nejmenší změny potřebné pro aktuální krok portace.
- Zachovej původní funkční kód a komentáře, které není nutné měnit.
- Nepřidávej nesouvisející refaktoring ani plošné změny formátování.
- Původní PIC32 projekt slouží jako reference. Úpravy portu prováděj
  v projektu ESP32-S3, pokud zadání výslovně neurčuje jinak.

### Styl kódu

- Při psaní a úpravách C kódu dodržuj Solaris C style (`cstyle`).
- Styl původních okolních souborů zachovávej tam, kde není v rozporu
  se Solaris C style. Kvůli sjednocení stylu nepřeformátovávej nesouvisející kód.
- Používej prefix podle modulu: například `wifi_`, `tcp_`, `mcom_`, `pcore_`.
- Nový kód doplň přiměřeným množstvím komentářů vysvětlujících účel,
  souvislosti a méně zřejmé chování. Jazyk přizpůsob okolnímu kódu.

### Architektura a kontext vykonávání

- V současné etapě zachovej původní RTL a jeho periodu 500 µs.
- `mcom_svc()` a `pcore_svc()` nyní běží pod RTL v periodickém přerušení
  na CPU1. TCP úloha na CPU0 předává bajty přes RX/TX FIFO.
- Čtení a zápis znaků mezi RTL a FIFO musí být neblokující.
- Přesun komunikace do aplikační úlohy je odložené téma, nikoli schválená
  součást nynější implementace. Proveď jej až podle příslušného zadání.
- Změnu kontextu vykonávání služby nepovažuj za pouhou výměnu transportu.

### Ověřování a předávání práce

- Ověř překlad pro ESP32-S3 a proveď kontroly odpovídající změně.
- Rozlišuj ověření překladem, hostitelským testem a během na skutečné desce.
- Při předání uveď podstatná omezení a dočasně nepodporované příkazy.
- Zachovej rozpracované změny uživatele mimo rozsah úkolu.
- Lokální Wi-Fi heslo neukládej do verzovaných zdrojů ani do výpisů.

## 2. Témata k doplnění

Následující položky jsou otevřené otázky, nikoli závazná pravidla.

### Rozdělení práce mezi RTL a FreeRTOS

- [DOPLNIT] Které služby zůstanou v přerušení a které poběží v úlohách?
- [DOPLNIT] CPU affinity, priority úloh, jejich periody a velikosti zásobníků.
- [DOPLNIT] Povolená doba vykonávání ISR a způsob jejího měření.

### Sdílená data a synchronizace

- [DOPLNIT] Vlastnictví dat a pravidla předávání mezi CPU, úlohami a ISR.
- [DOPLNIT] Volba FIFO, front, notifikací, mutexů a kritických sekcí.
- [DOPLNIT] Povolené atomické operace, typy a umístění dat v RAM/PSRAM.
- [DOPLNIT] Chování při zaplnění bufferu, odpojení a resetu relace.

### Čas a periferie

- [DOPLNIT] Zdroj reálného času: ESP-IDF, vlastní hodiny, PCF8563, NTP.
- [DOPLNIT] Rozhraní ovladačů I²C, CAN/TWAI a SD; obsluha chyb a timeoutů.
- [DOPLNIT] Podmínky aktivace dosud nepřenesených služeb.

### Kompatibilita a paměť

- [DOPLNIT] Požadavky na kompatibilitu protokolu s původní aplikací.
- [DOPLNIT] Kompatibilita dat na SD a v EEPROM, velikosti typů a zarovnání.
- [DOPLNIT] Pravidla dynamické alokace, interní RAM, PSRAM a DMA bufferů.

### Testy, dokumentace a Git

- [DOPLNIT] Povinné testy pro jednotlivé části a kritéria dokončení portace.
- [DOPLNIT] Postup ověřování na desce a zaznamenávání výsledků.
- [DOPLNIT] Které dokumenty aktualizovat při změně chování nebo architektury.
- [DOPLNIT] Členění commitů, názvy větví a postup publikování změn.

## 3. Šablona nového pravidla

- **Název / identifikátor:** [DOPLNIT]
- **Stav:** návrh / schváleno
- **Pravidlo:** [Jednoznačně popsat požadované chování.]
- **Důvod:** [Proč pravidlo existuje.]
- **Rozsah a výjimky:** [Kde platí a kdy se uplatní jiný postup.]
- **Ověření:** [Jak poznáme, že bylo dodrženo.]

Závazné jsou dosavadní dohody a pravidla označená jako schválená.
Návrhy a položky `[DOPLNIT]` samy o sobě nové povinnosti nezavádějí.
