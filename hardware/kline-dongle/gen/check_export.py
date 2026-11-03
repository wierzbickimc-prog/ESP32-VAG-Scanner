"""Validate the actual KiCad files and build Seeed Fusion and JLCPCB PCBA upload artifacts.

Run with normal Python 3; pcbnew is not required. No files are submitted online.
"""
import csv
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from parts import HERE, PROCUREMENT, LCSC, PARTS
from place import BOARD_H

CLI = os.environ.get("KICAD_CLI") or shutil.which("kicad-cli")
if not CLI:
    candidate = Path("/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli")
    if candidate.is_file():
        CLI = str(candidate)
if not CLI:
    raise SystemExit("Set KICAD_CLI to the KiCad 10 kicad-cli executable")
BUILD = HERE / "build"
BUILD.mkdir(exist_ok=True)
SCH = HERE / "kline-dongle.kicad_sch"
PCB = HERE / "kline-dongle.kicad_pcb"


def run(*args):
    result = subprocess.run([CLI, *map(str, args)], cwd=HERE)
    if result.returncode:
        raise SystemExit(f"KiCad {' '.join(map(str, args[:2]))} failed (exit {result.returncode}); inspect build/ reports. No new package exported.")


# JLCPCB's part models are not all oriented like KiCad's. Corrections by footprint name,
# confirmed in JLCPCB's placement preview on 2026-11-01 (SOT-23 here also covers SOT-23-5;
# JLCKicadTools' older -90 for it was a quarter-turn off). Still check the preview.
JLC_ROTATION = [(r"^SOT-23", 180), (r"^SOT-353", 180), (r"^SOIC-", 270)]
# Hand-soldered after delivery: not stocked by JLCPCB, or through-hole.
JLC_HAND = {"U1", "U4"}


def jlc_rotation(footprint, rot):
    name = footprint.split(":")[-1]
    extra = next((r for pattern, r in JLC_ROTATION if re.match(pattern, name)), 0)
    return (float(rot) + extra) % 360


def read_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


# Check the delivered board, including the limitations recorded in the JSON reports.
for command, source, report, extra in [
    (("sch", "erc"), SCH, "erc.json", []),
    (("pcb", "drc"), PCB, "drc.json", ["--schematic-parity", "--refill-zones"]),
]:
    run(*command, "--severity-all", "--format", "json", "--exit-code-violations",
        *extra, "-o", BUILD / report, source)
    result = json.loads((BUILD / report).read_text())
    problems = result.get("violations", []) + result.get("unconnected_items", []) + result.get("schematic_parity", [])
    problems += [v for sheet in result.get("sheets", []) for v in sheet.get("violations", [])]
    if problems:
        raise SystemExit(f"{report} contains violations; no new manufacturing package exported")

