"""Parts and nets of the K-line dongle: one list feeds both the schematic and the PCB."""

import os
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
FP = os.environ.get("KICAD_FOOTPRINT_DIR", "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")

# Symbol pin lists: (number, name, side) where side is L/R/T/B on the symbol body.
SYMBOLS = {
    "LVC1G17": {"pins": [("1", "NC", "L"), ("2", "A", "L"), ("5", "VCC", "R"), ("4", "Y", "R"), ("3", "GND", "R")]},
    "N_MOS": {"pins": [("1", "G", "L"), ("3", "D", "R"), ("2", "S", "B")]},
    "TestPoint": {"pins": [("1", "TP", "L")]},
    "PWR_FLAG": {"pins": [("1", "PWR", "L")]},
    "R":     {"two": "res"},
    "C":     {"two": "cap"},
    "Fuse":  {"two": "fuse"},
    "D_Schottky": {"two": "diode"},
    "D_TVS": {"two": "tvs"},
    "LED":   {"two": "led"},
    "OBD_Wires": {"pins": [("1", "+12V(16)", "R"), ("2", "GND(4)", "R"),
                           ("3", "GND(5)", "R"), ("4", "K(7)", "R")]},
    "R-78HB": {"pins": [("1", "VIN", "L"), ("2", "GND", "B"), ("3", "VOUT", "R")]},
    "L9637D": {"pins": [("7", "VS", "L"), ("3", "VCC", "L"), ("4", "TX", "L"), ("1", "RX", "L"),
                        ("6", "K", "R"), ("8", "LI", "R"), ("2", "LO", "R"), ("5", "GND", "R")]},
    "74AHCT1G125": {"pins": [("2", "A", "L"), ("1", "~{OE}", "L"), ("5", "VCC", "R"),
                             ("4", "Y", "R"), ("3", "GND", "R")]},
    "XIAO_ESP32C3": {"pins": [("1", "D0/GPIO2", "L"), ("2", "D1/GPIO3", "L"), ("3", "D2/GPIO4", "L"),
                              ("4", "D3/GPIO5", "L"), ("5", "D4/GPIO6", "L"), ("6", "D5/GPIO7", "L"),
                              ("7", "D6/GPIO21", "L"),
                              ("14", "5V", "R"), ("13", "GND", "R"), ("12", "3V3", "R"),
                              ("11", "D10/GPIO10", "R"), ("10", "D9/GPIO9", "R"), ("9", "D8/GPIO8", "R"),
                              ("8", "D7/GPIO20", "R")]},
}

