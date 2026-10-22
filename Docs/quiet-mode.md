# Noise after the 1:1 conversion

The 1:1 commit (`77c24c5`) changes the six-axis distance calibration from
1066.66 to approximately 3199.98 steps/mm. It inherits the previous calibration's
meaning of a 1:3 ratio (one motor turn for three output turns); it is not a new
measurement of the modified mechanism. Check a short, measured arm movement
before assuming this ratio matches the physical gears.

| Setting | Before the 1:1 commit | Current 1:1 build |
| --- | --- | --- |
| Commanded microsteps | 32 | 32 |
| Interpolation | 256 | 256 |
| Run / home current | 3000 / 2500 mA RMS | 3000 / 2500 mA RMS |
| Nominal hold current after a run | About 1250 mA RMS | About 1250 mA RMS |
| Driver mode at boot | SpreadCycle | SpreadCycle |
| Steps/mm | 1066.66 | 3199.98 |
| Random file feed / acceleration | F338 / M204 P8 T8 | F338 / M204 P8 T8 |
| Octopus speed and acceleration factor | 70% | 70% |

For the same axis coordinate speed, the new calibration requests three times
the motor steps and motor rotation. That changes the motor's operating point
and can move it into an audible resonance. The changed mechanical drive can
also change how vibration is transmitted. Neither explanation is confirmed
without a measured travel check and a controlled comparison on the mechanism.

The 1:1 change did not disable a previously enabled silent mode. However, a
quiet mode previously selected with M569 is lost when Octopus restarts: EEPROM
settings are disabled and the boot default is SpreadCycle. ESP replacement
alone does not change Octopus driver mode. There is no separate random-mode
driver current or chopper setting; random motion runs continuously and changes
direction more often than a single pose.

## Reversible quiet comparison, no firmware flash needed

Buzzing fits electrical driver noise or resonance. Jerking can indicate missed
steps, a binding mechanism, an incorrect travel calibration, or pauses in command
delivery. Do not use lower current or the absence of noise as proof that these
problems are resolved. Keep the arms supported during the first comparison.

First select **Stop** in the working ESP12 dashboard and wait for motion to stop.
Save the terminal output of `M906`, `M569`, `M122`, and `M911`. Then send each of
these lines through the dashboard terminal:

```gcode
M400
M569 S1 X Y Z A B C
G4 P1000
M569
```

All six drivers should report `stealthChop`. This changes the driver mode only;
it does not alter calibration, microsteps, current, or the hold multiplier.
First compare one short, supported movement at a reduced dashboard speed. Only
then compare a short part of the same random program. Stop the comparison if
jerking, loss of position, or inadequate holding appears. Re-home after a loss
of position before resuming operation.

To restore the existing mode, select Stop, wait for motion to stop, then send:

```gcode
M400
M569 S0 X Y Z A B C
M569
```

Restarting Octopus also restores SpreadCycle. A permanent silent default should
follow a successful loaded comparison. StealthChop targets low-speed electrical
noise; adequate torque and tracking during acceleration must be verified on this
mechanism. See [Analog Devices' comparison of the modes](https://www.analog.com/en/resources/app-notes/an-015.html)
and [Marlin's M569 reference](https://marlinfw.org/docs/gcode/M569.html).
