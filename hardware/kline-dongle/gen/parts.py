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
    ("F1", "Fuse", "BSMD1812-050-60V", "Fuse:Fuse_1812_4532Metric",
     {"1": "VBAT_IN", "2": "VBAT_F"}, (60, 40), "PTC, 0.5 A hold at 23 C, 60 V, 40 A maximum fault current"),
    ("D1", "D_Schottky", "SS14", "Diode_SMD:D_SMA",
     {"1": "VBAT", "2": "VBAT_F"}, (75, 40), "Reverse-polarity protection, 40 V 1 A (D2 clamps below 30 V)"),
    ("D2", "D_TVS", "SMBJ18CA", "Diode_SMD:D_SMB",
     {"1": "VBAT", "2": "GND"}, (90, 40), "18 V standoff, 29.2 V maximum clamp at rated pulse current; surge qualification pending"),
    ("C1", "C", "10u 50V X7R", "Capacitor_SMD:C_1210_3225Metric",
     {"1": "VBAT", "2": "GND"}, (100, 40), "X7R"),
    ("C2", "C", "100n 50V X7R", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "VBAT", "2": "GND"}, (110, 40), "L9637 VS decoupling"),
    ("U1", "R-78HB", "R-78HB5.0-0.5L", "Converter_DCDC:Converter_DCDC_RECOM_R-78HB-0.5L_THT",
     {"1": "VBAT", "2": "GND", "3": "+5V"}, (130, 40), "RECOM 9-72 V in, 5 V 0.5 A out; L = lies flat (8.5 mm high)"),
    ("C3", "C", "10u 25V X5R", "Capacitor_SMD:C_0805_2012Metric",
     {"1": "+5V", "2": "GND"}, (150, 40), "Converter output"),
    ("C4", "C", "100n 50V X7R", "Capacitor_SMD:C_0603_1608Metric",
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
    ("C5", "C", "100n 50V X7R", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "+5V", "2": "GND"}, (70, 140), "U3 decoupling"),
    ("R3", "R", "10k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "+3V3", "2": "TXD"}, (20, 140), "Holds TX idle while the ESP32 boots"),
    ("U6", "LVC1G17", "SN74LVC1G17DCKR", "Package_TO_SOT_SMD:SOT-353_SC-70-5",
     {"1": None, "2": "KRX", "3": "GND", "4": "RXD", "5": "+3V3"}, (0, 0),
     "5 V tolerant Schmitt RX buffer with Ioff; avoids USB-only backfeeding U2 VCC"),
    ("C8", "C", "100n 50V X7R", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "+3V3", "2": "GND"}, (0, 0), "U6 decoupling"),
    ("R6", "R", "100k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "VBAT", "2": "VSENSE"}, (140, 140), "100k 1% upper divider; 0.11 mA at 12.6 V"),
    ("R7", "R", "10k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "VSENSE", "2": "GND"}, (150, 140), "10k 1% lower divider; 29.2 V TVS clamp -> 2.65 V at the ADC"),
    ("C6", "C", "100n 50V X7R", "Capacitor_SMD:C_0603_1608Metric",
     {"1": "VSENSE", "2": "GND"}, (160, 140), ""),
    ("R4", "R", "1k", "Resistor_SMD:R_0603_1608Metric",
     {"1": "LED", "2": "LED_A"}, (180, 140), ""),
    ("D5", "LED", "Green", "LED_SMD:LED_0603_1608Metric",
     {"1": "GND", "2": "LED_A"}, (190, 140), "Status"),
    ("U4", "XIAO_ESP32C3", "Seeed XIAO ESP32C3", "dongle:XIAO_ESP32C3_Carrier",
     {"1": None, "2": "VSENSE", "3": "TXD", "4": "RXD", "5": None, "6": "TX_EN", "7": None,
      "8": None, "9": None, "10": None, "11": "LED", "12": "+3V3", "13": "GND", "14": "XIAO_5V"},
     (150, 95), "Castellated, solder flat; USB-C at the board edge"),
    ("Q1", "N_MOS", "2N7002", "Package_TO_SOT_SMD:SOT-23",
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
    "F1": "https://www.lcsc.com/datasheet/lcsc_datasheet_2305121423_BHFUSE-BSMD1812-050-60V_C883142.pdf",
    "D2": "https://www.lcsc.com/datasheet/lcsc_datasheet_2407291900_MDD-Microdiode-Semiconductor-SMBJ18CA_C113991.pdf",
}

TWO_PIN_NAMES = {"diode": ("K", "A"), "tvs": ("A1", "A2"), "led": ("K", "A")}

# PCBA purchasing data, chosen from parts JLCPCB stocks (checked 2026-11-01).
# Value remains the electrical value on the drawing. J1 is deliberately bare wire
# pads; copper test points and flags are not purchases.
# ref: (manufacturer, MPN, LCSC/JLCPCB part number, datasheet or product link)
DNP = {"J1"}
LCSC_PDF = "https://www.lcsc.com/datasheet/lcsc_datasheet_"
PARTS_SOURCED = {
    "U1": ("RECOM", "R-78HB5.0-0.5L", "", DATASHEETS["U1"]),
    "U2": ("STMicroelectronics", "E-L9637D013TR", "C153038", DATASHEETS["U2"]),
    "U3": ("Texas Instruments", "SN74AHCT1G125DBVR", "C7484", DATASHEETS["U3"]),
    "U4": ("Seeed Studio", "113991054", "", "https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html"),
    "U6": ("Texas Instruments", "SN74LVC1G17DCKR", "C10425", "https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf"),
    "F1": ("BHFUSE", "BSMD1812-050-60V", "C883142", DATASHEETS["F1"]),
    "D1": ("MDD", "SS14", "C2480", LCSC_PDF + "2407101109_MDD-Microdiode-Semiconductor-SS14_C2480.pdf"),
    "D2": ("MDD", "SMBJ18CA", "C113991", DATASHEETS["D2"]),
    "D3": ("MDD", "B5819WS", "C64886", LCSC_PDF + "2407291648_MDD-Microdiode-Semiconductor-B5819WS_C64886.pdf"),
    "D5": ("Lite-On", "LTST-C190KGKT", "C125094", "https://optoelectronics.liteon.com/upload/download/DS22-2000-074/LTST-C190KGKT.PDF"),
    "Q1": ("Jiangsu Changjing", "2N7002", "C8545", LCSC_PDF + "2304140030_Jiangsu-Changjing-Electronics-Technology-Co---Ltd--2N7002_C8545.pdf"),
    "C1": ("Murata", "GRM32ER71H106KA12L", "C77102", LCSC_PDF + "2304140030_Murata-Electronics-GRM32ER71H106KA12L_C77102.pdf"),
    "C3": ("Samsung Electro-Mechanics", "CL21A106KAYNNNE", "C15850", LCSC_PDF + "2304140030_Samsung-Electro-Mechanics-CL21A106KAYNNNE_C15850.pdf"),
    "R1": ("UNI-ROYAL", "25121WJ0511T4E", "C36171", "https://www.lcsc.com/product-detail/C36171.html"),
}
for ref in ["C2", "C4", "C5", "C6", "C8"]:  # 100n 50V X7R
    PARTS_SOURCED[ref] = ("YAGEO", "CC0603KRX7R9BB104", "C14663", LCSC_PDF + "2211101700_YAGEO-CC0603KRX7R9BB104_C14663.pdf")
for refs, mpn, lcsc in [(["R3", "R7", "R10"], "0603WAF1002T5E", "C25804"),
                        (["R4"], "0603WAF1001T5E", "C21190"),
                        (["R6", "R11"], "0603WAF1003T5E", "C25803")]:
    for ref in refs:
        PARTS_SOURCED[ref] = ("UNI-ROYAL", mpn, lcsc, f"https://www.lcsc.com/product-detail/{lcsc}.html")
DATASHEETS["Q1"] = PARTS_SOURCED["Q1"][3]
DATASHEETS["U6"] = PARTS_SOURCED["U6"][3]

# (manufacturer, MPN, link) per populated ref; LCSC numbers for the JLCPCB BOM.
PROCUREMENT = {ref: (m, mpn, link) for ref, (m, mpn, _, link) in PARTS_SOURCED.items()}
LCSC = {ref: code for ref, (_, _, code, _) in PARTS_SOURCED.items() if code}
