# Driver cooling, torque, and noise

## Requested 64-microstep / 3 A test build

The `marlin-max` target prepares the requested higher-current profile. It is separate from the normal build because drivers have already been measured at 95-110 C: do not operate this higher-current profile with the unresolved cooling condition. It also requires motors rated for the selected phase current. The commercial module revision and motor nameplate ratings have not been physically verified.

| Setting | Normal `marlin` | Test `marlin-max` |
| --- | --- | --- |
| Commanded microsteps | 16 | 64 |
| Steps/mm, all six axes | 533.33 | 2133.32 |
| Run current, all six axes | 2500 mA RMS | 3000 mA RMS |
| Homing current | 2500 mA RMS | 2500 mA RMS |
| Nominal holding current after a run | About 1250 mA RMS | About 1250 mA RMS |
| Driver mode | SpreadCycle | SpreadCycle |
| Interpolation | To 256 | To 256 |
| Thermal warnings/current reduction/error stop | Enabled | Enabled |

Holding current is quantized by the driver; the test profile uses a 0.416666667 hold multiplier to avoid increasing idle heating along with run current. That multiplier also scales holding current during a temporary homing-current setting. Motion coordinates, acceleration, SD trajectories, supply voltage, and dashboard speed control are unchanged. Steps/mm scales with the selected microsteps, so a doubled microstep setting does not halve physical travel.

