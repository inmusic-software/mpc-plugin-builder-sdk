#pragma once

struct KenzflxPreset
{
    const char* name;
    float drive;
    float warmth;
    float tone;
    float chorus;
    float delay;
    float reverb;
    float crush;
    float mix;
    float reverse;
};

inline constexpr KenzflxPreset kKenzflxPresets[] = {
    {"Clean Flex",  0.25f, 0.35f, 0.62f, 0.10f, 0.12f, 0.12f, 0.00f, 0.65f, 0.00f},
    {"Warm Tape",   0.42f, 0.72f, 0.42f, 0.08f, 0.28f, 0.20f, 0.04f, 0.72f, 0.00f},
    {"Wide Motion", 0.30f, 0.48f, 0.56f, 0.68f, 0.20f, 0.28f, 0.02f, 0.70f, 0.00f},
    {"Echo Cloud",  0.28f, 0.62f, 0.46f, 0.20f, 0.62f, 0.72f, 0.03f, 0.78f, 0.00f},
    {"Dusty Crush", 0.68f, 0.64f, 0.38f, 0.12f, 0.24f, 0.18f, 0.68f, 0.75f, 0.00f},
    {"Half Time",   0.34f, 0.52f, 0.52f, 0.16f, 0.18f, 0.24f, 0.08f, 0.78f, 0.25f},
    {"Flex Beat",   0.46f, 0.54f, 0.50f, 0.24f, 0.32f, 0.30f, 0.14f, 0.82f, 0.58f},
    {"Reverse FX",  0.38f, 0.60f, 0.48f, 0.28f, 0.36f, 0.46f, 0.10f, 0.82f, 0.92f},
};

inline constexpr int kNumPresets =
    static_cast<int>(sizeof(kKenzflxPresets) / sizeof(kKenzflxPresets[0]));
