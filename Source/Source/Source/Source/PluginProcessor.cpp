#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>

namespace
{
// A small additive "soft organ" voice: no aliasing, nothing to configure.
struct PaletteSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

struct PaletteVoice : public juce::SynthesiserVoice
{
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<PaletteSound*> (s) != nullptr; }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        frequency = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        level = velocity * 0.17f;
        phase = 0.0;
        adsr.setSampleRate (getSampleRate());
        adsr.setParameters ({ 0.008f, 0.30f, 0.55f, 0.28f });
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (getCurrentlyPlayingNote() < 0)
            return;

        const double inc = juce::MathConstants<double>::twoPi * frequency / getSampleRate();

        for (int i = 0; i < numSamples; ++i)
        {
            const float env = adsr.getNextSample();
            const double p = phase;
            const float s = (float) (std::sin (p) + 0.35 * std::sin (2.0 * p) + 0.15 * std::sin (3.0 * p) + 0.06 * std::sin (4.0 * p));
            const float v = s * level * env * 0.65f;

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, startSample + i, v);

            phase += inc;
            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

    double frequency = 440.0, phase = 0.0;
    float level = 0.1f;
    juce::ADSR adsr;
};
} // namespace

// -------------------------------------------------------------------------------------------------

PaletteProcessor::PaletteProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < 10; ++i)
        synth.addVoice (new PaletteVoice());
    synth.addSound (new PaletteSound());

    // A tiny starting point so the roll isn't empty: one motive and a plain T-S-D-T / T-S-D7-T progression.
    const theory::Chord prog[pal::numMotives] = {
        { 0, 0, 0 }, { 3, 0, 0 }, { 4, 0, 0 }, { 0, 0, 0 },
        { 0, 0, 0 }, { 3, 0, 0 }, { 4, 1, 0 }, { 0, 0, 0 }
    };
    for (int i = 0; i < pal::numMotives; ++i)
        motives[(size_t) i].chord = prog[i];

    motives[0].notes = { { 0, 4, 7 }, { 4, 2, 9 }, { 6, 2, 8 }, { 8, 4, 7 } };
}

void PaletteProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
}

bool PaletteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PaletteProcessor::setMeter (int index)
{
    meterIndex = juce::jlimit (0, (int) pal::meters().size() - 1, index);
    for (auto& m : motives)
        m.clip (stepsPerMeasure());
}

// -------------------------------------------------------------------------------------------------
// Undo

void PaletteProcessor::pushUndo()
{
    undoStack.push_back (motives);
    if (undoStack.size() > 60)
        undoStack.erase (undoStack.begin());
}

void PaletteProcessor::undo()
{
    if (undoStack.empty())
        return;
    motives = undoStack.back();
    undoStack.pop_back();
}

// -------------------------------------------------------------------------------------------------
// Building sequences

std::vector<int> PaletteProcessor::chordMidiNotes (const theory::Chord& chord) const
{
    std::vector<int> out;
    out.push_back (degreeToMidi (theory::bassClass (chord) - 7));                 // bass, one octave below the tonic
    for (int i = 0; i < theory::noteCount (chord); ++i)
        out.push_back (degreeToMidi ((chord.root + 2 * i) % 7));                  // close voicing around the tonic octave
    return out;
}

void PaletteProcessor::appendMotive (pal::Sequence& seq, int baseStep, const pal::Notes& notes, const theory::Chord& chord) const
{
    const int spm = stepsPerMeasure();

    for (auto& n : notes)
    {
        const int pitch = degreeToMidi (n.degree);
        seq.events.push_back ({ baseStep + n.start, true, pitch, 100, 1 });
        seq.events.push_back ({ baseStep + n.start + n.length, false, pitch, 0, 1 });
    }

    if (chordsOn)
        for (int pitch : chordMidiNotes (chord))
        {
            seq.events.push_back ({ baseStep, true, pitch, 62, 2 });
            seq.events.push_back ({ baseStep + spm, false, pitch, 0, 2 });
        }
}