BTT rates the TMC5160T Pro at **3.1 A RMS / 4.4 A peak**, and lists a **3 A maximum for the module base connection**. The test build therefore requests 3.0 A RMS, not 3.3 A. This is a hardware-based test ceiling, not a validated safe continuous current for the current assembly. See [BTT's specification](https://global.bttwiki.com/TMC5160T%20Pro%20V1.0.html).

Compared with 2.5 A, 3.0 A represents 20% more phase current and about **44% more resistive heating** at the same resistance. Torque will not necessarily rise proportionally near motor saturation. Reducing speed from the dashboard can improve torque margin but does not reduce the configured phase current; it cannot substitute for cooling.

64 microsteps is a smoothness comparison, not a torque multiplier. Because both profiles already interpolate to 256, the audible improvement may be small. Two commanded microsteps are also supported for experiments, but are not selected in this build; coarse stepping without interpolation tends to increase vibration. See [Analog Devices on microstepping](https://www.analog.com/en/resources/analog-dialogue/articles/mastering-precision-understanding-microstepping.html).

Build only, without copying to SD or flashing:

```powershell
python build.pt marlin-max --build-only
```

Output: `Marlin/marlin-2.1.2.6/.pio/build/STM32F446ZE_btt_max_power/firmware.bin`. This is **Octopus/Marlin firmware**, not ESP-12S firmware. The ESP does not need reflashing for this profile. After resolving cooling and checking motor compatibility, use the normal SD-card flashing procedure and re-home. The existing ESP startup homing/autoplay still applies.

`python build.py marlin-max` builds and copies the test binary to the configured SD path if present, using the same copy behavior as `python build.py`. To return to the normal 16-microstep / 2.5 A firmware, build `python build.py marlin --build-only` and flash its separate `STM32F446ZE_btt/firmware.bin`.

The build flags are in `Marlin/marlin-2.1.2.6/ini/lumiac.ini`. Compile-time guards reject configured run or homing current above 3000 mA; this is not a runtime clamp on manually entered M906 commands. No thermal protection is disabled, and program starts do not reapply high current over thermal current reductions.

## Follow-up: random motion at 95-110 C

After the ESP controller replacement, the user reports working controls but driver temperatures of 95-110 C during random motion, below 90 C during presets, and more noise in random mode. Whether forced airflow has since been added, the installed microstep setting, and the noise character still need confirmation. The ESP firmware replacement does not change Marlin's driver current or chopper mode.

Continuous random movement keeps run current active. After a preset stops, this TMC5160 initialization waits about two seconds before reducing toward the configured half-current hold setting. This explains a likely duty-cycle contribution to the temperature difference; it does not establish that either measured temperature is acceptable for the actual module. Reversals and different per-axis speeds are additional noise candidates.

For the existing web terminal, the following comparison needs no new firmware:

1. Stop using the UI **Stop** button, support the arms as needed, and let the drivers cool. Establish direct airflow over every driver before another loaded run. If airflow is already present, check heatsink contact and whether hot air is being trapped or recirculated. Do not deliberately run the assembly back to 95-110 C to collect data.
2. Read the installed settings and warning history with `M906`, `M569`, `M122`, and `M911`. Save the output. Do not clear thermal flags or override current that has been reduced by thermal protection.
3. Keep microsteps, current, speed, and the selected random program unchanged for the first noise comparison. While stopped, send:

   ```gcode
   M400
   M569 S1 X Y Z A B C
   G4 P1000
   M569
   ```

   These commands select StealthChop and report the mode; they do not start motion. Confirm all six axes report StealthChop, then use the same random program and speed for a short, supported comparison. Stop if force drops, movement stalls, or abnormal heating returns. The next random launch retains this chopper choice in the current Marlin source; rebooting the Octopus restores SpreadCycle.
4. To return to SpreadCycle, use UI **Stop**, wait for the mechanism to stop, then send:

   ```gcode
   M400
   M569 S0 X Y Z A B C
   M569
   ```

StealthChop targets electrical motor noise; it does not repair a loose gearbox, binding, or a stalled motor, and quieter sound does not prove adequate cooling. Its usable torque depends on the motor, load, and rate of acceleration. See [Analog Devices' comparison](https://www.analog.com/en/resources/app-notes/an-015.html).

If cooling alone is insufficient, a lower run current needs a separate supported test within the motor rating. For example, 2.0 A versus the source's 2.5 A reduces the resistive loss component by about 36%, with reduced available torque. A numeric current change is not included in the noise-comparison commands because the motor rating and any live thermal current reduction are still unknown. Do not decrease holding torque with unsupported arms. A continuous-duty current must be established by temperature and load testing, not by the PSU wattage or microstep count.

## Normal build configuration and reported hardware

The user reports a **24 V, 1000 W supply, no driver fan, and temperatures reaching 120 C** measured with an external instrument. Stop loaded operation, support the arms before removing power if they can drop, and let the drivers cool. Fit correctly mounted heatsinks and forced airflow across all six modules before loaded comparisons. A fan must run whenever the drivers are energized, including while holding still.

The normal `marlin` build configures the following; the separate test profile is listed above. Diagnostic commands below check what the installed firmware actually uses:

| Setting | Source default |
| --- | --- |
| Controller | BTT Octopus v1.1 (not Octopus Pro) |
| Drivers | Six TMC5160 in SPI mode; X Y Z A B C |
| Run / homing current | 2500 mA RMS on every axis |
| Hold current multiplier | 0.5 (nominally about 1250 mA RMS) |
| Current sense resistor | 0.075 ohm on every axis; verify against the actual module |
| Microsteps | 16, with interpolation to 256 |
| Chopper | SpreadCycle, 24 V timing; automatic hybrid switching disabled |
| Thermal monitoring | Enabled; persistent overtemperature warnings reduce current in 50 mA steps |
| Motion profile via M215 | Per-axis acceleration cap 50 mm/s2; travel acceleration 15 mm/s2 |
| Motion smoothing | S-curve enabled; junction deviation 0.003 mm |

The firmware driver type cannot distinguish a particular commercial "Pro" module. The motor ratings and module labels/revisions remain unconfirmed. BTT lists the **TMC5160T Pro** at 3.1 A RMS / 4.4 A peak, with a 0.075-ohm sense resistor. This is a module specification, not a safe continuous current for this uncooled assembly or an unidentified motor. Do not enter 4400 into M906: Marlin takes RMS milliamps. See [BTT's module specifications](https://neo.bttwiki.com/en/docs/accessories-docs/tmc-driver/tmc-5160-t-pro/).

## Where more usable torque can come from

Cooling is the first change. Marlin can reduce current when it receives persistent thermal warnings, so overheating may already be reducing available torque. Confirm this with logs rather than assuming it has happened. Do not disable thermal protection or repeatedly restore current while a warning remains active.

The TMC5160's internal prewarning is nominally 120 C, with thermal shutdown at a higher selectable temperature. An external heatsink/MOSFET measurement is not the IC junction temperature, and the external power transistors are not individually monitored by this sensor. Do not use the absence of an M911 warning as proof that the module is cool. See the [TMC5160/A datasheet, sections 3 and 11](https://www.analog.com/media/en/technical-documentation/data-sheets/TMC5160A_datasheet_rev1.18.pdf).

After cooling, verify the motor current rating (including peak/RMS conventions), module revision, sense resistor, connectors, and temperatures under sustained load before choosing a current ceiling. The normal build retains 2500 mA; it is **not validated as safe for the unknown motors**. The optional test build requests 3000 mA only when that firmware is installed. Building either profile does not flash the controller.

For perspective, increasing current from 2.5 A to 3.0 A would give at most roughly 20% extra torque before magnetic saturation, while resistive losses rise by about 44%: `(3.0 / 2.5)^2 = 1.44`. Reducing from 2.5 A to 2.0 A reduces the resistive component of heating by about 36%, but also reduces torque. These are comparisons, not instructions to apply either current without checking the ratings.

Reduce moving weight, friction, or binding; counterbalance gravity loads; or use more mechanical reduction if additional force is needed. These reduce required motor torque. With this fixed-current configuration, making the robot lighter alone does not automatically lower winding current or driver heating: it creates room to reduce the programmed current. Keep enough holding torque to prevent the arms dropping.

## Supply voltage

Keep this Octopus v1.1 installation at **24 V**. The board's supply support is 12/24 V; the higher voltage rating of a Pro driver does not upgrade the motherboard, capacitors, fans, or lamp circuit. See [BTT's Octopus specifications](https://global.bttwiki.com/Octopus.html).

A 1000 W supply is a capacity rating, not the power forced into the motors. Supply current and motor phase current are different. Raising supply voltage can improve torque at higher motor speed, but a current-regulated driver still regulates winding current to its setting. It does not halve winding current or its I-squared-R heating; some driver losses can increase. Voltage is not the remedy for this overheating. See [STEPPERONLINE's drive-voltage explanation](https://help.omc-stepperonline.com/hc/s/articles/the-effect-of-voltage-on-the-performance-of-stepper-motors).

## Why random motion can sound different

There is no separate random-mode current, microstep, or chopper setting in this source. POS1/POS2 and random launches all apply the same M215 acceleration profile.

The active loop in every current `gcodes/input*.txt` file has 36 arc-length-spaced points. The arms follow related but non-identical smooth waves, using different secondary curves to create an organic motion. Arc-length spacing keeps the combined six-axis path speed steady while preserving the less symmetrical character of the original random movement. Each pose file has one straight coordinated move and then stops. Continuous random operation also keeps run current active instead of settling to reduced hold current.

There is also a speed difference in the ESP32 path:

- The UI defaults to 200 with a maximum of 400. For an SD job, the bridge sends `G1 F400` and `M220 S50` at that default.
- Pose files contain no F command, so their requested coordinated feed is effectively 200 mm/min.
- Every random file explicitly sets `F200`, so its requested coordinated feed becomes 100 mm/min at the same UI setting.

These are path feedrates, not the speed of every arm; acceleration and segment geometry also affect each axis. The slower random speed may sit in a resonance band. This is a candidate explanation, not a measured diagnosis. Compare the same motion at the same actual feedrate before blaming microsteps. The software change leaves existing program speeds and trajectories intact.

## Quiet random-motion G-code

All seven current random files (`gcodes/input*.txt`) now begin with `M204 P8 T8`. It overrides the launcher's 15 mm/s² acceleration for random movement only, reducing acceleration to 8 mm/s². The active portion of each file is the same 36-point organic dance, using a fixed `F200` feedrate; all points are arc-length spaced. `@LOOP` returns to its first point, so earlier trajectory variants below that directive are retained in the source file but do not execute. Copy the updated `gcodes` directory to the Octopus SD card; no firmware rebuild is needed for this G-code-only change.

## Microstep comparisons

**Start with 16 microsteps, interpolation on, and SpreadCycle.** This is already the configured baseline for loaded motion. Try 8 next and 32 only as a comparison. Lower subdivision increases the torque associated with a single small commanded increment; it does not multiply the motor's available running torque. Full/half stepping can introduce more vibration and noise. Microstepping helps smoothness even when positioning precision is unimportant. See [Analog Devices on microstepping and running torque](https://www.analog.com/en/resources/analog-dialogue/articles/mastering-precision-understanding-microstepping.html).

All supported microstep settings retain 256-step interpolation, so audible differences may be small. There is no guaranteed microstep setting that simultaneously maximizes torque and minimizes noise.

A 0.5 mm position tolerance does not make stalling acceptable: this system has no position feedback to correct missed steps, so their error can accumulate far beyond that tolerance.

For a microstep-only comparison in the normal build, set **only `SPIDER_MICROSTEPS`** in `Marlin/marlin-2.1.2.6/Marlin/Configuration.h`, then rebuild. It sets all six TMC microstep values and scales all six steps/mm together. The `marlin-max` environment overrides this macro to 64 via its build flags:

| SPIDER_MICROSTEPS | Steps/mm | Purpose |
| --- | --- | --- |
| 16 | 533.33 | Existing baseline and recommended starting point |
| 2 | 66.66625 | Coarse-command experiment; no promised torque gain or noise reduction |
| 8 | 266.665 | Lower pulse-rate comparison; no promised torque gain |
| 32 | 1066.66 | Finer-command comparison; no promised noise reduction |
| 64 | 2133.32 | Selected test profile; finest practical external command setting |

The calibration comes from the existing 533.33 steps/mm value, not a new measurement of the gearing. The same G-code coordinates retain the same intended physical travel. Do not halve microsteps without halving steps/mm: that would double travel, speed, and acceleration in physical units. Do not change the generic `MICROSTEP_MODES` array or use `M350` for these SPI TMC drivers; this tree's M350 operates hardware MS pins, not the TMC SPI microstep registers.

Build from the repository root without copying anything to an SD card:

```powershell
python -m platformio run -d Marlin/marlin-2.1.2.6 -e STM32F446ZE_btt
```

The root `build.py` also copies firmware to a detected SD card, so use the command above for build-only checks. After cooling and rating checks, use the normal README flash procedure for the chosen profile and re-home before motion. EEPROM settings are disabled in this source: do not depend on M500 to save runtime experiments. Check M503 and M122 after each flash; old/manual M92 values would invalidate the comparison.

## Controlled bench checks after cooling and rating verification

1. Support the mechanism, ensure every driver has airflow, and establish a current within the confirmed motor/module ratings. Do not reduce holding current with a suspended load unsupported.
2. Prevent the ESP32 startup automation from homing and starting random motion during setup. For a direct Octopus USB bench test, disconnect the ESP32 control connection with power off. Merely closing the web page does not stop the automation.
3. Read and save the baseline with the commands below. All six drivers must communicate correctly. Investigate `All HIGH` / `All LOW`, thermal warnings, or shorts before moving.
4. Once the rig is ready for motion, home it, compare the same short motion and the same feedrate in each profile, and then run one pass of the same random file. Direct `M23 /gcodes/input.txt` followed by `M24` starts an ordinary one-pass SD job; `M215 S1` starts an indefinite loop. For that direct SD comparison, first set `M201 X50 Y50 Z50 A50 B50 C50`, `M204 P15 T15`, and a fixed `M220` percentage: M23/M24 do not apply the M215 motion profile. Re-home after any stall or skipped movement.
5. Record current, microsteps, chopper mode, speed override, temperature at the same location, noise, and missed movement. Stop for renewed abnormal heating, thermal warnings, loss of torque, or rough/stalled motion. A short successful move does not establish a safe continuous temperature; check the intended duty cycle after short trials pass.

Read-only diagnostics:

```gcode
M115
M503
M906
M569
M122
M911
```

M122 reports configuration and driver status flags; it is not a calibrated external-module thermometer. Capture M911 before clearing any warning history. Look for serial messages such as `current decreased to ...`, which would support thermal current reduction as a cause of weak motion.

After the rig is cool and stationary, compare chopper modes separately from the microstep comparison. With SD streaming stopped/paused and no competing commands from the ESP32, wait for queued motion and select SpreadCycle on all six axes:

```gcode
M400
M569 S0 X Y Z A B C
M569
```

For a supported, light-load quietness comparison, StealthChop is available at runtime even though SpreadCycle is the default. Start from standstill and allow initial regulation before moving:

```gcode
M400
M569 S1 X Y Z A B C
G4 P1000
M569
```

StealthChop is designed for quietness; confirm its load margin with the actual motor. For forceful movement and changing loads, retain SpreadCycle unless testing establishes adequate performance in StealthChop. Switch back with the S0 sequence while stationary. M400 alone does not stop a streaming SD loop.

For a gentler acceleration experiment, `M204 P8 T8` lowers travel acceleration, but apply it **after** launching/pausing an M215 job because M215 resets it to 15 on each new launch. Hold feedrate, current, and microsteps constant during this comparison. Slower acceleration reduces inertial torque demand; it does not remove gravity load. Reducing speed is not guaranteed to quiet a resonance.
