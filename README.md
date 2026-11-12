# CarMon

CarMon is a custom vehicle monitoring and data logging project combining PIC32 firmware, OBD-II interface hardware, desktop utilities, and an Android application. It collects engine and vehicle measurements, records them for later analysis, and provides tools for viewing live data and investigating vehicle diagnostics.

The source includes standard OBD measurements and Toyota diesel-specific definitions for the **1AD-FTV** and **2AD-FTV** engines, including injector feedback, exhaust gas temperatures, and diesel particulate filter (DPF) measurements. Vehicle-specific PID definitions and CAN mappings need to be checked before using the project with another engine or ECU.

## What it does

- Acquires OBD parameters in configurable fast, medium, and slow polling groups.
- Supports autonomous operation, configuration, and operation assisted by a remote terminal.
- Includes CAN communication and monitoring, plus KWP2000 code for K-line communication. The enabled protocols depend on the firmware build configuration.
- Logs vehicle measurements to an SD card using the project's DLFS storage format.
- Integrates GPS, a real-time clock, EEPROM configuration, battery voltage measurement, and an LCD terminal in the embedded code.
- Provides desktop tools to extract recorded sessions, convert raw ECU values into physical quantities, and plot measurements.
- Includes an Android Bluetooth application with a dashboard, logging settings, supported PID inspection, freeze-frame data, and diagnostic trouble code screens.

This repository contains several generations and related parts of the project. The main OBD firmware Makefile targets the **Microchip PIC32MX795F512H**. The desktop and Android applications include legacy dependencies; the repository does not provide a single installer or an automated end-to-end setup.

## System overview

```text
Vehicle ECU / CAN bus
        |
Custom OBD interface board
        |
PIC32 firmware ---- GPS / RTC / EEPROM / LCD
        |
        +---- SD card (DLFS) ---- extraction ---- CSV ---- conversion ---- plots
        |
        +---- Bluetooth / USB serial ---- host tools
        |
        +---- Bluetooth ---- Android ObdLogger
```

Host communication uses the project's custom converter protocol. The included applications should not be assumed to work unchanged with a generic ELM327 adapter.

## Repository layout

| Path | Contents |
| --- | --- |
| [`fw/obdII/`](fw/obdII/) | Main PIC32 OBD interface and CarMon firmware, hardware profile, and Makefile. |
| [`fw/common/`](fw/common/) | Shared services and drivers: OBD, CAN, KWP2000, logging, DLFS, GPS, Bluetooth, USB, LCD, EEPROM, and RTC. Includes startup code, linker scripts, and vendor support code. |
| [`fw/bootloader/`](fw/bootloader/) | PIC32 bootloader with separate Bluetooth UART and KWP UART build configurations. |
| [`fw/mb/`](fw/mb/) | Assembly firmware for the motherboard controller. |
| [`fw/miniterm/`](fw/miniterm/) | Assembly firmware for the display/keyboard terminal. |
| [`fw/rfid/`](fw/rfid/) | Related RFID firmware. |
| [`hw/elektro/`](hw/elektro/) | OBD interface, motherboard, GPS, display, and RFID schematics and PCB layouts. |
| [`sw/app/`](sw/app/) | Desktop logging, protocol, conversion, plotting, and Bluetooth helper tools. |
| [`sw/app/dlfs/`](sw/app/dlfs/) | C utilities for extracting and formatting DLFS storage. |
| [`sw/app/android/ObdLogger/`](sw/app/android/ObdLogger/) | Java Android application and XML measurement definitions. |
| [`sw/app/android/Rfid/`](sw/app/android/Rfid/) | Related Android RFID application. |

## Getting started

```sh
git clone https://github.com/tio-checo/CarMon.git
cd CarMon
```

For offline analysis, start with the desktop tools below. To build a complete device, first review the hardware revision, its firmware hardware profile, the enabled modules in the Makefile, and the vehicle-specific measurement definitions.

### Convert and plot recorded data

The conversion and plotting utilities use Python 3. Install their dependencies in a virtual environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install lxml numpy matplotlib
cd sw/app
```

Run conversion from **`sw/app`**: `probe.py` loads measurement definitions through a path relative to the current working directory.

```sh
# Convert an extracted raw CSV session to a text table.
# Replace the example path with your own recording.
python cmconv.py -f /path/to/obd-session.csv -e 2adftv

# Inspect the converted file's available measurements.
python data_viewer.py -f /path/to/obd-session.txt -q

