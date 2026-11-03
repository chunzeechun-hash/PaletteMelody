#pragma once

#include "Motive.h"
#include <array>

// "Methods of motive development", after the ones described in The Palette's help.
// Each method takes the source motive and returns three alternative new motives.
namespace pal
{
enum class Method
{
    sequenceUp, sequenceDown, repeat, inversion, extraction, turnOver,
    newRhythm, varyPitch, varyRhythm, arpeggio, variation, unifyRhythm,
    count
};

inline const char* methodName (Method m)
{
    switch (m)
    {
        case Method::sequenceUp:   return "Sequence Up";
        case Method::sequenceDown: return "Sequence Down";
        case Method::repeat:       return "Repeat";
        case Method::inversion:    return "Inversion";
        case Method::extraction:   return "Extraction";
        case Method::turnOver:     return "Turn Over";
        case Method::newRhythm:    return "New Rhythm";
        case Method::varyPitch:    return "Vary Pitch";
        case Method::varyRhythm:   return "Vary Rhythm";
        case Method::arpeggio:     return "Arpeggio";
        case Method::variation:    return "Variation";
        case Method::unifyRhythm:  return "Unify Rhythm";
        default:                   return "";
    }
}

inline const char* methodHelp (Method m)
{
    switch (m)
    {
        case Method::sequenceUp:   return "Move the motive up by 1, 2 or 3 scale degrees.";
        case Method::sequenceDown: return "Move the motive down by 1, 2 or 3 scale degrees.";
        case Method::repeat:       return "Exact repeat, one octave up, one octave down.";
        case Method::inversion:    return "Flip the intervals: all of them, every second one, or a random choice.";
        case Method::extraction:   return "Keep only part of the motive: first half, last half, or every other note.";
        case Method::turnOver:     return "Reverse it: rhythm and pitches, pitches only, or rhythm only.";
        case Method::newRhythm:    return "Same pitches, brand new rhythm (random - press Regenerate for more).";
        case Method::varyPitch:    return "Nudge one or two notes by a scale degree.";
        case Method::varyRhythm:   return "Shift note boundaries by a sixteenth.";
        case Method::arpeggio:     return "Replace non-chord tones with chord tones: nearest, upwards, downwards.";
        case Method::variation:    return "A bit of rhythm variation plus a bit of pitch variation.";
        case Method::unifyRhythm:  return "Same pitches in even sixteenths, eighths, or quarters.";
        default:                   return "";
    }
}

using Options = std::array<Notes, 3>;

namespace detail
{
    // Keep the result inside the roll (shift by octaves first, clamp as a last resort), then clip.
    inline void tidy (Notes& n, int steps)
    {
        if (n.empty())
            return;

        int lo = 1000, hi = -1000;
        for (auto& x : n) { lo = juce::jmin (lo, x.degree); hi = juce::jmax (hi, x.degree); }

        while (hi > maxDegree && lo - 7 >= minDegree)
        {
            for (auto& x : n) x.degree -= 7;
            hi -= 7; lo -= 7;
        }
        while (lo < minDegree && hi + 7 <= maxDegree)
        {
            for (auto& x : n) x.degree += 7;
            hi += 7; lo += 7;
        }

        Motive tmp;
        tmp.notes = n;
        tmp.clip (steps);
        n = tmp.notes;
    }

    inline void shuffle (std::vector<int>& v, juce::Random& rng)
    {
        for (int i = (int) v.size() - 1; i > 0; --i)
            std::swap (v[(size_t) i], v[(size_t) rng.nextInt (i + 1)]);
    }

    // Move the boundary between two touching notes by one sixteenth.
    inline bool nudgeRhythm (Notes& r, juce::Random& rng)
    {
        const int n = (int) r.size();
        if (n < 2)
            return false;

        for (int attempt = 0; attempt < 24; ++attempt)
        {
            const int i = rng.nextInt (n - 1);
            const int d = rng.nextBool() ? 1 : -1;
            Note& a = r[(size_t) i];
            Note& b = r[(size_t) i + 1];
            if (a.start + a.length != b.start) continue;
            if (a.length + d < 1 || b.length - d < 1) continue;
            a.length += d;
            b.start += d;
            b.length -= d;
            return true;
        }
        return false;
    }