# ref, symbol, value, footprint, {pin: net}, (sch x, sch y), description
PARTS = [
    ("J1", "OBD_Wires", "OBD-II plug wires", "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
     {"1": "VBAT_IN", "2": "GND", "3": "GND", "4": "KLINE"}, (30, 50),
     "Solder the OBD-II plug's pins 16, 4, 5, 7 here"),
    ("F1", "Fuse", "MF-MSMF050/60X-2", "Fuse:Fuse_1812_4532Metric",
     {"1": "VBAT_IN", "2": "VBAT_F"}, (60, 40), "Bourns, 0.5 A hold at 23 C, 60 V, 10 A maximum fault current"),
    ("D1", "D_Schottky", "SS16", "Diode_SMD:D_SMA",
     {"1": "VBAT", "2": "VBAT_F"}, (75, 40), "Reverse-polarity protection, 60 V 1 A"),
    ("D2", "D_TVS", "SMBJ18CA", "Diode_SMD:D_SMB",
     {"1": "VBAT", "2": "GND"}, (90, 40), "Diodes Inc., 18 V standoff, 29.2 V maximum clamp at rated pulse current; surge qualification pending"),
    ("C1", "C", "10u 50V X7R", "Capacitor_SMD:C_1210_3225Metric",
     {"1": "VBAT", "2": "GND"}, (100, 40), "X7R"),
    ("C2", "C", "100n 50V", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "VBAT", "2": "GND"}, (110, 40), "L9637 VS decoupling"),
    ("U1", "R-78HB", "R-78HB5.0-0.5L", "Converter_DCDC:Converter_DCDC_RECOM_R-78HB-0.5L_THT",
     {"1": "VBAT", "2": "GND", "3": "+5V"}, (130, 40), "RECOM 9-72 V in, 5 V 0.5 A out; L = lies flat (8.5 mm high)"),
    ("C3", "C", "10u 10V X7R", "Capacitor_SMD:C_0805_2012Metric",
     {"1": "+5V", "2": "GND"}, (150, 40), "Converter output"),
    ("C4", "C", "100n 25V X7R", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "+5V", "2": "GND"}, (160, 40), "L9637 VCC decoupling"),
    ("D3", "D_Schottky", "B5819WS", "Diode_SMD:D_SOD-323",
     {"1": "XIAO_5V", "2": "+5V"}, (175, 40), "Keeps USB VBUS out of the converter (Seeed's recommended 5V-pad input)"),
    ("U2", "L9637D", "L9637D", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
     {"1": "KRX", "2": None, "3": "+5V", "4": "KTX", "5": "GND", "6": "KLINE", "7": "VBAT", "8": "GND"},
     (70, 100), "ISO 9141 K-line transceiver (no TX timeout)"),
    ("R1", "R", "510 1W", "Resistor_SMD:R_2512_6332Metric",
     {"1": "VBAT", "2": "KLINE"}, (100, 95), "ISO 9141 tester pull-up"),
    ("U3", "74AHCT1G125", "SN74AHCT1G125DBVR", "Package_TO_SOT_SMD:SOT-23-5",
     {"1": "TX_OE_N", "2": "TXD", "3": "GND", "4": "KTX", "5": "+5V"}, (40, 140),
     "3.3 V -> 5 V level shift for TX"),
    ("C5", "C", "100n 25V X7R", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "+5V", "2": "GND"}, (70, 140), "U3 decoupling"),
    ("R3", "R", "10k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "+3V3", "2": "TXD"}, (20, 140), "Holds TX idle while the ESP32 boots"),
    ("U6", "LVC1G17", "SN74LVC1G17DCKR", "Package_TO_SOT_SMD:SOT-353_SC-70-5",
     {"1": None, "2": "KRX", "3": "GND", "4": "RXD", "5": "+3V3"}, (0, 0),
     "5 V tolerant Schmitt RX buffer with Ioff; avoids USB-only backfeeding U2 VCC"),
    ("C8", "C", "100n 25V X7R", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "+3V3", "2": "GND"}, (0, 0), "U6 decoupling"),
    ("R6", "R", "100k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "VBAT", "2": "VSENSE"}, (140, 140), "100k 1% upper divider; 0.11 mA at 12.6 V"),
    ("R7", "R", "10k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "VSENSE", "2": "GND"}, (150, 140), "10k 1% lower divider; 29.2 V TVS clamp -> 2.65 V at the ADC"),
    ("C6", "C", "100n 25V X7R", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "VSENSE", "2": "GND"}, (160, 140), ""),
    ("R4", "R", "1k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "LED", "2": "LED_A"}, (180, 140), ""),
    ("D5", "LED", "Green", "LED_SMD:LED_0603_1608Metric",
     {"1": "GND", "2": "LED_A"}, (190, 140), "Status"),
    ("U4", "XIAO_ESP32C3", "Seeed XIAO ESP32C3", "dongle:XIAO_ESP32C3_Carrier",
     {"1": None, "2": "VSENSE", "3": "TXD", "4": "RXD", "5": None, "6": "TX_EN", "7": None,
      "8": None, "9": None, "10": None, "11": "LED", "12": "+3V3", "13": "GND", "14": "XIAO_5V"},
     (150, 95), "Castellated, solder flat; USB-C at the board edge"),
    ("Q1", "N_MOS", "BSS138-7-F", "Package_TO_SOT_SMD:SOT-23",
     {"1": "TX_EN", "2": "GND", "3": "TX_OE_N"}, (0, 0), "Release K-line while MCU is off/reset; GPIO7 HIGH enables TX buffer"),
    ("R10", "R", "10k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "+5V", "2": "TX_OE_N"}, (0, 0), "Default-disable U3; independent of MCU supply"),
    ("R11", "R", "100k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "TX_EN", "2": "GND"}, (0, 0), "Q1 gate pull-down during reset / power off"),
]

# Explicit electrical types make ERC useful; passive remains correct for R/C/diodes/connectors.
PIN_TYPES = {
    "LVC1G17": {"1": "no_connect", "2": "input", "3": "power_in", "4": "output", "5": "power_in"},
    "N_MOS": {"1": "input", "2": "passive", "3": "open_collector"},
    "R-78HB": {"1": "power_in", "2": "power_in", "3": "power_out"},
    "L9637D": {"1": "output", "2": "output", "3": "power_in", "4": "input",
                "5": "power_in", "6": "bidirectional", "7": "power_in", "8": "input"},
    "74AHCT1G125": {"1": "input", "2": "input", "3": "power_in", "4": "tri_state", "5": "power_in"},
    "XIAO_ESP32C3": {**{str(n): "bidirectional" for n in range(1, 15)},
                     "12": "power_out", "13": "power_in", "14": "power_in"},
    "PWR_FLAG": {"1": "power_out"},
}

# Test points live on the back; keep them accessible after the XIAO is soldered.
for i, n in enumerate(["VBAT", "+5V", "+3V3", "KLINE", "TXD", "RXD", "GND", "VSENSE"], 1):
    PARTS.append((f"TP{i}", "TestPoint", n, "TestPoint:TestPoint_Pad_D1.0mm",
                  {"1": n}, (0, 0), "Back-side 1 mm probe pad"))
# Source declarations after passive input protection and the removable supply link.
for i, n in enumerate(["VBAT", "GND", "XIAO_5V"], 1):
    PARTS.append((f"#FLG0{i}", "PWR_FLAG", "PWR_FLAG", "", {"1": n}, (0, 0),
                  "External power path; XIAO_5V is supplied through D3 or by USB"))

DATASHEETS = {
    "U1": "https://www.recom-power.com/pdf/Innoline/R-78HBxx-0.5_L.pdf",
    "U2": "https://www.st.com/resource/en/datasheet/l9637.pdf",
    "U3": "https://www.ti.com/lit/ds/symlink/sn74ahct1g125.pdf",
    "U4": "https://files.seeedstudio.com/wiki/XIAO_WiFi/Resources/Seeeduino-XIAO-ESP32C3-SCH.pdf",
    "F1": "https://www.bourns.com/docs/product-datasheets/mf-msmf.pdf",
    "D2": "https://www.diodes.com/part/view/SMBJ18CA",
}

TWO_PIN_NAMES = {"diode": ("K", "A"), "tvs": ("A1", "A2"), "led": ("K", "A")}

# Turnkey PCBA purchasing data. Value remains the electrical value on the drawing.
# J1 is deliberately bare wire pads; copper test points and flags are not purchases.
DNP = {"J1"}
PROCUREMENT = {
    "U1": ("RECOM", "R-78HB5.0-0.5L", DATASHEETS["U1"]),
    "U2": ("STMicroelectronics", "L9637D", DATASHEETS["U2"]),
    "U3": ("Texas Instruments", "SN74AHCT1G125DBVR", DATASHEETS["U3"]),
    "U4": ("Seeed Studio", "113991054", "https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html"),
    "F1": ("Bourns", "MF-MSMF050/60X-2", DATASHEETS["F1"]),
    "D1": ("Diodes Incorporated", "SS16-13-F", "https://www.diodes.com/assets/Datasheets/ds13004.pdf"),
    "D2": ("Diodes Incorporated", "SMBJ18CA-13-F", DATASHEETS["D2"]),
    "D3": ("Good-Ark", "B5819WS", "https://www.goodark.com/specification/B5817WS-B5819WS.pdf"),
    "D5": ("Lite-On", "LTST-C190KGKT", "https://optoelectronics.liteon.com/upload/download/DS22-2000-074/LTST-C190KGKT.PDF"),
    "C1": ("KEMET", "C1210C106K5RACTU", "https://search.kemet.com/download/specsheet/C1210C106K5RACTU"),
    "C2": ("KEMET", "C0603C104K5RACTU", "https://search.kemet.com/download/specsheet/C0603C104K5RACTU"),
    "C3": ("KEMET", "C0805C106K8RACTU", "https://search.kemet.com/download/specsheet/C0805C106K8RACTU"),
}
for ref in ["C4", "C5", "C6"]:
    PROCUREMENT[ref] = ("KEMET", "C0603C104K3RACTU", "https://search.kemet.com/download/specsheet/C0603C104K3RACTU")
for refs, mpn in [(["R1"], "RC2512FK-07510RL"),
                  (["R3", "R7"], "RC0603FR-0710KL"),
                  (["R4"], "RC0603FR-071KL"),
                  (["R6"], "RC0603FR-07100KL")]:
    for ref in refs:
        PROCUREMENT[ref] = ("YAGEO", mpn, "https://www.yageogroup.com/component-documentation/download/specsheet/" + mpn)


DATASHEETS["Q1"] = "https://www.diodes.com/assets/Datasheets/ds30144.pdf"
PROCUREMENT["Q1"] = ("Diodes Incorporated", "BSS138-7-F", DATASHEETS["Q1"])
PROCUREMENT["R10"] = PROCUREMENT["R3"]
PROCUREMENT["R11"] = ("YAGEO", "RC0603FR-07100KL", "https://www.yageogroup.com/component-documentation/download/specsheet/RC0603FR-07100KL")


PROCUREMENT["C8"] = PROCUREMENT["C4"]
DATASHEETS["U6"] = "https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf"
PROCUREMENT["U6"] = ("Texas Instruments", "SN74LVC1G17DCKR", DATASHEETS["U6"])
