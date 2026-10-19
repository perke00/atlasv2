#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace atlas
{
using NoteMask = std::array<bool, 12>;

struct ScaleDef { const char* name; std::vector<int> degrees; };

inline const std::vector<ScaleDef>& scaleDefs()
{
    static const std::vector<ScaleDef> defs = {
        { "Chromatic",        { 0,1,2,3,4,5,6,7,8,9,10,11 } },
        { "Major",            { 0,2,4,5,7,9,11 } },
        { "Minor",            { 0,2,3,5,7,8,10 } },
        { "Harmonic Minor",   { 0,2,3,5,7,8,11 } },
        { "Melodic Minor",    { 0,2,3,5,7,9,11 } },
        { "Dorian",           { 0,2,3,5,7,9,10 } },
        { "Phrygian",         { 0,1,3,5,7,8,10 } },
        { "Lydian",           { 0,2,4,6,7,9,11 } },
        { "Mixolydian",       { 0,2,4,5,7,9,10 } },
        { "Major Pentatonic", { 0,2,4,7,9 } },
        { "Minor Pentatonic", { 0,3,5,7,10 } },
        { "Blues",            { 0,3,5,6,7,10 } },
    };
    return defs;
}

inline NoteMask makeMask (int scaleIndex, int root)
{
    NoteMask m{};
    const auto& defs = scaleDefs();
    const auto& d = defs[(size_t) std::max (0, std::min ((int) defs.size() - 1, scaleIndex))].degrees;
    for (int deg : d) m[(size_t) (((deg + root) % 12 + 12) % 12)] = true;
    return m;
}

inline int pitchClass (int note) { return ((note % 12) + 12) % 12; }

inline int countNotes (const NoteMask& m)
{
    int c = 0; for (bool b : m) c += b ? 1 : 0; return c;
}

// nearest allowed note (MIDI number) to a fractional MIDI pitch; prev gives a little hysteresis
inline int nearestNote (float midi, const NoteMask& maskIn, int prev, float hyst = 0.18f)
{
    NoteMask mask = maskIn;
    if (countNotes (mask) == 0) mask.fill (true);

    const int base = (int) std::floor (midi);
    int best = base; float bestD = 1e9f;
    for (int c = base - 7; c <= base + 8; ++c)
    {
        if (! mask[(size_t) pitchClass (c)]) continue;
        const float d = std::fabs (midi - (float) c);
        if (d < bestD) { bestD = d; best = c; }
    }
    if (prev >= 0 && mask[(size_t) pitchClass (prev)] && std::fabs (midi - (float) prev) <= bestD + hyst)
        return prev;
    return best;
}

// move 'steps' scale degrees up (positive) or down (negative) from a note
inline int stepInScale (int note, int steps, const NoteMask& maskIn)
{
    NoteMask mask = maskIn;
    if (countNotes (mask) == 0) mask.fill (true);
    if (steps == 0) return note;
    const int dir = steps > 0 ? 1 : -1;
    int remaining = std::abs (steps), n = note, guard = 0;
    while (remaining > 0 && guard++ < 96)
    {
        n += dir;
        if (mask[(size_t) pitchClass (n)]) --remaining;
    }
    return n;
}
} // namespace atlas
