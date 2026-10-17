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
#include "core/Names.hpp"

class MoonVerbProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    static const int ROWS = 5, COLS = 9, FACTORY_SLOTS = moonverb::ProgramNames::SLOTS, USER_SLOTS = 50;
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
    // the host's program list: the factory programs (slots 0..69), then the 50 registers (70..119); empty slots are listed as such
    int getNumPrograms() override { return FACTORY_SLOTS + USER_SLOTS; }
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // ---- what the front panel does (any thread) ----
    void pressKey(moonverb::Keys::Key k, bool down);        // a key held down / let go, exactly as the scan sees it
    void store();                                           // STORE: REG held down while LOAD is pressed (files the running program in the register the display shows, REG mode)
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

    // ---- presets: a factory program (slot = 10 x row + column, rows 0..6) or a register (0..49). Choosing one loads it. ----
    void loadPreset(bool user, int slot);
    void stepPreset(int dir);                               // the next / previous preset that holds something, across both banks, wrapping
    void storePreset(int slot);                             // file the running program, as it is now, in register `slot`
    void resetCell(int r, int c);                           // a parameter back to the value the loaded preset gave it
    void stepCell(int r, int c, int delta);                 // the firmware's own +-n step on a parameter (the arrow keys)

    // ---- RATE of the BPM programs: HZ shows the program's beat as a rate; SYNC makes it a note value of the host's tempo (the firmware plays 64..191 BPM) ----
    enum { RATE_HZ = 0, RATE_SYNC = 1 };
    void setRateMode(int m);
    void stepRateDiv(int dir);                              // the next / previous note value the firmware can reach at the host's tempo
    static int rateDivs(); static const char* rateDivName(int i); static double rateBpm(double hostBpm, int div); static bool rateReachable(double hostBpm, int div);
    static int rateWord(double hostBpm, int div); static bool isRateText(const char* text13);

    // ---- what the front panel shows (copies, thread-safe) ----
    struct View {
        moonverb::PanelLogic::Snap snap; std::string status, family; bool romsOk = false, live = false, booting = false;
        std::string romFile[moonverb::ROLES]; int midiChannel = 1; bool omni = false, pgmChange = false;
        bool user = false; int slot = 0; bool loading = false, storing = false, modified = false, namesReady = false;      // the preset running (or being loaded / stored)
        std::string storingName;                                                                            // what is being stored (the slot's own name comes after)
        std::string factory[FACTORY_SLOTS], reg[USER_SLOTS];                                                // names; "" = nothing there (factory: not read yet when !namesReady)
        bool baseValid = false; int base[ROWS][COLS] = {};
        int rateMode = 0, rateDiv = 6, rateRow = -1, rateCol = -1; double hostBpm = 120.0;                // RATE: its mode, note value, the host's tempo, the cell (-1: no RATE)                                                  // the words the loaded preset gave each cell
        std::string presetName() const { return user ? reg[slot] : factory[slot]; }
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
    juce::var readSettings() const;
    void writeSettings(const juce::String& key, const juce::var& value);
    void startNames(const moonverb::RomSet& set);
    static juce::String romKey(const moonverb::RomSet& set);
    std::vector<int> presetOrder(const View& v) const;      // every preset that holds something, factory first: user slots as 100 + n

    // ROMs (message thread; guarded)
    std::mutex romMutex; moonverb::RomSet roms; std::string status = "LOAD ROMS";
    // the machine: the audio thread owns `voice`; a worker boots a new one and hands it over atomically
    std::unique_ptr<moonverb::Voice> voice; std::atomic<moonverb::Voice*> handover{nullptr};
    // every power-up is numbered: a machine from an older one (a host that prepares before it restores the state) gives way to the newer one
    std::atomic<uint64_t> bootGen{0}, handoverGen{0}; uint64_t voiceGen = 0;
    std::thread bootThread; std::atomic<bool> booting{false}; std::atomic<bool> restartRequested{false}, keepRam{true}; std::atomic<int> restartsDone{0};
    void stopMachine(bool keepRam);                        // ask the audio thread to drop the machine (saving its RAM first unless told not to) and wait for it
    std::atomic<double> bootRate{0.0}; double voiceRate = 0.0, hostRate = 48000.0; bool prepared = false; std::atomic<bool> power{true};
    moonverb::PanelLogic logic;
    // factory names, read once per ROM set by a scratch machine on a worker (and cached in the settings file)
    std::thread namesThread; std::atomic<bool> cancelNames{false}; moonverb::ProgramNames names; juce::String namesKey;
    // the preset (message thread writes, under snapMutex); the audio thread finishes a load: `loadWait` until the keys and the cell table settle
    std::atomic<int> rateMode{RATE_HZ}, rateDiv{6}; std::atomic<double> hostBpm{120.0}; int rateRow = -1, rateCol = -1;
    bool presetUser = false; int presetSlot = 0; std::atomic<bool> loading{false}, storing{false}; std::string storingName;
    bool baseValid = false; int baseWord[ROWS][COLS] = {}; bool loadWait = false; double loadQuiet = 0.0, loadAge = 0.0; bool loadSeen = false, loadByCall = false; uint64_t loadsAt = 0; std::string loadTarget;    // loadTarget: "r.c" the display shows once the firmware has it running

    std::mutex snapMutex; moonverb::PanelLogic::Snap snap; std::vector<uint8_t> battery; std::string family;
    std::mutex cmdMutex; std::vector<std::function<void(moonverb::Voice&)>> cmds; std::atomic<bool> cmdPending{false};

    // parameters
    std::atomic<float>* cellRaw[ROWS * COLS] = {}; std::atomic<float>* inputRaw = nullptr; std::atomic<float>* inPadRaw = nullptr; std::atomic<float>* bypassRaw = nullptr; std::atomic<float>* outPadRaw = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr; juce::RangedAudioParameter* cellParam[ROWS * COLS] = {};
    friend class MoonVerbEditor;
    // audio -> message thread: knobs the firmware moved by itself
    std::atomic<float> follow[ROWS * COLS]; std::atomic<bool> followPending{false}; std::atomic<int> bypassFollow{-1};
    float followTarget[ROWS * COLS], followFrom[ROWS * COLS];      // audio thread: a followed value not yet seen in the host's parameter, and the host value it replaces

    // audio-thread bookkeeping
    int ctlCount = 0, snapCount = 0, ramCount = 0; bool wasPlaying = false; double nextClockPpq = 0.0; bool clockFromMidi = false;
    bool lastBypassReq = false, bypassInit = false, bypassFollowing = false, bypassTarget = false; float bypassFrom = 0.f; double tapWait = 0.0; int lastFwBypass = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoonVerbProcessor)
};
