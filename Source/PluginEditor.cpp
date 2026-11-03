#include "PluginProcessor.h"
#include <cmath>
#include <functional>
#include <memory>

namespace
{
const juce::Colour kBg        (0xffe8e4da);
const juce::Colour kPanel     (0xfff6f3ec);
const juce::Colour kInk       (0xff23262d);
const juce::Colour kLine      (0xffbdb8ad);
const juce::Colour kChordTone (0xffc6d3f7);
const juce::Colour kNonChord  (0xfff4c6d6);
const juce::Colour kNoteFill  (0xff4f5461);
const juce::Colour kTonic     (0xff9ed9ea);
const juce::Colour kSub       (0xffb5e0a5);
const juce::Colour kDom       (0xfff2a9c3);
const juce::Colour kWarn      (0xffb00020);

juce::Colour groupColour (const theory::Chord& c)
{
    switch (theory::functionGroup (c.root))
    {
        case 0:  return kTonic;
        case 1:  return kSub;
        default: return kDom;
    }
}

void drawMini (juce::Graphics& g, juce::Rectangle<float> r, const pal::Notes& notes, int steps)
{
    g.setColour (juce::Colours::white);
    g.fillRect (r);
    g.setColour (kLine);
    g.drawRect (r, 1.0f);

    if (notes.empty())
        return;

    int lo = 1000, hi = -1000;
    for (auto& n : notes) { lo = juce::jmin (lo, n.degree); hi = juce::jmax (hi, n.degree); }
    --lo; ++hi;
    while (hi - lo < 7) { --lo; ++hi; }

    const float rowH = r.getHeight() / (float) (hi - lo + 1);
    const float colW = r.getWidth() / (float) steps;

    g.setColour (kNoteFill);
    for (auto& n : notes)
        g.fillRoundedRectangle (r.getX() + (float) n.start * colW + 0.5f,
                                r.getY() + (float) (hi - n.degree) * rowH + 0.5f,
                                juce::jmax (2.0f, (float) n.length * colW - 1.0f),
                                juce::jmax (2.0f, rowH - 1.0f), 1.5f);
}

class PianoRoll : public juce::Component
{
public:
    explicit PianoRoll (PaletteProcessor& p) : proc (p) {}
    std::function<void()> onEdited;
    void setMotive (int index) { motive = index; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto geo = getGeo();
        const auto& m = proc.motives[(size_t) motive];
        const bool flats = theory::prefersFlats (proc.tonicPc, proc.mode);
        g.fillAll (kPanel);

        for (int r = 0; r < geo.rows; ++r)
        {
            const int degree = pal::maxDegree - r;
            const float y = (float) r * geo.rowH;
            g.setColour (theory::isChordTone (m.chord, degree) ? kChordTone : kNonChord);
            g.fillRect (geo.x0, y, (float) getWidth() - geo.x0, geo.rowH);

            const bool isTonic = ((degree % 7) + 7) % 7 == 0;
            g.setColour (isTonic ? kPanel.darker (0.25f) : kPanel);
            g.fillRect (0.0f, y, geo.x0, geo.rowH);

            g.setColour (kInk);
            g.setFont (juce::FontOptions (juce::jmin (13.0f, geo.rowH * 0.8f), isTonic ? juce::Font::bold : juce::Font::plain));
            const int midi = proc.degreeToMidi (degree);
            const auto name = juce::MidiMessage::getMidiNoteName (midi, ! flats, true, 4);
            g.drawText (juce::String (((degree % 7) + 7) % 7 + 1) + "   " + name,
                        juce::Rectangle<float> (6.0f, y, geo.x0 - 8.0f, geo.rowH), juce::Justification::centredLeft);

            g.setColour (kLine.withAlpha (0.5f));
            g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());
        }

        for (int s = 0; s <= geo.steps; ++s)
        {
            const float x = geo.x0 + (float) s * geo.colW;
            g.setColour (s % 4 == 0 ? kInk.withAlpha (0.55f) : kLine.withAlpha (0.8f));
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        }