static void sortEvents (std::vector<pal::Event>& events)
{
    // note-offs before note-ons at the same step, so repeated notes retrigger cleanly
    std::stable_sort (events.begin(), events.end(), [] (const pal::Event& a, const pal::Event& b)
    {
        if (a.step != b.step) return a.step < b.step;
        return (int) a.on < (int) b.on;
    });
}

pal::Sequence PaletteProcessor::buildSong() const
{
    pal::Sequence seq;
    seq.baseMotive = 0;
    const int spm = stepsPerMeasure();
    seq.totalSteps = spm * pal::numMotives;
    for (int i = 0; i < pal::numMotives; ++i)
        appendMotive (seq, i * spm, motives[(size_t) i].notes, motives[(size_t) i].chord);
    sortEvents (seq.events);
    return seq;
}

pal::Sequence PaletteProcessor::buildMotive (const pal::Notes& notes, const theory::Chord& chord, int baseMotive) const
{
    pal::Sequence seq;
    seq.baseMotive = baseMotive;
    seq.totalSteps = stepsPerMeasure();
    appendMotive (seq, 0, notes, chord);
    sortEvents (seq.events);
    return seq;
}

// -------------------------------------------------------------------------------------------------
// Playback

void PaletteProcessor::startPlayback (pal::Sequence seq)
{
    sortEvents (seq.events);
    const juce::ScopedLock sl (seqLock);
    playBase = seq.baseMotive;
    active = std::move (seq);
    nextEvent = 0;
    playPos = 0.0;
    playStep = 0.0;
    panic = true;
    playing = true;
}

void PaletteProcessor::stopPlayback()
{
    playing = false;
    panic = true;
}

void PaletteProcessor::previewNote (int degree)
{
    previewRequest = degreeToMidi (degree);
}

void PaletteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    juce::MidiBuffer generated;

    if (panic.exchange (false))
    {
        for (int ch = 1; ch <= 2; ++ch)
            generated.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        previewActive = -1;
    }

    // one-shot pitch preview (clicking the key labels, adding or moving a note)
    const int request = previewRequest.exchange (-1);
    if (request >= 0)
    {
        if (previewActive >= 0)
            generated.addEvent (juce::MidiMessage::noteOff (1, previewActive), 0);
        generated.addEvent (juce::MidiMessage::noteOn (1, request, (juce::uint8) 100), 0);
        previewActive = request;
        previewSamplesLeft = (int) (getSampleRate() * 0.3);
    }
    else if (previewActive >= 0)
    {
        previewSamplesLeft -= numSamples;
        if (previewSamplesLeft <= 0)
        {
            generated.addEvent (juce::MidiMessage::noteOff (1, previewActive), numSamples - 1);
            previewActive = -1;
        }
    }

    if (playing.load())
    {
        const juce::ScopedTryLock stl (seqLock);
        if (stl.isLocked())
        {
            const double stepSamples = juce::jmax (1.0, getSampleRate() * 15.0 / bpm.load());   // one sixteenth
            const double endPos = playPos + (double) numSamples / stepSamples;

            while (nextEvent < active.events.size() && (double) active.events[nextEvent].step < endPos)
            {
                const auto& e = active.events[nextEvent++];
                const int offset = juce::jlimit (0, numSamples - 1, (int) (((double) e.step - playPos) * stepSamples));
                generated.addEvent (e.on ? juce::MidiMessage::noteOn (e.channel, e.pitch, (juce::uint8) e.velocity)
                                         : juce::MidiMessage::noteOff (e.channel, e.pitch),
                                    offset);
            }

            playPos = endPos;
            playStep = playPos;

            if (nextEvent >= active.events.size() && playPos >= (double) active.totalSteps)
                playing = false;
        }
    }

    juce::MidiBuffer toSynth;
    toSynth.addEvents (midi, 0, numSamples, 0);
    toSynth.addEvents (generated, 0, numSamples, 0);
    synth.renderNextBlock (buffer, toSynth, 0, numSamples);

    midi.addEvents (generated, 0, numSamples, 0);   // also available as MIDI output
}

