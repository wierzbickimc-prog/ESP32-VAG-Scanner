# ESP32 VAG Scanner: ESP32-P4 firmware

The P4 unit acts as a USB host for the same FTDI KKL cable the Mac uses
(`0403:6001`). There is no wiring: plug the cable into the P4's host-capable USB
port and the cable's OBD end into the car.

The protocol is `../kwcore/kw1281.c`, shared with the Mac app and tested
against a fake ECU (`make -C ../kwcore test`).

## Layout

| File | Role |
|---|---|
| `main/io_esp32_ftdi.c` | KwIo backend: USB host + FTDI vendor requests (break, DTR, baud, 1 ms latency) |
| `main/kline_task.c` | The only task that touches the cable; UI sends it requests |
| `main/gui.c` | LVGL 9 UI: scan, module detail, faults, clear, measuring groups |
| `main/board_display.c` | JD9365 panel + GSL3680 touch bring-up, LVGL via esp_lvgl_port |
| `components/jc8012p4a1c/` | Panel/touch drivers vendored from chipguyhere/JC8012P4A1C_display (MIT) |
| `main/main.c` | Starts the UI; stays idle if the display is unavailable |

## Build: 10.1-inch JC8012P4A1C

Builds cleanly with **ESP-IDF v5.5.6** (installed at `~/esp/esp-idf-v5.5`). v5.5.0
does not work: the esp_lvgl_port release it resolves needs a 5.5.x patch-release API.

```
. ~/esp/esp-idf-v5.5/export.sh
cd esp32
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash monitor   # the board's CH340 port
```

Components (`usb_host_cdc_acm`, `lvgl`, `esp_lvgl_port`, `esp_lcd_touch`) are
fetched automatically from `main/idf_component.yml`.

## Build: 4.3-inch JC4880P443

The PCB marked `JC4880P443`, with a `JC-ESP32P4-M3` module, has a separate
480x800 ST7701S / GT911 board profile. The owner's unit was identified as
**ESP32-P4 revision v3.2, 16 MB flash** on 2026-11-13. Its **top USB-C port**
(owner's orientation) is native USB Serial/JTAG, currently `/dev/cu.usbmodem101`.
The original board uses v1.x silicon: their bootloaders are incompatible.
Keep both the config file and build directory separate:

```sh
. ~/esp/esp-idf-v5.5/export.sh
cd esp32
idf.py -B build-jc4880p443 \
  -D SDKCONFIG=sdkconfig.jc4880p443 \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.jc4880p443' \
  build merge-bin -o esp32-vag-scanner-jc4880p443.bin
```

The merged image is `build-jc4880p443/esp32-vag-scanner-jc4880p443.bin`.
To flash this profile after building, use the **same configuration arguments**:

```sh
idf.py -B build-jc4880p443 \
  -D SDKCONFIG=sdkconfig.jc4880p443 \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.jc4880p443' \
  -p /dev/cu.usbmodem101 flash monitor
```

This profile selects v3.x boot support, USB Serial/JTAG console input/output,
the vendor's LCD reset GPIO5, backlight GPIO23, and touch I2C SDA7/SCL8.
The GT911 is probed at both 0x5D and 0x14; unconfirmed reset/interrupt pins
are left alone. C6 SDIO pins match Guition's board definition. Its firmware
is not updated by this build.

The UI scales its geometry and fonts for the smaller display. The build resizes
the existing RGB565 images to 480x384 with `assets/tools/resize_p4_heroes.py`
(Python standard library only). No SD card is needed.

Panel timing and initialization come from
[Guition's board definition](https://github.com/Guition-JC/Guition/tree/737d0c14001f82e79ae82a41257cf0a3c86bc101/components/board/guition_boards/jc4880p443).
The initialization table and its license are kept in `components/jc4880p443/`.
The registry board component itself needs ESP-IDF 6.1; this port uses the
table with the project's existing ESP-IDF 5.5.6 drivers.

**Validation:** both board profiles compile; protocol/health, startup, and Wi-Fi
event tests pass. Host rendering covers the main screens, health prerequisite
wrapping, and the Wi-Fi keyboard at both resolutions. The original board's six
preview screens match their pre-port pixels. The 4.3-inch firmware was flashed on
2026-11-13: 32 MB PSRAM passes its test at 200 MHz, the panel/LVGL initialize,
GT911 answers at 0x5D, the native debug console returns screenshots, and the C6
completes SDIO/Wi-Fi startup. The owner confirmed a stable display and working
touch in Demo after the fixes below; serial capture shows no further panic/reset
during the boot and interaction checks. Wi-Fi scan/connection and KKL
enumeration/VBUS still need validation. The web
installer and release workflow continue to ship the 10.1-inch profile.

Bring-up exposed two compatibility issues now handled by this port: DSI clock
selection must use `phy_clk_src=0` rather than the legacy clock alias on v3.x;
this factory C6 sends duplicate Wi-Fi station-start events. The wrappers in
`main/wifi_events.c` prevent a repeated lwIP interface addition and allow normal
stop/restart cycles. The regression test runs without hardware:

```sh
python3 esp32/tests/run_wifi_events_test.py   # from the repository root
```

The full factory P4 flash backup and checksum are kept in the gitignored
`esp32/board-backups/` directory so cleaning a build does not remove them.

## Direct module access

The home screen always offers 01 Engine, 02 Transmission, 03 ABS, 08 HVAC,
15 Airbags, and 17 Instruments. Tap a card to connect to that address and read its
faults automatically; no scan is needed. **More modules** lists the other known
addresses, and **Scan car** still discovers controllers. Addresses are hexadecimal.
A controller that does not answer stays distinct from one reporting no faults.

The serial debug console also accepts `module 01` (direct open/read by hex address)
and `modules` (the More modules picker). `open N` opens the N-th home card; the first
six cards remain in the order above even after a scan.

## Fault code descriptions

If `../ross-tech-fault-codes.csv` (code,meaning) exists at build time it is embedded
and the boot log shows `fault code table: N descriptions`. Without it, faults
show as codes only. The file is gitignored.

## First test

Connecting the cable prepares the USB interface. It does not wake or query any
vehicle controller. With the ignition on, tap **Scan car** or a module card to
start communication.

If the panel or UI lock fails to initialize, the firmware logs the failure and
remains idle. It does not scan or read engine faults automatically. Restore the
display and restart the unit to use the controls. Cable detection still logs:

```
I ftdi: KKL cable ready at 9600 baud
I vagscanner: cable ready; no vehicle communication requested
```

If you never see "KKL cable ready", the port isn't acting as USB host (no 5 V
on the port, or it's the USB-Serial/JTAG port).

The startup regression test runs on the host without a board or ESP-IDF:

```sh
python3 esp32/tests/run_startup_test.py   # from the repository root
```

It covers display failure, UI lock failure, normal startup, and cable reconnects.

## Hardware status

Not yet tested on a car.

Verified on the board: chip rev v1.3 boots, panel orientation (MADCTL 0xC0) is
correct, touch needs mirror_x=1 / mirror_y=0, and the KKL cable enumerates on
the second USB port ("KKL cable ready at 9600 baud").