        for (auto& n : m.notes)
        {
            const float x = geo.x0 + (float) n.start * geo.colW + 1.0f;
            const float y = (float) (pal::maxDegree - n.degree) * geo.rowH + 1.0f;
            const float w = (float) n.length * geo.colW - 2.0f;
            g.setColour (kNoteFill);
            g.fillRoundedRectangle (x, y, w, geo.rowH - 2.0f, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawRoundedRectangle (x, y, w, geo.rowH - 2.0f, 3.0f, 1.0f);
        }

        if (m.notes.empty())
        {
            g.setColour (kInk.withAlpha (0.55f));
            g.setFont (juce::FontOptions (14.0f));
            g.drawText ("Click a cell to add a note, drag its right edge to lengthen it, right-click to delete.",
                        juce::Rectangle<float> (geo.x0, 0.0f, (float) getWidth() - geo.x0, (float) getHeight()),
                        juce::Justification::centred);
        }

        if (proc.isPlaying() && proc.getPlayBase() >= 0)
        {
            const double local = proc.getPlayStep() - (double) (motive - proc.getPlayBase()) * (double) geo.steps;
            if (local >= 0.0 && local < (double) geo.steps)
            {
                g.setColour (juce::Colour (0xffd62839));
                g.fillRect (geo.x0 + (float) local * geo.colW - 1.0f, 0.0f, 2.0f, (float) getHeight());
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto geo = getGeo();
        if ((float) e.x < geo.x0) { proc.previewNote (degreeAt (geo, e.y)); return; }
        auto& m = proc.motives[(size_t) motive];
        const int step = stepAt (geo, e.x);
        const int degree = degreeAt (geo, e.y);
        const int hit = m.findNoteAt (step, degree);

        if (e.mods.isPopupMenu())
        {
            if (hit >= 0) { proc.pushUndo(); m.notes.erase (m.notes.begin() + hit); edited(); }
            return;
        }

        proc.pushUndo();
        original = m;
        lastDegree = degree;

        if (hit >= 0)
        {
            active = origNote = m.notes[(size_t) hit];
            hadOriginal = true;
            const float noteRight = geo.x0 + (float) (active.start + active.length) * geo.colW;
            const bool onRightEdge = (float) e.x > noteRight - juce::jmin (9.0f, geo.colW * (float) active.length * 0.4f);
            drag = onRightEdge ? Drag::resize : Drag::move;
            grabOffset = step - active.start;
            proc.previewNote (active.degree);
        }
        else
        {
            hadOriginal = false;
            active = { step, 1, degree };
            drag = Drag::resize;
            grabOffset = 0;
            apply();
            proc.previewNote (degree);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::none) return;
        const auto geo = getGeo();
        const int step = stepAt (geo, e.x);
        const int degree = degreeAt (geo, e.y);

        if (drag == Drag::resize)
        {
            active.length = juce::jmax (1, step - active.start + 1);
        }
        else
        {
            active.start = juce::jlimit (0, geo.steps - active.length, step - grabOffset);
            active.degree = degree;
            if (degree != lastDegree) { proc.previewNote (degree); lastDegree = degree; }
        }
        apply();
    }

    void mouseUp (const juce::MouseEvent&) override { drag = Drag::none; }

private:
    struct Geo { float x0; float rowH; float colW; int steps; int rows; };
    enum class Drag { none, move, resize };

    Geo getGeo() const
    {
        Geo g;
        g.x0 = 78.0f;
        g.steps = proc.stepsPerMeasure();
        g.rows = pal::maxDegree - pal::minDegree + 1;
        g.colW = ((float) getWidth() - g.x0) / (float) g.steps;
        g.rowH = (float) getHeight() / (float) g.rows;
        return g;
    }

    static int stepAt (const Geo& g, int x)   { return juce::jlimit (0, g.steps - 1, (int) std::floor (((float) x - g.x0) / g.colW)); }
    static int degreeAt (const Geo& g, int y) { return juce::jlimit (pal::minDegree, pal::maxDegree, pal::maxDegree - (int) std::floor ((float) y / g.rowH)); }

    void apply()
    {
        auto m = original;
        if (hadOriginal)
        {
            auto it = std::find (m.notes.begin(), m.notes.end(), origNote);
            if (it != m.notes.end()) m.notes.erase (it);
        }
        m.insertNote (active, proc.stepsPerMeasure());
        proc.motives[(size_t) motive] = m;
        edited();
    }

    void edited() { repaint(); if (onEdited) onEdited(); }

    PaletteProcessor& proc;
    int motive = 0;
    Drag drag = Drag::none;
    pal::Motive original;
    pal::Note active, origNote;
    bool hadOriginal = false;
    int grabOffset = 0, lastDegree = 0;
};
        ...
    int grabOffset = 0, lastDegree = 0;
};  <-- This is the end of Part 1

class MotiveCell : public juce::Component
{  <-- This is the start of Part 2
...