// -------------------------------------------------------------------------------------------------
// MIDI export

bool PaletteProcessor::writeMidiFile (const juce::File& file) const
{
    const auto& meter = pal::meters()[(size_t) meterIndex];
    constexpr int ticksPerQuarter = 960;
    constexpr int ticksPerStep = ticksPerQuarter / 4;

    juce::MidiMessageSequence melody, chords;
    melody.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm.load())), 0.0);
    melody.addEvent (juce::MidiMessage::timeSignatureMetaEvent (meter.num, meter.den), 0.0);

    for (auto& e : buildSong().events)
    {
        auto msg = e.on ? juce::MidiMessage::noteOn (e.channel, e.pitch, (juce::uint8) e.velocity)
                        : juce::MidiMessage::noteOff (e.channel, e.pitch);
        msg.setTimeStamp ((double) (e.step * ticksPerStep));
        (e.channel == 1 ? melody : chords).addEvent (msg);
    }
    melody.updateMatchedPairs();
    chords.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (ticksPerQuarter);
    midiFile.addTrack (melody);
    if (chords.getNumEvents() > 0)
        midiFile.addTrack (chords);

    file.deleteFile();
    juce::FileOutputStream stream (file);
    return stream.openedOk() && midiFile.writeTo (stream);
}

juce::File PaletteProcessor::exportMidiFile() const
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Palette melody.mid");
    return writeMidiFile (file) ? file : juce::File();
}

// -------------------------------------------------------------------------------------------------
// State

void PaletteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("PaletteMelodyState");
    xml.setAttribute ("tonic", tonicPc);
    xml.setAttribute ("mode", (int) mode);
    xml.setAttribute ("meter", meterIndex);
    xml.setAttribute ("bpm", bpm.load());
    xml.setAttribute ("chords", chordsOn);

    for (auto& m : motives)
    {
        auto* e = xml.createNewChildElement ("Motive");
        e->setAttribute ("root", m.chord.root);
        e->setAttribute ("seventh", m.chord.seventh);
        e->setAttribute ("inv", m.chord.inversion);
        e->setAttribute ("notes", m.notesToString());
    }
    copyXmlToBinary (xml, destData);
}

void PaletteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("PaletteMelodyState"))
        return;

    tonicPc = juce::jlimit (0, 11, xml->getIntAttribute ("tonic", 0));
    mode = xml->getIntAttribute ("mode", 0) == 1 ? theory::Mode::minor : theory::Mode::major;
    meterIndex = juce::jlimit (0, (int) pal::meters().size() - 1, xml->getIntAttribute ("meter", 2));
    bpm = juce::jlimit (40.0, 200.0, xml->getDoubleAttribute ("bpm", 100.0));
    chordsOn = xml->getBoolAttribute ("chords", true);

    int i = 0;
    for (auto* e : xml->getChildWithTagNameIterator ("Motive"))
    {
        if (i >= pal::numMotives)
            break;
        auto& m = motives[(size_t) i++];
        m.chord.root = juce::jlimit (0, 6, e->getIntAttribute ("root", 0));
        m.chord.seventh = juce::jlimit (0, 1, e->getIntAttribute ("seventh", 0));
        m.chord.inversion = juce::jlimit (0, 2 + m.chord.seventh, e->getIntAttribute ("inv", 0));
        m.notesFromString (e->getStringAttribute ("notes"));
        m.clip (stepsPerMeasure());
    }
    undoStack.clear();
}

juce::AudioProcessorEditor* PaletteProcessor::createEditor()
{
    return createPaletteEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PaletteProcessor();
}
