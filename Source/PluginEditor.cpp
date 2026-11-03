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

class MotiveCell : public juce::Component
{
public:
    MotiveCell (PaletteProcessor& p, int idx) : proc (p), index (idx)
    {
        addAndMakeVisible (chordButton);
        chordButton.onClick = [this] { if (onChordClicked) onChordClicked (index); };
    }

    std::function<void (int)> onSelect, onAudition, onChordClicked;
    bool selected = false;
    juce::TextButton chordButton;

    void refresh()
    {
        const auto& m = proc.motives[(size_t) index];
        const theory::Chord* prev = index > 0 ? &proc.motives[(size_t) index - 1].chord : nullptr;
        const bool ok = theory::canFollow (prev, m.chord);

        chordButton.setButtonText (theory::chordLabel (m.chord) + "  " + theory::guitarName (m.chord, proc.mode, proc.tonicPc));
        chordButton.setColour (juce::TextButton::buttonColourId, groupColour (m.chord));
        chordButton.setColour (juce::TextButton::textColourOffId, ok ? kInk : kWarn);
        chordButton.setTooltip (ok ? "Click to choose a chord"
                                   : "This chord doesn't follow the functional logic of the chord before it");
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (selected ? juce::Colour (0xffcfc3f5) : kPanel);
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (selected ? juce::Colour (0xff6b4fd8) : kLine);
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, selected ? 2.0f : 1.0f);

        g.setColour (kInk);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1), 8, 3, 20, 14, juce::Justification::centredLeft);

        drawMini (g, juce::Rectangle<float> (6.0f, 20.0f, area.getWidth() - 12.0f, area.getHeight() - 20.0f - 30.0f),
                  proc.motives[(size_t) index].notes, proc.stepsPerMeasure());
    }

    void resized() override
    {
        chordButton.setBounds (getLocalBounds().removeFromBottom (26).reduced (4, 0).withTrimmedBottom (3));
    }

    void mouseDown (const juce::MouseEvent&) override { if (onSelect) onSelect (index); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onAudition) onAudition (index); }

private:
    PaletteProcessor& proc;
    int index;
};

class OptionCard : public juce::Component
{
public:
    OptionCard (int number) : title ("Option " + juce::String (number))
    {
        addAndMakeVisible (playButton);
        addAndMakeVisible (useButton);
        playButton.onClick = [this] { if (onPlay) onPlay(); };
        useButton.onClick  = [this] { if (onUse) onUse(); };
    }

    std::function<void()> onPlay, onUse;

    void setNotes (const pal::Notes& n, int stepsPerMeasure)
    {
        notes = n;
        steps = stepsPerMeasure;
        playButton.setEnabled (! n.empty());
        useButton.setEnabled (! n.empty());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (kPanel);
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (kLine);
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

        g.setColour (kInk);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, 8, 4, 100, 14, juce::Justification::centredLeft);
        drawMini (g, juce::Rectangle<float> (6.0f, 22.0f, area.getWidth() - 110.0f, area.getHeight() - 28.0f), notes, steps);
    }

    void resized() override
    {
        auto r = getLocalBounds().removeFromRight (94).reduced (4);
        playButton.setBounds (r.removeFromTop (r.getHeight() / 2).withTrimmedBottom (3));
        useButton.setBounds (r.withTrimmedTop (3));
    }

private:
    juce::String title;
    pal::Notes notes;
    int steps = 16;
    juce::TextButton playButton { "Play" }, useButton { "Use..." };
};

class MidiDragButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MidiDragButton (PaletteProcessor& p) : proc (p)
    {
        setTooltip ("Drag this onto a track in your DAW to drop in the melody and chords as MIDI.");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff6b4fd8));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Drag MIDI to a track", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < 6) return;
        started = true;
        const auto file = proc.exportMidiFile();
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

    void mouseUp (const juce::MouseEvent&) override { started = false; }

private:
    PaletteProcessor& proc;
    bool started = false;
};

class PaletteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PaletteEditor (PaletteProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p), roll (p), dragButton (p)
    {
        setSize (1000, 690);

        for (int i = 0; i < 12; ++i)
            keyBox.addItem (theory::pcName (i, theory::prefersFlats (i, theory::Mode::major)), i + 1);
        modeBox.addItem ("Major", 1);
        modeBox.addItem ("Minor", 2);
        for (auto& m : pal::meters()) meterBox.addItem (m.name, meterBox.getNumItems() + 1);

        keyBox.setSelectedId (proc.tonicPc + 1, juce::dontSendNotification);
        modeBox.setSelectedId (proc.mode == theory::Mode::major ? 1 : 2, juce::dontSendNotification);
        meterBox.setSelectedId (proc.meterIndex + 1, juce::dontSendNotification);

        keyBox.onChange = [this] { proc.tonicPc = keyBox.getSelectedId() - 1; refreshAll(); };
        modeBox.onChange = [this] { proc.mode = modeBox.getSelectedId() == 1 ? theory::Mode::major : theory::Mode::minor; refreshAll(); };
        meterBox.onChange = [this] { proc.pushUndo(); proc.setMeter (meterBox.getSelectedId() - 1); refreshAll(); };

        tempoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        tempoSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 22);
        tempoSlider.setRange (40.0, 200.0, 1.0);
        tempoSlider.setValue (proc.bpm.load(), juce::dontSendNotification);
        tempoSlider.onValueChange = [this] { proc.bpm = tempoSlider.getValue(); };

        playButton.onClick = [this]
        {
            if (proc.isPlaying()) proc.stopPlayback();
            else                  proc.startPlayback (proc.buildSong());
        };

        chordsToggle.setToggleState (proc.chordsOn, juce::dontSendNotification);
        chordsToggle.onClick = [this] { proc.chordsOn = chordsToggle.getToggleState(); };
        undoButton.onClick = [this] { proc.undo(); refreshAll(); };
        
        saveButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Save melody as MIDI",
                                                           juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Palette melody.mid"),
                                                           "*.mid");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f != juce::File())
                                          proc.writeMidiFile (f.withFileExtension ("mid"));
                                  });
        };

        for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &modeBox, &meterBox, &tempoSlider, &playButton,
                                                                  &chordsToggle, &undoButton, &saveButton, &dragButton })
            addAndMakeVisible (c);

        for (auto* l : { &keyLabel, &tempoLabel })
        {
            l->setColour (juce::Label::textColourId, kInk);
            addAndMakeVisible (l);
        }

        for (int i = 0; i < pal::numMotives; ++i)
        {
            auto cell = std::make_unique<MotiveCell> (proc, i);
            cell->onSelect = [this] (int idx) { selectMotive (idx); };
            cell->onAudition = [this] (int idx)
            {
                const auto& m = proc.motives[(size_t) idx];
                proc.startPlayback (proc.buildMotive (m.notes, m.chord, idx));
            };
            cell->onChordClicked = [this] (int idx) { showChordMenu (idx, false); };
            addAndMakeVisible (*cell);
            cells.push_back (std::move (cell));
        }

        roll.onEdited = [this] { refreshCells(); };
        addAndMakeVisible (roll);

        devLabel.setText ("Develop the selected motive", juce::dontSendNotification);
        devLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        devLabel.setColour (juce::Label::textColourId, kInk);
        addAndMakeVisible (devLabel);

        for (int i = 0; i < (int) pal::Method::count; ++i)
        {
            auto b = std::make_unique<juce::TextButton> (pal::methodName ((pal::Method) i));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (101);
            b->setTooltip (pal::methodHelp ((pal::Method) i));
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffcfc3f5));
            b->setColour (juce::TextButton::textColourOnId, kInk);
            b->onClick = [this, i] { currentMethod = i; regenerate(); };
            addAndMakeVisible (*b);
            methodButtons.push_back (std::move (b));
        }

        regenButton.onClick = [this] { regenerate(); };
        addAndMakeVisible (regenButton);

        for (int i = 0; i < 3; ++i)
        {
            auto card = std::make_unique<OptionCard> (i + 1);
            card->setNotes ({}, proc.stepsPerMeasure());
            card->onPlay = [this, i]
            {
                proc.startPlayback (proc.buildMotive (options[(size_t) i], proc.motives[(size_t) selected].chord, -1));
            };
            card->onUse = [this, i] { showUseMenu (i); };
            addAndMakeVisible (*card);
            cards.push_back (std::move (card));
        }

        hint.setText ("Click a key label to hear a pitch. Double-click a motive to hear it. Blue rows are chord tones, pink rows are not.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, kInk.withAlpha (0.7f));
        hint.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (hint);

        selectMotive (0);
        refreshAll();
        startTimerHz (30);
    }

    ~PaletteEditor() override { stopTimer(); }
    void paint (juce::Graphics& g) override { g.fillAll (kBg); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        auto top = area.removeFromTop (34);
        keyLabel.setText ("Key", juce::dontSendNotification);
        keyLabel.setBounds (top.removeFromLeft (28));
        keyBox.setBounds (top.removeFromLeft (64));
        top.removeFromLeft (4);
        modeBox.setBounds (top.removeFromLeft (84));
        top.removeFromLeft (8);
        meterBox.setBounds (top.removeFromLeft (64));
        top.removeFromLeft (14);
        tempoLabel.setText ("Tempo", juce::dontSendNotification);
        tempoLabel.setBounds (top.removeFromLeft (48));
        tempoSlider.setBounds (top.removeFromLeft (190));
        top.removeFromLeft (12);
        playButton.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (6);
        chordsToggle.setBounds (top.removeFromLeft (78));
        top.removeFromLeft (6);
        undoButton.setBounds (top.removeFromLeft (60));
        dragButton.setBounds (top.removeFromRight (170));
        top.removeFromRight (6);
        saveButton.setBounds (top.removeFromRight (96));

        area.removeFromTop (8);
        auto strip = area.removeFromTop (118);
        const int gap = 6;
        const int cellW = (strip.getWidth() - gap * (pal::numMotives - 1)) / pal::numMotives;
        for (auto& c : cells)
        {
            c->setBounds (strip.removeFromLeft (cellW));
            strip.removeFromLeft (gap);
        }

        area.removeFromTop (10);
        auto bottom = area.removeFromBottom (22);
        hint.setBounds (bottom);

        auto right = area.removeFromRight (336);
        area.removeFromRight (12);
        roll.setBounds (area);

        auto head = right.removeFromTop (24);
        regenButton.setBounds (head.removeFromRight (90));
        devLabel.setBounds (head);
        right.removeFromTop (4);

        const int bw = (right.getWidth() - 12) / 3;
        for (size_t i = 0; i < methodButtons.size(); ++i)
        {
            const int row = (int) i / 3, col = (int) i % 3;
            methodButtons[i]->setBounds (right.getX() + col * (bw + 6), right.getY() + row * 34, bw, 30);
        }
        right.removeFromTop (4 * 34 + 6);

        const int cardH = (right.getHeight() - 12) / 3;
        for (auto& c : cards)
        {
            c->setBounds (right.removeFromTop (cardH));
            right.removeFromTop (6);
        }
    }

