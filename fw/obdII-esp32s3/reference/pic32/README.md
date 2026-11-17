# Referenční sestavení PIC32

`Makefile` je přesná kopie aktuální pracovní verze `fw/obdII/Makefile`.
V okamžiku importu se tento soubor lišil od revize Mercurialu uvedené
v manifestu; manifest proto obsahuje také hash a stav každého zdroje.
Makefile zde slouží k dohledání aktivních modulů a původního sestavení,
nikoliv k překladu přeuspořádaných kopií.

Byly převzaty vlastní moduly uvedené v jeho seznamu objektů. Nebyly převzaty
Microchip USB stack, PLIB, SD-SPI/MDDFS, MIPS startup, linker script ani
původní standardní hlavičky ani binární knihovny libc. Tyto závislosti nahradí ESP-IDF.
Zdrojové soubory portu jsou zatím záměrně původní, a proto je stále odkazují.
Ani volitelné vypnuté moduly KWP2000, DAQ, RFID a MCP9801 nejsou součástí importu.
