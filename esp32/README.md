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
| `main/main.c` | Starts the UI, or headless mode if the display fails to come up |

## Build

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

## Fault code descriptions

If `../ross-tech-fault-codes.csv` (code,meaning) exists at build time it is embedded
and the boot log shows `fault code table: N descriptions`. Without it, faults
show as codes only. The file is gitignored.

## First test

If the panel fails to come up, the firmware falls back to headless mode and
logs to the serial monitor. With the cable in the P4 and in the car (ignition
on), headless mode should show:

```
I ftdi: KKL cable ready at 9600 baud
I vagscanner: probing 01
I vagscanner: found 01 (KW1281): ...ident...
I vagscanner: 0 fault code(s) on 01
```

If you never see "KKL cable ready", the port isn't acting as USB host (no 5 V
on the port, or it's the USB-Serial/JTAG port).

## Hardware status

Not yet tested on a car.

Verified on the board: chip rev v1.3 boots, panel orientation (MADCTL 0xC0) is
correct, touch needs mirror_x=1 / mirror_y=0, and the KKL cable enumerates on
the second USB port ("KKL cable ready at 9600 baud").
