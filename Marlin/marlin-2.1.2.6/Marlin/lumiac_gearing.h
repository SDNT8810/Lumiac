#pragma once
// Gear ratio = motor revolutions / output revolutions.
// Direct drive (1:1): INPUT_TURNS 1, OUTPUT_TURNS 1.
// Previous Lumiac setup (1:3): INPUT_TURNS 1, OUTPUT_TURNS 3.
// Change these two values together, then rebuild ALL firmware images so the
// controller and Octopus share the same calibration identity.
#ifndef LUMIAC_GEAR_INPUT_TURNS
  #define LUMIAC_GEAR_INPUT_TURNS 1
#endif
#ifndef LUMIAC_GEAR_OUTPUT_TURNS
  #define LUMIAC_GEAR_OUTPUT_TURNS 1
#endif
#if LUMIAC_GEAR_INPUT_TURNS <= 0 || LUMIAC_GEAR_OUTPUT_TURNS <= 0
  #error "Lumiac gear input and output turns must be positive integers."
#endif

// Derived from the existing 533.33 steps/mm at 16 microsteps with 1:3 gearing.
// This preserves that calibration; verify actual arm travel after changing gears.
#define LUMIAC_DIRECT_STEPS_AT_16 (533.33f * 3.0f)
#define SPIDER_AXIS_STEPS_PER_UNIT (LUMIAC_DIRECT_STEPS_AT_16 * (SPIDER_MICROSTEPS / 16.0f) * LUMIAC_GEAR_INPUT_TURNS / LUMIAC_GEAR_OUTPUT_TURNS)