private:
    void timerCallback() override
    {
        const bool playing = proc.isPlaying();
        if (playing) roll.repaint();
        if (playing != wasPlaying)
        {
            wasPlaying = playing;
            playButton.setButtonText (playing ? "Stop" : "Play");
            roll.repaint();
        }
        undoButton.setEnabled (proc.canUndo());
    }

    void selectMotive (int index)
    {
        selected = juce::jlimit (0, pal::numMotives - 1, index);
        for (size_t i = 0; i < cells.size(); ++i)
        {
            cells[i]->selected = ((int) i == selected);
            cells[i]->repaint();
        }
        roll.setMotive (selected);
        regenerate();
    }

    void refreshCells()
    {
        for (auto& c : cells) { c->refresh(); c->repaint(); }
    }

    void refreshAll()
    {
        refreshCells();
        roll.repaint();
        for (auto& c : cards) c->setNotes ({}, proc.stepsPerMeasure());
        regenerate();
    }

    void regenerate()
    {
        const int steps = proc.stepsPerMeasure();
        if (currentMethod < 0) return;
        options = pal::develop ((pal::Method) currentMethod, proc.motives[(size_t) selected], steps, rng);
        for (size_t i = 0; i < cards.size(); ++i)
            cards[i]->setNotes (options[i], steps);
    }

    void setChord (int motiveIndex, const theory::Chord& chord)
    {
        proc.pushUndo();
        proc.motives[(size_t) motiveIndex].chord = chord;
        refreshCells();
        roll.repaint();
        regenerate();
        const auto& m = proc.motives[(size_t) motiveIndex];
        proc.startPlayback (proc.buildMotive (m.notes, m.chord, motiveIndex));
    }

    void showChordMenu (int motiveIndex, bool showAll)
    {
        const theory::Chord* prev = motiveIndex > 0 ? &proc.motives[(size_t) motiveIndex - 1].chord : nullptr;
        const auto all = theory::allChords();

        juce::PopupMenu groups[3];
        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto& c = all[i];
            if (! showAll && ! theory::canFollow (prev, c)) continue;
            const auto text = theory::chordLabel (c) + "    " + theory::guitarName (c, proc.mode, proc.tonicPc);
            groups[theory::functionGroup (c.root)].addItem (juce::PopupMenu::Item (text).setID ((int) i + 1).setColour (kInk));
        }

        juce::PopupMenu menu;
        const char* names[3] = { "Tonic (T)", "Subdominant (S)", "Dominant (D)" };
        for (int g = 0; g < 3; ++g)
            if (groups[g].getNumItems() > 0) menu.addSubMenu (names[g], groups[g]);

        menu.addSeparator();
        menu.addItem (100000, showAll ? "Only chords that follow the rules" : "Show all chords...");

        juce::Component::SafePointer<PaletteEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&cells[(size_t) motiveIndex]->chordButton),
                            [safe, motiveIndex, showAll, all] (int result)
                            {
                                if (safe == nullptr || result == 0) return;
                                if (result == 100000) safe->showChordMenu (motiveIndex, ! showAll);
                                else safe->setChord (motiveIndex, all[(size_t) result - 1]);
                            });
    }

    void showUseMenu (int optionIndex)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Copy this option into motive...");
        for (int i = 0; i < pal::numMotives; ++i)
            menu.addItem (i + 1, "Motive " + juce::String (i + 1) + (i == selected ? "  (replace the one you're developing)" : ""));

        juce::Component::SafePointer<PaletteEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (cards[(size_t) optionIndex].get()),
                            [safe, optionIndex] (int result)
                            {
                                if (safe == nullptr || result == 0) return;
                                safe->proc.pushUndo();
                                safe->proc.motives[(size_t) result - 1].notes = safe->options[(size_t) optionIndex];
                                safe->proc.motives[(size_t) result - 1].clip (safe->proc.stepsPerMeasure());
                                safe->selectMotive (result - 1);
                                safe->refreshCells();
                            });
    }

    PaletteProcessor& proc;
    PianoRoll roll;
    MidiDragButton dragButton;
    std::vector<std::unique_ptr<MotiveCell>> cells;
    std::vector<std::unique_ptr<juce::TextButton>> methodButtons;
    std::vector<std::unique_ptr<OptionCard>> cards;

    juce::ComboBox keyBox, modeBox, meterBox;
    juce::Slider tempoSlider;
    juce::TextButton playButton { "Play" }, undoButton { "Undo" }, saveButton { "Save .mid" }, regenButton { "Regenerate" };
    juce::ToggleButton chordsToggle { "Chords" };
    juce::Label keyLabel, tempoLabel, devLabel, hint;
    juce::TooltipWindow tooltipWindow { this, 450 };
    std::unique_ptr<juce::FileChooser> chooser;

    pal::Options options;
    juce::Random rng;
    int selected = 0;
    int currentMethod = -1;
    bool wasPlaying = false;
};
} // namespace

juce::AudioProcessorEditor* createPaletteEditor (PaletteProcessor& p)
{
    return new PaletteEditor (p);
}
