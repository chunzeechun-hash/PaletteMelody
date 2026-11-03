#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

// Scale, chord and functional-harmony helpers.
// Everything in the plugin is stored as diatonic scale degrees (0 = tonic), so changing
// key or mode simply re-pitches the whole melody.
namespace theory
{
// Minor = harmonic minor (raised 7th, major V), the same minor used by The Palette.
enum class Mode { major = 0, minor = 1 };

inline int floorDiv7 (int v) { return v >= 0 ? v / 7 : -((-v + 6) / 7); }

inline int degreeToSemis (Mode m, int degree)
{
    static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int min[7] = { 0, 2, 3, 5, 7, 8, 11 };
    const int oct = floorDiv7 (degree);
    const int idx = degree - oct * 7;
    return (m == Mode::major ? maj[idx] : min[idx]) + 12 * oct;
}

// Degree 0 sits at C3..B3 depending on the key; chords live around here, the melody above it.
inline int tonicMidi (int tonicPc) { return 48 + tonicPc; }
inline int degreeToMidi (Mode m, int tonicPc, int degree) { return tonicMidi (tonicPc) + degreeToSemis (m, degree); }

inline bool prefersFlats (int tonicPc, Mode m)
{
    static const bool majorFlats[12] = { false, true, false, true, false, true, false, false, true, false, true, false };
    static const bool minorFlats[12] = { true, false, true, true, false, true, false, true, false, false, true, false };
    return m == Mode::major ? majorFlats[tonicPc % 12] : minorFlats[tonicPc % 12];
}

inline juce::String pcName (int pc, bool flats)
{
    static const char* sharp[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flat[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return (flats ? flat : sharp)[((pc % 12) + 12) % 12];
}

// ---------------------------------------------------------------------------------------------
struct Chord
{
    int root = 0;        // 0..6  (I..VII)
    int seventh = 0;     // 0 = triad, 1 = seventh chord
    int inversion = 0;   // triads 0..2, sevenths 0..3

    bool operator== (const Chord& o) const { return root == o.root && seventh == o.seventh && inversion == o.inversion; }
};

inline int noteCount (const Chord& c) { return 3 + c.seventh; }
inline int bassClass (const Chord& c)  { return (c.root + 2 * c.inversion) % 7; }

inline bool isChordTone (const Chord& c, int degree)
{
    const int rel = ((degree - c.root) % 7 + 7) % 7;
    return rel == 0 || rel == 2 || rel == 4 || (c.seventh != 0 && rel == 6);
}

// 0 = tonic, 1 = subdominant, 2 = dominant
inline int functionGroup (int root)
{
    switch (root)
    {
        case 0:  return 0;
        case 1: case 3: case 5: return 1;
        default: return 2;
    }
}

// "How strenuous does this chord sound": tonic < VI < IV < II < III < V < VII.
inline int tension (int root)
{
    static const int t[7] = { 0, 12, 20, 11, 21, 10, 22 };
    return t[root];
}

inline int formRank (const Chord& c) { return c.seventh ? 3 + c.inversion : c.inversion; }
inline bool isK64 (const Chord& c)   { return c.root == 0 && c.seventh == 0 && c.inversion == 2; }

inline juce::String romanNumeral (int root)
{
    static const char* r[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    return r[root];
}

inline juce::String formName (const Chord& c)
{
    static const char* tri[3] = { "5/3", "6", "6/4" };
    static const char* sev[4] = { "7", "6/5", "4/3", "2" };
    return c.seventh ? sev[c.inversion] : tri[c.inversion];
}

inline juce::String chordLabel (const Chord& c)
{
    if (isK64 (c))
        return "K6/4";
    if (c.root == 0) return "T" + formName (c);
    if (c.root == 3) return "S" + formName (c);
    if (c.root == 4) return "D" + formName (c);
    return romanNumeral (c.root) + " " + formName (c);
}

inline std::vector<Chord> allChords()
{
    std::vector<Chord> out;
    for (int root = 0; root < 7; ++root)
        for (int sev = 0; sev < 2; ++sev)
            for (int inv = 0; inv < 3 + sev; ++inv)
                out.push_back ({ root, sev, inv });
    return out;
}

// The "logic of functional motion" from The Palette's help, as a simple rule set:
//  - start on the tonic;
//  - every chord should sound more strenuous than the one before (T -> S -> D), so the
//    subdominant never follows the dominant; the tonic can always be returned to;
//  - inversions and sevenths of a degree come after that degree's plain triad;
//  - the cadential K6/4 follows tonic/subdominant chords and must go to the dominant.
inline bool canFollow (const Chord* prev, const Chord& c)
{
    if (prev == nullptr)
        return c.root == 0 && c.seventh == 0 && c.inversion <= 1;

    const Chord& p = *prev;

    if (isK64 (p))
        return c.root == 4 && c.inversion == 0;

    if (isK64 (c))
        return functionGroup (p.root) <= 1;

    if (c.root == p.root)
        return formRank (c) > formRank (p);

    if (c.root == 0)
        return c.seventh == 0 && c.inversion <= 1;

    return tension (c.root) > tension (p.root);
}

// Guitar-style chord name, e.g. "Dm7", "G7", "C/E".
inline juce::String guitarName (const Chord& c, Mode m, int tonicPc)
{
    const bool flats = prefersFlats (tonicPc, m);
    const int n = noteCount (c);
    int pcs[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < n; ++i)
        pcs[i] = (tonicPc + degreeToSemis (m, c.root + 2 * i)) % 12;

    auto iv = [&] (int a, int b) { return (pcs[b] - pcs[a] + 12) % 12; };
    const int i1 = iv (0, 1), i2 = iv (1, 2);

    juce::String quality;
    if (n == 3)
    {
        if (i1 == 4 && i2 == 3)      quality = "";
        else if (i1 == 3 && i2 == 4) quality = "m";
        else if (i1 == 3 && i2 == 3) quality = "dim";
        else                         quality = "aug";
    }
    else
    {
        const int i3 = iv (2, 3);
        if (i1 == 4 && i2 == 3 && i3 == 4)      quality = "Maj7";
        else if (i1 == 4 && i2 == 3 && i3 == 3) quality = "7";
        else if (i1 == 3 && i2 == 4 && i3 == 3) quality = "m7";
        else if (i1 == 3 && i2 == 3 && i3 == 4) quality = "m7b5";
        else if (i1 == 3 && i2 == 3 && i3 == 3) quality = "dim7";
        else if (i1 == 3 && i2 == 4 && i3 == 4) quality = "mMaj7";
        else                                    quality = "Maj7#5";
    }

    juce::String name = pcName (pcs[0], flats) + quality;
    if (c.inversion > 0)
        name << "/" << pcName (pcs[c.inversion], flats);
    return name;
}
} // namespace theory
