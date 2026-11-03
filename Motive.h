#pragma once

#include "Theory.h"
#include <algorithm>
#include <vector>

namespace pal
{
// Melody pitch range in scale degrees: 22 rows, three octaves plus the tonic.
constexpr int minDegree = 0;
constexpr int maxDegree = 21;

// One melody note. Times are in sixteenth-note steps from the start of the motive (one measure).
struct Note
{
    int start = 0;
    int length = 1;
    int degree = 7;

    bool operator== (const Note& o) const { return start == o.start && length == o.length && degree == o.degree; }
};

using Notes = std::vector<Note>;

inline void sortByStart (Notes& n)
{
    std::stable_sort (n.begin(), n.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
}

// A motive is one measure of monophonic melody plus the chord that sits under it.
struct Motive
{
    Notes notes;
    theory::Chord chord;

    // Drop notes outside the measure, shorten notes that run past it or into the next note.
    void clip (int steps)
    {
        sortByStart (notes);
        notes.erase (std::remove_if (notes.begin(), notes.end(),
                                     [steps] (const Note& n) { return n.start < 0 || n.start >= steps || n.length < 1; }),
                     notes.end());
        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& n = notes[i];
            n.degree = juce::jlimit (minDegree, maxDegree, n.degree);
            n.length = juce::jmin (n.length, steps - n.start);
            if (i + 1 < notes.size())
                n.length = juce::jmin (n.length, notes[i + 1].start - n.start);
            n.length = juce::jmax (1, n.length);
        }
    }

    // Insert a note; the new note wins over whatever it overlaps (monophonic melody).
    void insertNote (Note n, int steps)
    {
        n.start = juce::jlimit (0, steps - 1, n.start);
        n.length = juce::jlimit (1, steps - n.start, n.length);
        n.degree = juce::jlimit (minDegree, maxDegree, n.degree);

        for (int i = (int) notes.size(); --i >= 0;)
        {
            auto& o = notes[(size_t) i];
            if (o.start >= n.start && o.start < n.start + n.length)
                notes.erase (notes.begin() + i);
            else if (o.start < n.start && o.start + o.length > n.start)
                o.length = n.start - o.start;
        }
        notes.push_back (n);
        sortByStart (notes);
    }

    int findNoteAt (int step, int degree) const
    {
        for (size_t i = 0; i < notes.size(); ++i)
            if (notes[i].degree == degree && step >= notes[i].start && step < notes[i].start + notes[i].length)
                return (int) i;
        return -1;
    }

    juce::String notesToString() const
    {
        juce::String s;
        for (auto& n : notes)
            s << n.start << "," << n.length << "," << n.degree << ";";
        return s;
    }

    void notesFromString (const juce::String& s)
    {
        notes.clear();
        for (auto& item : juce::StringArray::fromTokens (s, ";", ""))
        {
            auto p = juce::StringArray::fromTokens (item, ",", "");
            if (p.size() == 3)
                notes.push_back ({ p[0].getIntValue(), p[1].getIntValue(), p[2].getIntValue() });
        }
    }
};
} // namespace pal
