# Handover: ESP32 VAG Scanner (macOS + ESP32-P4)

## Direct module access (2026-10-29)

- Both landing screens always show 01/02/03/08/15/17, plus More modules for the
  remaining known addresses. Scan remains optional and updates cards by address.
- Opening a card automatically reads faults. Identification and fault reading share
  one KW1281 connection; the ESP32 worker emits MODULE before FAULTS for direct reads.
- Unchecked cards, no response, unsupported KWP2000, and zero faults have distinct states.
  macOS clears results when changing cables; reopening a module refreshes its faults.
- The P4 header is shorter so all six shortcuts fit. Debug console: `module HEX`
  opens and reads directly; `modules` opens the picker. `open N` now indexes the home
  cards (the six shortcuts come first), rather than scan discovery order.
- Verified: macOS build; six XCTest integration tests against simulated cars; protocol
  tests; ESP-IDF build; host exercise of the ESP32 fault worker against the fake ECU
  (direct/repeated/missing/unsupported/recovery, one connect call per read). macOS
  landing screen visually checked. No board connected for flashing or touch validation;
  real-car timing remains unverified.
- Startup is manual on both platforms. The ESP32 headless fallback only logs cable
  status; it never queues a scan or chains an engine fault read. A failed LVGL lock
  also enters this idle fallback. Restore the display and restart to use the controls.
  Host regression coverage: `python3 esp32/tests/run_startup_test.py`.
- Run app integration tests with
  `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test --package-path mac`.

## Status (2026-10-25, evening)

- **Task 1 done.** `kwcore` rewritten. The simulated car in `kwcore/fake_ecu.c` (engine 01,
  cluster 17, central convenience 46, KWP2000 airbag 15) backs 7 tests that pass:
  `make -C kwcore test`. They fail when the old bugs are put back. **Keep them passing; add a
  test with every protocol change.**
- **Task 2 done** apart from the car test. `kwcli` + `io_serial.c` (cu.* ports, IOSSIOSPEED,
  DTR, echo swallow, `KW_TRACE=1` byte log). Break ioctls succeed on Apple's driver. When
  tested, the cable was not on a car (no echo), so nothing has woken a real controller yet.
- **Task 3 done.** `mac/` links kwcore (symlinks in `Sources/KWCore`). It has scan, faults,
  clear (with confirmation) and live measuring blocks. "Demo car (simulated)" in the cable
  picker runs the fake car; `swift run ESP32VAGScanner --demo` opens straight into it.
- **Tasks 4-5 done and building** on ESP-IDF v5.5.6 (`~/esp/esp-idf-v5.5`), with no warnings
  in our code. The display and touch drivers are vendored in `esp32/components/jc8012p4a1c`.
  Not yet flashed to the board. See `esp32/README.md` for what's unverified.
- **Addresses are hex** (`0x17` = cluster, `0x46` = CCM, `0x56` = radio), as upstream parses them.
- **P4 flashed and running** (chip rev v1.3, so the build targets revisions below v3). The display and touch
  start, and the KKL cable in the P4's other USB port reports "KKL cable ready at 9600 baud".
- **Fault descriptions:** `kwcore/fault_table.c` loads the user's `ross-tech-fault-codes.csv`
  (gitignored, not ours to publish), giving 878 usable codes. P-codes convert as
  16384 + 1024 * first digit + last three. The Mac app reads
  `~/Library/Application Support/ESP32 VAG Scanner/fault-codes.csv`; the P4 build embeds the CSV if present.

- **Look and car detection (2026-10-25, night):** dark theme on both apps. `kwcore/car_profile.c`
  recognises the owner's cars from part-number prefixes in the scan (7D/70 = T4 EuroVan,
  1H/1E = Mk3 Golf; engine ECU prefixes vote weakly) and shows that car's photo; otherwise
  the Golf Mk1-Mk8 family tree. Photos are Wikimedia Commons (credits in `assets/CREDITS.md`,
  shown in both apps). `assets/tools/fetch.py` downloads them and `make_assets.py` builds the
  P4 RGB565 heroes and the Mac banners. Demo cars: `fake_ecu_init_car` (EuroVan / Harlequin /
  Golf IV), on the Mac via the cable menu or `--demo=<car> [--overview]`, on the P4 by holding Scan.