    inline void nudgePitch (Notes& r, juce::Random& rng)
    {
        if (! r.empty())
            r[(size_t) rng.nextInt ((int) r.size())].degree += rng.nextBool() ? 1 : -1;
    }
} // namespace detail

inline Options develop (Method method, const Motive& src, int steps, juce::Random& rng)
{
    Options out;
    Notes base = src.notes;
    sortByStart (base);
    const int n = (int) base.size();
    if (n == 0)
        return out;

    auto transpose = [&] (int by)
    {
        Notes r = base;
        for (auto& x : r) x.degree += by;
        return r;
    };

    const int firstStart = base.front().start;
    int lastEnd = 0;
    for (auto& x : base) lastEnd = juce::jmax (lastEnd, x.start + x.length);

    switch (method)
    {
        case Method::sequenceUp:
            for (int k = 0; k < 3; ++k) out[(size_t) k] = transpose (k + 1);
            break;

        case Method::sequenceDown:
            for (int k = 0; k < 3; ++k) out[(size_t) k] = transpose (-(k + 1));
            break;

        case Method::repeat:
            out[0] = base;
            out[1] = transpose (7);
            out[2] = transpose (-7);
            break;

        case Method::inversion:
            for (int k = 0; k < 3; ++k)
            {
                Notes r = base;
                int prevOriginal = base[0].degree, prevNew = base[0].degree;
                for (int i = 1; i < n; ++i)
                {
                    const int interval = base[(size_t) i].degree - prevOriginal;
                    const bool flip = (k == 0) ? true : (k == 1 ? (i % 2 == 0) : rng.nextBool());
                    prevNew += flip ? -interval : interval;
                    prevOriginal = base[(size_t) i].degree;
                    r[(size_t) i].degree = prevNew;
                }
                out[(size_t) k] = r;
            }
            break;

        case Method::extraction:
        {
            const int half = (n + 1) / 2;
            for (int i = 0; i < half; ++i)         out[0].push_back (base[(size_t) i]);
            for (int i = n - half; i < n; ++i)     out[1].push_back (base[(size_t) i]);
            for (int i = 0; i < n; i += 2)         out[2].push_back (base[(size_t) i]);
            break;
        }

        case Method::turnOver:
        {
            Notes mirrored = base;
            for (auto& x : mirrored)
                x.start = firstStart + lastEnd - (x.start + x.length);
            sortByStart (mirrored);

            out[0] = mirrored;                                      // rhythm and pitches reversed
            out[1] = base;                                          // pitches reversed, rhythm kept
            for (int i = 0; i < n; ++i) out[1][(size_t) i].degree = base[(size_t) (n - 1 - i)].degree;
            out[2] = mirrored;                                      // rhythm reversed, pitch order kept
            for (int i = 0; i < n; ++i) out[2][(size_t) i].degree = base[(size_t) i].degree;
            break;
        }

        case Method::newRhythm:
        {
            const int avail = lastEnd - firstStart;
            const int count = juce::jmin (n, avail);
            for (int k = 0; k < 3; ++k)
            {
                Notes r;
                for (int attempt = 0; attempt < 12; ++attempt)
                {
                    std::vector<int> evens, odds, cuts;
                    for (int c = 1; c < avail; ++c)
                        (c % 2 == 0 ? evens : odds).push_back (c);
                    detail::shuffle (evens, rng);
                    detail::shuffle (odds, rng);
                    for (int e : evens) if ((int) cuts.size() < count - 1) cuts.push_back (e);
                    for (int o : odds)  if ((int) cuts.size() < count - 1) cuts.push_back (o);
                    std::sort (cuts.begin(), cuts.end());

                    r.clear();
                    int t = firstStart;
                    for (int i = 0; i < count; ++i)
                    {
                        const int next = (i < count - 1) ? firstStart + cuts[(size_t) i] : lastEnd;
                        r.push_back ({ t, next - t, base[(size_t) i].degree });
                        t = next;
                    }

                    auto sameRhythm = [] (const Notes& a, const Notes& b)
                    {
                        if (a.size() != b.size()) return false;
                        for (size_t i = 0; i < a.size(); ++i)
                            if (a[i].start != b[i].start || a[i].length != b[i].length) return false;
                        return true;
                    };
                    bool duplicate = sameRhythm (r, base);
                    for (int prior = 0; prior < k && ! duplicate; ++prior)
                        duplicate = sameRhythm (r, out[(size_t) prior]);
                    if (! duplicate) break;
                }
                out[(size_t) k] = r;
            }
            break;
        }

        case Method::varyPitch:
            for (int k = 0; k < 3; ++k)
            {
                Notes r = base;
                for (int c = 0; c < (k == 0 ? 1 : 2); ++c) detail::nudgePitch (r, rng);
                out[(size_t) k] = r;
            }
            break;

        case Method::varyRhythm:
            for (int k = 0; k < 3; ++k)
            {
                Notes r = base;
                for (int c = 0; c <= k; ++c) detail::nudgeRhythm (r, rng);
                out[(size_t) k] = r;
            }
            break;

        case Method::arpeggio:
        {
            auto move = [&] (int degree, int dir)
            {
                if (theory::isChordTone (src.chord, degree))
                    return degree;
                for (int off = 1; off < 7; ++off)
                {
                    const bool up   = dir >= 0 && theory::isChordTone (src.chord, degree + off);
                    const bool down = dir <= 0 && theory::isChordTone (src.chord, degree - off);
                    if (up && down) return degree + (rng.nextBool() ? off : -off);   // nearest, tie broken at random
                    if (up)   return degree + off;
                    if (down) return degree - off;
                }
                return degree;
            };
            for (int k = 0; k < 3; ++k)
            {
                Notes r = base;
                for (auto& x : r) x.degree = move (x.degree, k == 0 ? 0 : (k == 1 ? 1 : -1));
                out[(size_t) k] = r;
            }
            break;
        }

        case Method::variation:
            for (int k = 0; k < 3; ++k)
            {
                Notes r = base;
                for (int c = 0; c <= k; ++c) { detail::nudgeRhythm (r, rng); detail::nudgePitch (r, rng); }
                out[(size_t) k] = r;
            }
            break;

        case Method::unifyRhythm:
            for (int k = 0; k < 3; ++k)
            {
                const int spacing = (k == 0) ? 1 : (k == 1 ? 2 : 4);
                Notes r = base;
                int t = firstStart;
                for (auto& x : r) { x.start = t; x.length = spacing; t += spacing; }
                out[(size_t) k] = r;
            }
            break;

        default:
            break;
    }

    for (auto& o : out)
        detail::tidy (o, steps);
    return out;
}
} // namespace pal
