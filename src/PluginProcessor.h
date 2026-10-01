// MoonVerb: a digital reverb/effects processor that runs the original unit's own firmware on emulated Z80s and its own signal processor.
// The firmware images are not included: you point the plugin at a folder holding your own dumps (see the manual).
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "core/PanelLogic.hpp"
#include "core/Roms.hpp"
#include "core/BatRam.hpp"

class MoonVerbProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    static const int ROWS = 5, COLS = 9;
    MoonVerbProcessor();
    ~MoonVerbProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MoonVerb"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // ---- what the front panel does (any thread) ----
    void pressKey(moonverb::Keys::Key k, bool down);        // a key held down / let go, exactly as the scan sees it
    void softKnob(int delta);                               // the soft knob's own call on the parameter the display is showing (PARAM mode)
    void setPower(bool on);
    bool powered() const { return power.load(); }
    void powerCycle();
    void clearMemory();
    void setMidiChannel(int ch1to16);
    void setOmni(bool on);
    void setProgramChange(bool on);
    void importBank(const juce::File& f);
    bool exportBank(const juce::File& f);
    void loadRomFolder(const juce::File& dir);
    void forgetRoms();
    void ask(std::function<void(moonverb::Voice&)> f);

    // ---- what the front panel shows (copies, thread-safe) ----
    struct View {
        moonverb::PanelLogic::Snap snap; std::string status, family; bool romsOk = false, live = false, booting = false;
        std::string romFile[moonverb::ROLES]; int midiChannel = 1; bool omni = false, pgmChange = false;
    };
    View view();

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout(MoonVerbProcessor*);

private:
    void timerCallback() override;
    void loadFiles(const std::vector<juce::File>& files);
    void boot(double rate);
    void rememberRoms();
    juce::File settingsFile() const;
    std::vector<uint8_t> batteryCopy();

    // ROMs (message thread; guarded)
    std::mutex romMutex; moonverb::RomSet roms; std::string status = "LOAD ROMS";
    // the machine: the audio thread owns `voice`; a worker boots a new one and hands it over atomically
    std::unique_ptr<moonverb::Voice> voice; std::atomic<moonverb::Voice*> handover{nullptr};
    std::thread bootThread; std::atomic<bool> booting{false}; std::atomic<bool> restartRequested{false};
    std::atomic<double> bootRate{0.0}; double voiceRate = 0.0, hostRate = 48000.0; bool prepared = false; std::atomic<bool> power{true};
    moonverb::PanelLogic logic;

    std::mutex snapMutex; moonverb::PanelLogic::Snap snap; std::vector<uint8_t> battery; std::string family;
    std::mutex cmdMutex; std::vector<std::function<void(moonverb::Voice&)>> cmds; std::atomic<bool> cmdPending{false};

    // parameters
    std::atomic<float>* cellRaw[ROWS * COLS] = {}; std::atomic<float>* inputRaw = nullptr; std::atomic<float>* inPadRaw = nullptr; std::atomic<float>* bypassRaw = nullptr; std::atomic<float>* outPadRaw = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr; juce::RangedAudioParameter* cellParam[ROWS * COLS] = {};
    // audio -> message thread: knobs the firmware moved by itself
    std::atomic<float> follow[ROWS * COLS]; std::atomic<bool> followPending{false}; std::atomic<int> bypassFollow{-1};

    // audio-thread bookkeeping
    int ctlCount = 0, snapCount = 0, ramCount = 0; bool wasPlaying = false; double nextClockPpq = 0.0; bool clockFromMidi = false;
    float lastBypassParam = 0.f; bool bypassInit = false; int lastFwBypass = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoonVerbProcessor)
};