- **Fault types and details (2026-10-25):** `kw_fault_type()` in `kwcore/fault_table.c` turns
  status bits 0-6 into the standard VAG text (`29` = Short to Ground; `00`/`35` = none). The list is
  **pulled, not typed in**: `python3 assets/tools/fetch_fault_types.py` writes `kwcore/fault_types.h`
  from KLineKWP1281Lib_ESP32 at a pinned commit. It's GPL-3.0, so the header is gitignored like the
  CSV; without it the apps show the number only. After fetching on the Mac, `touch kwcore/fault_table.c`
  (SwiftPM doesn't notice the new header). Both apps
  show it on the fault card, and tapping a card opens the details. KW1281 stores only code + status
  byte: there is **no mileage, occurrence count or freeze frame** to read (that's KWP2000/UDS).
- **P4 debug console** (115200 on the CH340 port): `assets/tools/p4shot.py PORT "demo eurovan"
  wait:10 shot:out.png` drives the UI and saves half-size screenshots.

**Next steps, in order:**
1. On the car, ignition on: `KW_TRACE=1 kwcore/kwcli /dev/cu.usbserial-210 ident 01`.
   If it fails, compare with `kw1281test /dev/cu.usbserial-210 9600 1 ReadIdent`.
2. Plug the KKL cable into the P4 and scan the car. The P4 has tonight's build (flashed over its
   CH340 port, `idf.py -p /dev/cu.usbserial-XXXX flash`); the fault type line shows on both apps.
   Note the CH340 and the KKL cable can both show up as `/dev/cu.usbserial-210`.
3. Screenshots for the README live in `docs/screenshots/`: Mac via `screencapture -l <window id>` on
   `--demo` runs, P4 via `assets/tools/p4shot.py` (needs pyserial + Pillow).

---

Read this whole file before changing code. It records decisions and verified facts.
Where it conflicts with `esp32/README.md` or earlier comments, this file wins.

## Goal

Read VW/Audi controllers over the K-line (KW1281 protocol): scan modules, read ident,
read/clear fault codes, read measuring groups. Two targets, **same cable, same protocol core**:

1. **macOS**: a SwiftUI app (`mac/`) linking the C core.
2. **ESP32-P4**: a Guition JC8012P4A1C-style unit (screen + case). The KKL cable plugs
   into the P4's USB port. The P4 is the USB host.

## Hard constraints (do not revisit)

- **The interface is the user's KKL USB cable on both targets.** Verified on the Mac:
  FTDI, VID `0x0403`, PID `0x6001` (FT232R/BM class), product "USB <-> Serial",
  **no USB serial number**, shows up as `/dev/cu.usbserial-210`.
- **The P4 is sealed: USB ports only, no GPIO access, no wiring, no soldering.**
  Any design that drives the K-line from P4 GPIO pins is impossible.
- **No Wi-Fi bridge and no "P4 as interface for the Mac" design.** Each target talks to its own cable.
- **The ground truth is the upstream C# tool in `kw1281test/`.** When unsure how the
  protocol behaves, read `KwpCommon.cs` and `KW1281Dialog.cs` and match them.

## Delete / abandon

- `esp32/kline_esp32.c` (the GPIO bit-bang backend): replace it with a USB host FTDI backend.
- The wiring section of `esp32/README.md`. It is also electrically wrong: the K-line idles
  at 12-14 V, not 5 V.
- `esp32/api.c` (the HTTP JSON API) and `macgui/gui.py` (the Tk client).
- The probe programs and binaries in `kwcore/` (`busy*`, `cabletest*`, `echo*`,
  `wakeprobe*`, `stubtest*`, `out.txt`, `kwcli` binary). Keep `stubtest2.c` as the
  starting point for a proper fake-ECU test (see Task 1).

## Architecture

```
kwcore/kw1281.c   protocol, hardware-independent, plain C99
kwcore/kw1281.h   KwIo interface + API
kwcore/io_serial.c         macOS/Linux backend (termios)
esp32/main/io_esp32_ftdi.c ESP32-P4 backend (ESP-IDF USB host + FTDI)
mac/              SwiftUI app (ESP32VAGScanner), links kwcore as a SwiftPM C target
esp32/            ESP-IDF project: LVGL UI + K-line task
```

### KwIo contract (fix the header comment to say this)

- `read_byte(ctx)`: returns the next byte received from the ECU, or -1 on timeout.
- `write_byte(ctx, b)`: sends one byte **and consumes its echo before returning**.
  K-line is half-duplex; a KKL cable receives every byte it transmits. The backend must
  read and discard that echo so the core never sees it. (Upstream:
  `KwpCommon.WriteByteAndDiscardEcho`.)
- `set_break(ctx, on)`: on=1 pulls TX low (break), on=0 releases it to idle high.
- `flush_in(ctx)`: discards buffered received bytes.
- Add `set_dtr(ctx, on)`. Many KKL cables power their K-line transceiver from DTR.
  Upstream pulses DTR low for 300 ms, then high, then waits 300 ms after opening the port
  (`Program.cs` `OpenPort`). Do the same in both backends' open functions.

## Task 1: fix kwcore protocol bugs (critical path, do first)

Build a fake ECU: a `KwIo` backed by a scripted byte queue that also checks what the core
writes. Replay a full ReadIdent and ReadFaultCodes exchange through it. A target must not
be considered working until its exchange passes against this fake.

Bugs in `kwcore/kw1281.c`, each one stops it working on a car:

1. **Echo.** Implement echo consumption in the backends per the contract above. Then
   `read_complement` actually reads the ECU's complement, as intended.
2. **Block length.** In `send_block`, the length byte = counter + title + body + end =
   `body_len + 3`. The current `body_len + 2` is wrong. (Upstream: `blockBytes.Count + 2`,
   where `blockBytes` already includes the title.)
3. **`send_block` timing.** Upstream waits 25 ms before every block, waits 2 ms (R6) before
   the first byte, and waits 2 ms after each acknowledged byte. The `0x03` block end is
   written but **not** acknowledged by the ECU.
4. **`receive_blocks` is broken**:
   - It stores the `int` result in `uint8_t`, so `title < 0` is never true, and a timeout
     loops forever. Use `int`.
   - It must collect **every** block up to and including the terminating ACK (0x09) or
     NAK (0x0A), sending an ACK block after each non-ACK block. Return the collected data
     blocks (title + body + length for each) to the caller. Do not return the title of the
     final ACK, and do not let the ACK overwrite the data buffer.
5. **Response titles differ from request titles.** Request → response:
   - ReadIdent `0x00` → AsciiData `0xF6`
   - FaultCodesRead `0x07` → FaultCodesResponse `0xFC`
   - FaultCodesDelete `0x05` → FaultCodesResponse `0xFC` (the remaining codes)
   - GroupRead `0x29` → GroupReadResponse `0xE7`
   - (`0x02` is GroupReadResponseWithText; `0x0A` NAK = not supported or denied.)
   - Full list: `kw1281test/BlockTitle.cs`.
6. **Connect sequence.** After `kw_wakeup`, the ECU **sends ident blocks unprompted**.
   Every operation must first run "connect": `receive_blocks` and keep the 0xF6 blocks
   as the ident. If the first body byte of any 0xF6 block is `> 0x7F`
   (`AsciiDataBlock.MoreDataAvailable`), send a ReadIdent `0x00` block and receive again.
   Repeat until no more data. Only then send the actual request. The current faults and
   group functions skip this.
7. **Fault read never sends its request.** It must send a `0x07` block after connect.
8. **Parsers walk the whole 256-byte buffer.** Parse exactly `len - 3` body bytes per
   block, in groups of 3. For faults, skip `dtc == 0xFFFF && status == 0x88` (means "no
   faults"). Fault codes can span several 0xFC blocks; concatenate them. For groups,
   handle 0xE7 only; a NAK means the group doesn't exist.
9. **Ident text.** Each 0xF6 body byte becomes `(char)(b & 0x7F)`. Concatenate the blocks.
10. **Disconnect.** Send a proper End block (title `0x06`) via `send_block`. A bare `0x03`
    is not an End block. Call it after every operation, before waking another address.
11. **Wakeup robustness.** Retry the wake-up up to 3 times, sleeping 1 s between attempts.
    Allow 2 s for the sync byte. In the first received block, handle a repeated
    `0x55 lsb msb` sync (`KW1281Dialog.ReadAndAckByteFirst`): ACK it again, then continue.
12. **Globals.** Move `block_counter` (and the backends' `static` contexts) into a session
    struct passed to every call. The P4 must not share state between tasks.
13. **Block counter.** It is a byte and wraps at 0xFF→0x00. Treat "first block of session"
    with an explicit flag, not `block_counter == 0`.

## Task 2: macOS backend (`kwcore/io_serial.c`)

- Open `/dev/cu.*`, never `/dev/tty.*`. Keep `O_NONBLOCK` for `open`, then clear it
  (`fcntl F_SETFL`) and use `poll` for read timeouts.
- Honour the `baud` argument. 9600 and 4800 are standard termios speeds. 10400 is not:
  set it with `ioctl(fd, IOSSIOSPEED, &speed)` (`<IOKit/serial/ioss.h>`) after `tcsetattr`.
- Break: `TIOCSBRK` / `TIOCCBRK`. DTR: `TIOCMBIS` / `TIOCMBIC` with `TIOCM_DTR`.
- `write_byte`: write, then read and discard 1 byte (the echo) with the read timeout.
- **Unverified: whether Apple's built-in FTDI driver actually drives break.** Prove it on
  the car before building UI. With the cable in the car and ignition on, both of these
  should print the engine ECU's ident:
  - upstream: `kw1281test /dev/cu.usbserial-210 9600 1 ReadIdent`
  - ours: `kwcli /dev/cu.usbserial-210 ident 1`

  If upstream works and ours doesn't, the bug is ours. If neither works, report back;
  do not switch to GPIO or Wi-Fi designs.

## Task 3: macOS app (`mac/`)

- Add `kwcore` as a SwiftPM C target and call it directly. Remove the `Process` call to
  the .NET `kw1281test` binary and its text parsing.
- Run all K-line work off the main thread, in one serial queue or actor that owns the
  session; the UI must never block.
- Port list: `/dev/cu.usbserial*`.
- Wrap the root view in `NavigationStack`; `NavigationLink` doesn't navigate without it.

## Task 4: ESP32-P4 backend (`io_esp32_ftdi.c`)

**First, find out which USB port on the unit is host-capable:** it must supply 5 V VBUS
and connect to the P4's USB OTG controller. A port that only powers the board, or that is
the USB-Serial/JTAG debug port, will not work. Report back if none is.

Use ESP-IDF's USB host stack with Espressif's FTDI VCP driver from the esp-usb component
registry (look for `usb_host_ftdi_vcp` / `usb_host_vcp`). **Read the component's headers
for the exact API; do not guess function names.** It is C++, so wrap it in a small
`extern "C"` shim if needed. If it can't send a raw vendor control request, use
`usb_host_cdc_acm`'s custom-request function, or talk to `usb_host` directly.

FTDI vendor control requests (bmRequestType `0x40`, wIndex = interface, usually 0):

| Purpose | bRequest | wValue |
|---|---|---|
| Reset | `0x00` | `0` |
| DTR on / off | `0x01` | `0x0101` / `0x0100` |
| Baud rate | `0x03` | divisor code: 9600 = `0x4138`, 10400 ≈ `0x4120` (verify against libftdi `ftdi_convert_baudrate`) |
| 8N1, break off | `0x04` | `0x0008` |
| 8N1, break **on** | `0x04` | `0x4008` (bit 14 = break) |
| Latency timer | `0x09` | `1` (ms; the default is 16) |

- **Bulk-IN packets begin with 2 modem-status bytes.** Strip them unless the driver
  already does (check). Otherwise they look like K-line data.
- The receive callback pushes bytes into a FreeRTOS stream buffer. `read_byte` waits on it
  with a timeout. `write_byte` sends the byte, then reads and discards the echo.
  `flush_in` resets the buffer.
- Set break through the `0x04` request. 5-baud bits are 200 ms long, so USB
  control-transfer jitter (about 1 ms) doesn't matter.
- Replace `busy_wait_ms` in the core with an injectable delay (e.g. a `delay_ms` field in
  KwIo or the session). Use `vTaskDelay` for waits of 10 ms or more, and
  `esp_rom_delay_us` below that. Busy-spinning for 2 s trips the task watchdog.

## Task 5: ESP32-P4 app

- **Fix the project layout.** The root `CMakeLists.txt` includes
  `$ENV{IDF_PATH}/tools/cmake/project.cmake` and calls `project()`. The sources live in
  `main/` with their own `idf_component_register`. Pull in `../kwcore/kw1281.c` via `SRCS`
  and `INCLUDE_DIRS`. The entry point is `void app_main(void)`.
- **Display.** Use the board's real esp_lcd MIPI-DSI JD9365 + GSL3680 touch bring-up,
  e.g. https://github.com/chipguyhere/JC8012P4A1C_display.
- **Check APIs against LVGL 9.** These are not LVGL 9 APIs: `lvgl_init`,
  `lv_obj_on_click`, `lv_obj_set_grid_cols/rows`, `lv_obj_set_style_font_size`,
  `lv_obj_set_style_padding`. Use `lv_init`, `lv_obj_add_event_cb`,
  `lv_obj_set_grid_dsc_array`, `lv_obj_set_style_text_font`, `lv_obj_set_style_pad_all`.
  Protect LVGL calls from other tasks with the port's LVGL lock.
- **Run the K-line in its own task.** One FreeRTOS task owns the USB device and the
  session. The UI posts requests (scan / ident / faults / clear / group) to a queue and
  gets results back. Never call kwcore from an LVGL event callback.
- **Don't block startup.** Never scan at boot before the UI is drawn. Show "Connect cable"
  until the FTDI device attaches, then offer a Scan button with progress.
- No Wi-Fi.

## Definition of done

1. The fake ECU passes for ident, faults, clear and group.
2. On the car (address 1, ignition on), `kwcli ... ident 1` and `faults 1` work on macOS.
3. The macOS app shows ident and fault codes for scanned modules without freezing the UI.
4. The P4 shows the same, with the same cable.

## Working rules

- Change one thing, test it, then move on. Don't add new hardware or architecture ideas;
  if blocked, write down exactly what failed (command plus output) and stop.
- Don't commit build outputs (`.build/`, `build/`, compiled probe binaries).
