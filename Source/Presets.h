#pragma once

#include <array>

namespace spectra
{

/** Factory presets. Freeze is deliberately absent: loading a preset never starts or stops a freeze. */
struct Preset
{
    const char* name;
    const char* description;
    double size, scan, drift, spread, diffuse, absorb, blur, shift, shimmer, tilt, width, attack, release, mix, level;
};

// name, description, then:          size   scan  drift spread diff  absorb blur  shift shim  tilt   width attack release mix   level
inline const std::array<Preset, 8> factoryPresets {{
    { "Init", "A neutral starting point.",                         400,  0.50, 0.20, 0.10, 0.30, 0.00, 0.00,   0, 0.00,  0.00, 0.60,  400, 1500, 0.70,  0 },
    { "Glass", "Bright, an octave up, with a touch of shimmer.",                        250,  0.60, 0.10, 0.05, 0.08, 0.00, 0.05,  12, 0.45,  0.35, 0.70,  600, 2500, 0.75, -3 },
    { "Choir Wash", "Long, airy and blurred, like voices in a hall.",                   900,  0.50, 0.35, 0.25, 0.85, 0.00, 0.45,   0, 0.15, -0.10, 0.90, 1500, 4000, 0.80,  0 },
    { "Sub Drone", "Dark and an octave down, with a slow fade.",                    600,  0.40, 0.15, 0.05, 0.20, 0.00, 0.20, -12, 0.00, -0.60, 0.40, 1200, 5000, 0.80,  0 },
    { "Frozen Bell", "A single instant, held still and tonal.",                   20,  0.50, 0.00, 0.00, 0.00, 0.00, 0.00,   0, 0.00,  0.10, 0.50,   30, 2000, 0.70,  0 },
    { "Grain Cloud", "A long slice scattered into a wide, grainy cloud.",                 1600,  0.50, 0.80, 0.70, 0.40, 0.00, 0.10,   0, 0.00,  0.00, 1.00,  300, 1500, 0.70,  0 },
    { "Shimmer Hall", "Soft octave shimmer with slow swells.",                 700,  0.50, 0.30, 0.15, 0.50, 0.00, 0.25,   0, 0.80,  0.25, 0.85, 2000, 6000, 0.75, -2 },
    { "Tape Memory", "Warm and dark, slowly soaking up what you play.",                 1200,  0.50, 0.40, 0.20, 0.35, 0.55, 0.15,   0, 0.00, -0.45, 0.60,  800, 3000, 0.70,  0 },
}};

} // namespace spectra