# Stage every output before replacing the previous generated package.
with tempfile.TemporaryDirectory(prefix="kline-export-", dir=BUILD) as temporary:
    stage = Path(temporary)
    gerbers = stage / "gerbers"
    gerbers.mkdir()
    run("pcb", "export", "gerbers", "--layers",
        "F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,F.Paste,Edge.Cuts",
        "--no-x2", "--subtract-soldermask", "--check-zones", "-o", str(gerbers) + os.sep, PCB)
    # Use Seeed's documented outline and drill extensions.
    (gerbers / "kline-dongle-Edge_Cuts.gm1").rename(gerbers / "kline-dongle-outline.gko")
    run("pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
        "--excellon-separate-th", "--generate-map", "--map-format", "pdf",
        "-o", str(gerbers) + os.sep, PCB)
    for path in gerbers.glob("*.drl"):
        path.rename(path.with_suffix(".txt"))
    run("sch", "export", "pdf", "-o", stage / "schematic.pdf", SCH)
    for side in ("F", "B"):
        run("pcb", "export", "svg", "--layers", f"{side}.Fab,{side}.Silkscreen,Edge.Cuts",
            "--mode-single", "--fit-page-to-board", "--exclude-drawing-sheet",
            *(["--mirror"] if side == "B" else []), "-o", stage / f"assembly-{side}.svg", PCB)
        run("pcb", "export", "pdf", "--layers", f"{side}.Fab,Edge.Cuts", "--scale", "3",
            "--mode-single", "--black-and-white", "--crossout-DNP-footprints-on-fab-layers",
            *(["--mirror"] if side == "B" else []), "-o", stage / f"assembly-{side}.pdf", PCB)
    run("pcb", "export", "pdf", "--layers", "F.Fab,Edge.Cuts", "--scale", "3",
        "--mode-single", "--black-and-white", "--sketch-pads-on-fab-layers",
        "--crossout-DNP-footprints-on-fab-layers", "-o", stage / "pinmap-F.pdf", PCB)

    # Export the actual schematic fields using Seeed's downloaded BOM template headers.
    run("sch", "export", "bom", "--fields", "Reference,MPN,QUANTITY,Part Link",
        "--labels", "Designator,Manufacturer Part Number or Seeed SKU,Qty,Link",
        "--group-by", "MPN,Manufacturer,Part Link", "--exclude-dnp", "--ref-range-delimiter", "",
        "-o", stage / "seeed-bom.csv", SCH)
    run("sch", "export", "bom", "--fields", "Reference,Value,Manufacturer,MPN,Footprint,Description,Part Link",
        "--labels", "Reference,Value,Manufacturer,MPN,Footprint,Notes,Part Link", "--exclude-dnp",
        "-o", stage / "bom-detail.csv", SCH)
    expected = set(PROCUREMENT)
    bom_refs = []
    for row in read_csv(stage / "seeed-bom.csv"):
        refs = row["Designator"].split(",")
        if int(row["Qty"]) != len(refs):
            raise SystemExit(f"BOM quantity mismatch: {row}")
        for ref in refs:
            if ref not in PROCUREMENT or row["Manufacturer Part Number or Seeed SKU"] != PROCUREMENT[ref][1]:
                raise SystemExit(f"Missing / stale purchasing metadata for {ref}; regenerate and review")
        bom_refs.extend(refs)
    if set(bom_refs) != expected or len(bom_refs) != len(expected):
        raise SystemExit("BOM references do not match all populated components")

    # Keep native coordinates as a reference, and convert SMT positions to lower-left mm.
    for name, extra in [("positions-kicad.csv", ["--smd-only"]), ("placements-all-kicad.csv", [])]:
        run("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--exclude-dnp",
            *extra, "-o", stage / name, PCB)
    all_positions = read_csv(stage / "placements-all-kicad.csv")
    if {p["Ref"] for p in all_positions} != expected:
        raise SystemExit("Placement/BOM reference mismatch; J1 and TP pads must be excluded")
    smt = read_csv(stage / "positions-kicad.csv")
    if {p["Ref"] for p in smt} != expected - {"U1"}:
        raise SystemExit("SMT placement set differs from expected populated SMT components")
    with (stage / "seeed-cpl.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["Designator", "Mid X", "Mid Y", "Rotation", "Layer"])
        for p in smt:
            if p["Side"] != "top":
                raise SystemExit("Review coordinate convention before adding bottom-side components")
            writer.writerow([p["Ref"], f'{float(p["PosX"]):.6f}',
                             f'{BOARD_H + float(p["PosY"]):.6f}', p["Rot"], "Top"])

    # JLCPCB: BOM keyed by LCSC number; CPL with JLC's headers and rotation corrections.
    footprints = {ref: fp for ref, _, _, fp, *_ in PARTS}
    values = {ref: val for ref, _, val, *_ in PARTS}
    jlc_refs = expected - JLC_HAND
    if set(LCSC) != jlc_refs:
        raise SystemExit(f"LCSC numbers missing or extra: {sorted(set(LCSC) ^ jlc_refs)}")
    groups = {}
    for ref in sorted(jlc_refs, key=lambda r: (re.sub(r"\d", "", r), int(re.sub(r"\D", "", r)))):
        groups.setdefault((values[ref], footprints[ref].split(":")[-1], LCSC[ref]), []).append(ref)
    with (stage / "jlc-bom.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["Comment", "Designator", "Footprint", "JLCPCB Part #"])
        for (value, footprint, lcsc), refs in groups.items():
            writer.writerow([value, ",".join(refs), footprint, lcsc])
    with (stage / "jlc-cpl.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for p in smt:
            if p["Ref"] in JLC_HAND:
                continue
            # Same origin as the Gerbers (board spans y -24..0), so JLCPCB's overlay lines up.
            writer.writerow([p["Ref"], f'{float(p["PosX"]):.4f}mm', f'{float(p["PosY"]):.4f}mm',
                             "Top", f'{jlc_rotation(footprints[p["Ref"]], p["Rot"]):g}'])
    with (stage / "accessories-to-quote.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["Item", "Manufacturer", "MPN", "Qty per PCBA", "Instruction", "Link"])
        writer.writerow(["External antenna", "Seeed Studio", "Included with U4 / SKU 113991054", 1,
                         "Attach to U4 after soldering; do not purchase twice; quote inclusion explicitly", PROCUREMENT["U4"][2]])

    for name in ("SEEED-ASSEMBLY.md", "JLC-ASSEMBLY.md", "FACTORY-NOTES.md", "README.md"):
        shutil.copyfile(HERE / name, stage / name)
    for report in ("erc.json", "drc.json"):
        shutil.copyfile(BUILD / report, stage / report)
    inputs = [SCH, PCB, HERE / "kline-dongle.kicad_pro", HERE / "dongle.kicad_sym",
              HERE / "sym-lib-table", HERE / "fp-lib-table", HERE / "README.md",
              HERE / "SEEED-ASSEMBLY.md", HERE / "JLC-ASSEMBLY.md", HERE / "FACTORY-NOTES.md"]
    inputs += sorted((HERE / "gen").glob("*.py"))
    inputs += sorted((HERE / "dongle.pretty").glob("*.kicad_mod"))
    manifest = {
        "status": "UNBUILT PROTOTYPE - bench, enclosure, thermal and automotive surge validation pending",
        "kicad_version": subprocess.check_output([CLI, "version"], text=True).strip(),
        "input_sha256": {str(p.relative_to(HERE)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        "checks": {"erc": 0, "drc": 0, "unconnected": 0, "schematic_parity": 0,
                   "bom_components": len(expected), "smt_placements": len(smt), "through_hole_placements": 1},
        "assembly": "Seeed sources and assembles all components, attaches antenna; customer handles J1 OBD wiring/enclosure",
        "usb": "USB and car power may be connected together; D3 keeps USB VBUS out of the converter",
        "firmware": "No application programming image supplied; C3 firmware pending",
        "cpl": "mm, lower-left board origin, +X right/+Y up, KiCad CCW rotation; Top only. U1 is THT, not SMT CPL.",
    }
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # The Gerber upload contains fabrication layers only; support files go in their own ZIP.
    with zipfile.ZipFile(stage / "seeed-gerbers.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(gerbers.iterdir()):
            if p.suffix in (".gtl", ".gbl", ".gts", ".gbs", ".gto", ".gbo", ".gtp", ".gko", ".txt"):
                archive.write(p, p.name)
    shutil.copyfile(stage / "seeed-gerbers.zip", stage / "jlc-gerbers.zip")  # JLCPCB reads the same set
    with zipfile.ZipFile(stage / "seeed-assembly-support.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(stage.iterdir()):
            if p.is_file() and p.suffix != ".zip" and not p.name.startswith("jlc-"):
                archive.write(p, p.name)
        for p in sorted(gerbers.glob("*.pdf")):
            archive.write(p, p.name)
    destination = BUILD / "fabrication"
    if destination.exists():
        shutil.rmtree(destination)  # exclusively generated by this script
    shutil.move(str(stage), destination)
print(f"Checks passed; Seeed and JLCPCB upload packages: {BUILD / 'fabrication'}")
