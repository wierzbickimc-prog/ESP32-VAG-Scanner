# EuroVan health checks

Both the macOS app and ESP32-P4 expose **EuroVan health checks** without requiring
a discovery scan. Select a procedure, press **Start**, then confirm the physical
prerequisites for that run. **Stop** or leaving the screen releases the cable.
The selected controller's identification is checked before reading test groups.

This release targets the owner's 2002 EuroVan: engine **022906032AF**, AXK 24V
VR6, and transmission **01P927733CG**. These are exact controller tokens, not
chassis-prefix/photo matches. Other revisions, engines, and transmissions remain
unsupported until their mappings and procedures are checked. Mk3 Golf work is
outside this feature's scope.

## Procedures and evidence

### 01P ATF level assistant

Source: VW factory repair information, mirrored under the 2002 AXK EuroVan:

- [Prerequisites, page 3087](https://workshop-manuals.com/volkswagen/eurovan_camper/v6-2.8l_%28axk%29/transmission_and_drivetrain/automatic_transmission/transaxle/fluid_a/t/component_information/testing_and_inspection/4_speed_01p_transmission/page_3087/).
- [Temperature measurement, page 3088](https://workshop-manuals.com/volkswagen/eurovan_camper/v6-2.8l_%28axk%29/transmission_and_drivetrain/automatic_transmission/transaxle/fluid_a/t/component_information/testing_and_inspection/4_speed_01p_transmission/page_3088/).
- [Overflow inspection, page 3089](https://workshop-manuals.com/volkswagen/eurovan_camper/v6-2.8l_%28axk%29/transmission_and_drivetrain/automatic_transmission/transaxle/fluid_a/t/component_information/testing_and_inspection/4_speed_01p_transmission/page_3089/).

| Requirement | How the app establishes it |
|---|---|
| Initially approximately 30°C or below | Must observe ATF ≤30°C during the run |
| Inspection temperature 35–45°C | Live 02 / group 005 / field 1, inclusive endpoints |
| Selector P | Live 02 / 001 / 1 |
| Supply ≥11.5 V | Live 02 / 002 / 3 |
| Level vehicle, parking brake applied | User confirmation |
| Engine running at idle | User confirmation; no unverified transmission RPM field used |
| Consumers off, fuses and grounds correct | User confirmation |
| No emergency-running mode | User confirmation; all stored transmission DTCs conservatively block readiness |

The three transmission fields above were already checked on the real van (see
HANDOVER.md, 2026-10-31). The combined assistant still needs an in-vehicle trial.
The app does not infer emergency mode merely from the absence of DTCs.

Above 45°C, readiness is revoked and the assistant requires cooling to ≤30°C
again. Starting at 40°C does not skip the initial-temperature condition. Readiness
is a signal to perform the physical overflow inspection, not a fluid-level
measurement. The initial fluid inside the overflow tube is insufficient evidence;
observe subsequent flow. The procedure specifies replacing the level-plug seal
and tightening to 15 Nm.

During live readiness, users may record continued dripping or no subsequent
flow. These are explicitly human observations. They do not become automatic
scanner diagnoses.

### AXK camshaft timing / possible chain wear

Source: [VW AXK camshaft adjustment, checking](https://charm.li/Volkswagen/2002/Eurovan%20GLS%20V6-2.8L%20%28AXK%29/Repair%20and%20Diagnosis/Engine%2C%20Cooling%20and%20Exhaust/Engine/Camshaft%2C%20Lifters%20and%20Push%20Rods/Camshaft/Testing%20and%20Inspection/Camshaft%20Adjustment%2C%20Checking/).
The table is embedded as images; the intake specification is in the table
labelled “Display Group Number 208 (Part 2 of 4)”. The equivalent workshop-manuals
mirror places this material around pages 1117–1118.

| Requirement / value | Implementation |
|---|---|
| Correct oil level | Per-run user confirmation |
| Oil ≥80°C; radiator fan has cycled | Per-run user confirmation; coolant is never treated as measured oil temperature |
| Engine stationary and idling | User confirmation plus RPM reads |
| 650–1400 rpm | Group 004/1 and companion RPM fields 208/1 and 209/1, inclusive |
| Intake phase flank 118–134 | 208/2, inclusive |
| Intake adapted offset −8° to +8° crankshaft | 208/3, inclusive |
| Intake status XX1XX100 | 208/4; require `(status & 0x27) == 0x24` |
| DTC memory checked / faults diagnosed | Fresh fault read each cycle; stored engine faults prevent a within-limits conclusion |

Group 209/3 is displayed alongside 208/3, with the absolute difference. No AXK
factory rejection limit for that difference or the exhaust offset was verified
in the retrieved table. In particular, the often-repeated 3° difference rule is
not implemented as a factory pass/fail criterion. Limits from VW bulletin
15-04-01 for BDF/BJS/BAA are not silently applied to AXK.

“Within documented limits” refers only to the checked intake table and valid
sampling conditions. It does not certify the chain, guides, tensioners or VVT
system. An out-of-range value directs further timing/adjuster/oil-pressure
diagnosis and does not assert chain elongation as the cause.

This is passive screening. The full factory workflow also performs output
diagnostic tests before checking camshaft function and may require mechanical
inspection or oil-pressure measurement. Those operations are not performed or
claimed by this feature. No fault clearing, adaptation or Basic Settings command
is sent.

### AXK misfire activity

Source: [VW bulletin 01-03-02, AXK counter monitoring](https://workshop-manuals.com/volkswagen/eurovan_mv/v6-2.8l_%28axk%29/powertrain_management/ignition_system/ignition_coil/component_information/technical_service_bulletins/all_other_service_bulletins_for_ignition_coil/0103-02/feb/03/ignition_system_mil_on/misfire_dtcs_set/rough_running/page_2842/).
The bulletin identifies group 016 for AXK cylinders 4–6; the preceding group
contains cylinders 1–3. Monitor with the engine running, including the warm-up
period, and confirm the vehicle is parked with its parking brake applied.

Successive counter increases produce **Attention needed**. Decreases/wraps
establish a new baseline rather than becoming an enormous positive delta.
Group 014/4 recognition status is displayed raw because its encoding has not
been verified on this ECU. No increases therefore remain **Inconclusive**;
neither zero counters nor old nonzero counters prove current health. Engine
faults are displayed as context and are not cleared. No warm-engine prerequisite
is imposed that would hide a cold-start misfire.

### AXK ECU supply

Source: [VW AXK voltage supply for control module, checking](https://charm.li/Volkswagen/2002/Eurovan%20GLS%20V6-2.8L%20%28AXK%29/Repair%20and%20Diagnosis/Powertrain%20Management/Computers%20and%20Control%20Systems/Engine%20Control%20Module/Testing%20and%20Inspection/).
Read 01 / 004 / 2 with the engine idling, consumers and A/C off, and fuses and
grounds checked. The documented minimum is 11.5 V. This reports minimum ECU
supply adequacy and cannot diagnose battery capacity or all alternator faults.
Other engine DTCs do not suppress an observed low-voltage result.

## Sampling rules (app policy, not additional VW specifications)

- Timing and supply require ≥10 seconds and ≥3 distinct complete frames before
  a within-limits result. Re-rendering or re-evaluating a frame is not a new
  sample. Timing variation >1° restarts the sampling window as inconclusive.
  Supply sampling also requires 650–1400 rpm, using the AXK camshaft table's
  operating band as a conservative app gate rather than a new supply-voltage
  specification.
- A complete frame contains a fresh DTC read and the required measuring groups
  from one controller session. No dashboard switching occurs during a check.
- Reject frame collection spanning >12 seconds, data older than 8 seconds,
  non-finite values/times, missing groups/fields, incorrect units, unknown
  controller identities, unsupported protocols and incompatible layouts.
- Invalid data, prerequisite changes and gaps >8 seconds break the observation
  window. ATF readiness and physical-result recording require live data.
- Initial controller/group failures terminate the stream and release the cable;
  retry is explicit. Cancellation is polled between group reads, not just at the
  end of an entire test.

## Storage and demo

On the Mac, **Save snapshot** retains up to 30 reports in local UserDefaults,
including sample time, controller identity, simulation flag, readings, confirmed
conditions, interpretation, procedure source and any physical observation.
On the P4, one snapshot per procedure is saved in NVS, including identity,
readings, conditions and simulation flag. It has no dependable wall clock, so no
calendar timestamp is fabricated. The procedure source is available via QR.
Saved observations are historical evidence; they are not current readiness.

The EuroVan health demo simulates a parked vehicle, warm engine, initially cool
ATF warming through the inspection window, valid cam adaptation and clean fault
memory. This specialized scenario is clearly labelled. It does not erase or
change the driving demo's stored fault fixtures. Normal demo behavior resumes
when the health session ends.

## Additional candidates

| Candidate | Remaining evidence before enabling automated conclusions |
|---|---|
| Fuel trims / mixture | AXK-specific group/bank layout, adaptation validity after reset and factory limits. [Ross-Tech's general explanation](https://www.ross-tech.com/vag-com/cars/fuel-trim.html) is useful context but its approximate ±10% is not an AXK specification. |
| Oxygen-sensor response/heaters, catalyst, secondary air, EVAP | Exact AXK sensor type, groups, status encodings, prerequisite temperatures/RPM and whether each procedure requires Basic Settings or actuation. |
| Cold temperature plausibility / warm-up | Which temperatures are genuinely independent, cold-soak requirements, AXK thermostat behavior, ambient/load criteria and supported sensors. |
| ABS wheel-speed consistency | Real controller identity, verified field mapping, sampling-rate adequacy and factory speed/tolerance conditions. Existing labels explicitly describe demos. |
| VVT response / transmission slip | Verified requested/actual fields and operating states; full procedures may require driving or active tests. |

These candidates are disclosed as pending in both apps rather than supplied
with guessed thresholds.

## Validation

`make -C kwcore test` runs protocol regressions and dedicated health tests for
boundary values, temperature-window transitions, prerequisite revocation,
adaptation status, stale/missing/wrong-unit data, repeated frames, counter resets,
1200-baud header groups, cancellation, identity gating and read-only behavior.
macOS integration tests exercise live health sessions, saving physical evidence,
stop/restart and returning the bus to ordinary module reads. Firmware compilation
and the existing startup regression cover integration and idle startup.

New health groups 014–016 and 208–209 still require capture/comparison on the
owner's real EuroVan before treating this as in-vehicle validated. Test source
accuracy and simulated integration do not establish that physical validation.