# Plot the converted measurements.
python data_viewer.py -f /path/to/obd-session.txt
```

`cmconv.py` defaults to `2adftv`; `1adftv` definitions are also included. With `-f`, the converter writes a file with the input filename's extension replaced by `.txt`. Without `-f`, it reads standard input and writes standard output.

The viewer supports selecting measurements with `-i`, histograms with `-t hist`, and starting battery voltage with `-s` when the recording contains the corresponding data. Run either tool with `--help` for its options. Recordings are not included in the repository.

### Extract SD card recordings

The device's DLFS storage has its own extraction utility; it is not simply a directory of ordinary files on a mounted card.

```sh
make -C sw/app/dlfs SHELL=/bin/bash
```

This builds `dlfsdump` and `dlfsformat` under `sw/app/dlfs/build/`, using a host C compiler and the Makefile's 64-bit flags. To extract a previously captured card image into a separate directory, run the following from the repository root:

```sh
mkdir -p extracted
cd extracted
../sw/app/dlfs/build/dlfsdump -f /path/to/card.img
```

The dumper creates timestamped `obd-NNN-YYYYMMDD-HHMMSS.csv` files in the current directory. Existing output files are skipped unless `-F` is supplied. Convert the extracted CSV files using `cmconv.py` as described above.

`dlfsformat` initializes DLFS storage and **overwrites storage metadata**. Review its source and confirm the target before using it on a card or device.

### Build the main firmware

[`fw/obdII/Makefile`](fw/obdII/Makefile) expects a PIC32-compatible MIPS little-endian toolchain. Its default tool directory is `/opt/mipsel-10.3.0`; it also expects the libraries `dsp`, `c_s_n`, and `gcc_s_n` under the toolchain's `lib/microchip` directory. A generic host compiler is insufficient.

Once that toolchain and its libraries are available:

```sh
make -C fw/obdII SHELL=/bin/bash TOOLS_DIR=/path/to/pic32-toolchain
```

The outputs are written under `fw/obdII/build/`, including `main.elf`, `main.hex`, disassembly, section information, and symbols. The explicit Bash setting accommodates the Makefile's `[[ ... ]]` directory creation command.

Review the `CFLAGS` module definitions and [`HardwareProfile_PIC32MX_OBDCONV.h`](fw/obdII/Include/HardwareProfile/HardwareProfile_PIC32MX_OBDCONV.h) when changing board or protocol configuration. The application uses a dedicated startup file and linker script; programming it requires a compatible bootloader and memory layout. Bootloader variants have their own `Makefile.bt` and `Makefile.kwp` files. A complete programming procedure and a toolchain installation recipe are not supplied in the repository.

### Legacy desktop and Android applications

- **Desktop logger:** `sw/app/obd_logger.py` uses Python 2, PyGTK 2, and pySerial. Its active serial setting is `/dev/rfcomm0` at 115200 baud. Review the device settings before running it.
- **Protocol utilities:** `obdII.py` and `ecu_sim.py` contain older Python 2 serial communication and simulation code with hard-coded device paths.
- **Bluetooth helpers:** `bt_manage.sh` and `app_wrapper.sh` contain machine-specific paths and Linux Bluetooth setup commands. Adapt them to your environment before use.
- **Android ObdLogger:** the checked-in project uses the Ant-era Android build system and targets `android-17`. It requires a compatible legacy Android SDK/Ant environment or migration to a current build system. Review `local.properties` and device settings rather than reusing checked-in machine paths. Compatibility with current Android versions has not been established here.

The Python 3 analysis tools can be used independently of the legacy live logger and Android application.

## Hardware and measurement definitions

The hardware files use **gEDA schematic** (`.sch`) and **PCB** (`.pcb`) formats. Start with [`hw/elektro/obdII_conv/`](hw/elektro/obdII_conv/) for the PIC32 converter and motherboard revisions. Custom symbol and footprint references may require libraries beyond this repository.

Measurement definitions live in [`sw/app/android/ObdLogger/res/xml/`](sw/app/android/ObdLogger/res/xml/). The Python converter reuses these definitions through [`probe.py`](sw/app/probe.py), including raw-to-physical conversions and compound measurements. Firmware polling defaults are defined in [`carmon.c`](fw/common/Source/carmon.c); Toyota CAN-to-PID mappings are in [`obd.c`](fw/common/Source/obd.c).

## License

No repository-wide license file is included. Individual source files contain copyright notices and vendor-specific terms, including bundled Microchip code. Review those terms and obtain clarification from the author before redistributing or reusing material where permission is unclear.
