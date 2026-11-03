#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "Development.h"

namespace pal
{
struct Meter { const char* name; int num; int den; int steps; };

inline const std::array<Meter, 4>& meters()
{
    static const std::array<Meter, 4> m {{ { "2/4", 2, 4, 8 }, { "3/4", 3, 4, 12 }, { "4/4", 4, 4, 16 }, { "6/8", 6, 8, 12 } }};
    return m;
}

constexpr int numMotives = 8;
using Song = std::array<Motive, numMotives>;

struct Event { int step; bool on; int pitch; int velocity; int channel; };

struct Sequence
{
    std::vector<Event> events;
    int totalSteps = 0;
    int baseMotive = -1;   // which motive the first step belongs to (for the playhead); -1 = none
};
} // namespace pal

class PaletteProcessor : public juce::AudioProcessor
{
public:
    PaletteProcessor();
    ~PaletteProcessor() override = default;

    // --- juce::AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Palette Melody"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- the melody model (touched from the message thread only) ---
    pal::Song motives;
    int tonicPc = 0;                       // 0 = C ... 11 = B
    theory::Mode mode = theory::Mode::major;
    int meterIndex = 2;                    // index into pal::meters()
    std::atomic<double> bpm { 100.0 };
    bool chordsOn = true;

    int stepsPerMeasure() const { return pal::meters()[(size_t) meterIndex].steps; }
    void setMeter (int index);

    int degreeToMidi (int degree) const { return theory::degreeToMidi (mode, tonicPc, degree); }
    std::vector<int> chordMidiNotes (const theory::Chord& chord) const;

    // undo (snapshots of the whole song)
    void pushUndo();
    void undo();
    bool canUndo() const { return ! undoStack.empty(); }

    // --- playback ---
    pal::Sequence buildSong() const;
    pal::Sequence buildMotive (const pal::Notes& notes, const theory::Chord& chord, int baseMotive) const;
    void startPlayback (pal::Sequence seq);
    void stopPlayback();
    bool isPlaying() const { return playing.load(); }
    double getPlayStep() const { return playStep.load(); }
    int getPlayBase() const { return playBase.load(); }
    void previewNote (int degree);

    // --- MIDI export ---
    bool writeMidiFile (const juce::File& file) const;
    juce::File exportMidiFile() const;   // writes to a temp file, ready to drag into the DAW

private:
    void appendMotive (pal::Sequence& seq, int baseStep, const pal::Notes& notes, const theory::Chord& chord) const;

    juce::Synthesiser synth;

    juce::CriticalSection seqLock;
    pal::Sequence active;
    size_t nextEvent = 0;
    double playPos = 0.0;

    std::atomic<bool> playing { false };
    std::atomic<bool> panic { false };
    std::atomic<double> playStep { 0.0 };
    std::atomic<int> playBase { -1 };

    std::atomic<int> previewRequest { -1 };
    int previewActive = -1;
    int previewSamplesLeft = 0;

    std::vector<pal::Song> undoStack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaletteProcessor)
};

juce::AudioProcessorEditor* createPaletteEditor (PaletteProcessor&);
