// Every control of the plugin, driven the way the editor and a host drive it: the audio callback runs on its own thread in real time while the message
// thread dispatches, keys are held for real milliseconds, host parameters are set from the message thread. Each check reads back what the firmware did.
// Needs $MOONVERB_ROMS (a folder holding a V2.0 or V3.01 set); without it the test says SKIP and passes.
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>
#include "PluginProcessor.h"

using moonverb::Keys;
static int failures = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } else { std::printf("  ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)

struct Rig {
    MoonVerbProcessor p; double rate; int block;
    std::thread audio; std::atomic<bool> stop{false};
    std::mutex midiLock; juce::MidiBuffer pendingMidi;
    std::atomic<float> inLevel{0.3f}; std::atomic<double> outRms{0}, inRms{0};
    Rig(double r, int b) : rate(r), block(b) {}
    void start() {
        audio = std::thread([this]() {
            juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; long long n = 0; double acc = 0, accIn = 0; long long accN = 0;
            auto next = std::chrono::steady_clock::now();
            while (!stop.load()) {
                const float a = inLevel.load();
                for (int s = 0; s < block; s++) { const float x = a * (float)std::sin(2 * M_PI * 440.0 * (double)(n + s) / rate); buf.setSample(0, s, x); buf.setSample(1, s, x); accIn += (double)x * x; }
                midi.clear(); { std::lock_guard<std::mutex> l(midiLock); midi.swapWith(pendingMidi); }
                p.processBlock(buf, midi);
                for (int s = 0; s < block; s++) { const double y = buf.getSample(0, s); acc += y * y; }
                accN += block;
                if (accN >= rate * 0.1) { outRms = std::sqrt(acc / accN); inRms = std::sqrt(accIn / accN); acc = accIn = 0; accN = 0; }
                n += block; next += std::chrono::microseconds((long long)(1e6 * block / rate)); std::this_thread::sleep_until(next);
            }
        });
    }
    ~Rig() { stop = true; if (audio.joinable()) audio.join(); }
    void wait(double s) { const auto end = juce::Time::getMillisecondCounterHiRes() + s * 1000.0; while (juce::Time::getMillisecondCounterHiRes() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil(10); }
    MoonVerbProcessor::View v() { return p.view(); }
    std::string disp() { return v().snap.display; }
    // a finger on a panel key: down for `hold` seconds, then the gap the firmware's scan needs before the next key
    void tap(Keys::Key k, double hold = 0.12, double gap = 0.45) { p.pressKey(k, true); wait(hold); p.pressKey(k, false); wait(gap); }
    void midiMsg(const juce::MidiMessage& m) { std::lock_guard<std::mutex> l(midiLock); pendingMidi.addEvent(m, 0); }
    void host(const char* id, float v01) { p.apvts.getParameter(id)->setValueNotifyingHost(v01); }
    bool waitFor(double s, const std::function<bool()>& f) { const auto end = juce::Time::getMillisecondCounterHiRes() + s * 1000.0; while (juce::Time::getMillisecondCounterHiRes() < end) { if (f()) return true; juce::MessageManager::getInstance()->runDispatchLoopUntil(10); } return f(); }
    // samples a condition every 50 ms for `s` seconds; returns how many times it changed value (a loop shows up as many changes)
    int flips(double s, const std::function<bool()>& f) { bool last = f(); int n = 0; const auto end = juce::Time::getMillisecondCounterHiRes() + s * 1000.0; while (juce::Time::getMillisecondCounterHiRes() < end) { wait(0.05); const bool now = f(); if (now != last) { n++; last = now; } } return n; }
    bool up() { return waitFor(120.0, [this]() { const auto x = v(); return x.live && x.snap.tableReady && !x.snap.display.empty() && x.snap.display.find_first_not_of(' ') != std::string::npos && x.snap.display.find('.') != std::string::npos; }); }
};

static bool has(const std::string& s, const char* t) { return s.find(t) != std::string::npos; }
static std::string head(const std::string& s, size_t n = 3) { const size_t a = s.find_first_not_of(' '); return a == std::string::npos ? std::string() : s.substr(a, std::min(n, s.size() - a)); }

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const char* roms = std::getenv("MOONVERB_ROMS");
    if (!roms) { std::printf("SKIP: set MOONVERB_ROMS to a folder holding your firmware images\n"); return 0; }
    const double rate = argc > 1 ? std::atof(argv[1]) : 44100.0; const int block = argc > 2 ? std::atoi(argv[2]) : 512;
    Rig R(rate, block);
    R.p.setPlayConfigDetails(2, 2, rate, block); R.p.loadRomFolder(juce::File(roms)); R.p.prepareToPlay(rate, block); R.start();

    std::printf("power-up\n");
    CHECK(R.up(), "unit comes up: \"%s\"", R.disp().c_str());
    R.wait(1.0);
    CHECK(R.v().snap.mode == 2, "starts in PGM mode (mode %d)", R.v().snap.mode);
    CHECK(!R.v().snap.bypassed, "starts live (not bypassed)");
    CHECK(R.outRms.load() > 0.01, "audio out (rms %.3f for %.3f in)", R.outRms.load(), R.inRms.load());

    std::printf("PGM mode: digits, ROW, LOAD\n");
    R.tap(Keys::digit(3)); CHECK(head(R.disp()) == "0.3", "digit 3 selects program 0.3: \"%s\"", R.disp().c_str());
    R.tap(Keys::F1); CHECK(head(R.disp()) == "1.3", "ROW UP moves to row 1: \"%s\"", R.disp().c_str());
    R.tap(Keys::digit(5)); CHECK(head(R.disp()) == "1.5", "digit 5 selects program 1.5: \"%s\"", R.disp().c_str());
    R.tap(Keys::F2); CHECK(head(R.disp()) == "0.5", "ROW DOWN moves back to row 0: \"%s\"", R.disp().c_str());
    R.tap(Keys::F1); R.tap(Keys::F1); R.tap(Keys::digit(0)); const std::string sel = R.disp();
    R.tap(Keys::LOAD); R.wait(3.0);
    CHECK(head(R.disp()) == "2.0" && R.v().snap.mode == 2, "LOAD loads 2.0: \"%s\" -> \"%s\"", sel.c_str(), R.disp().c_str());
    CHECK(R.outRms.load() > 0.005, "audio out after the load (rms %.3f)", R.outRms.load());

    std::printf("BYPASS key\n");
    R.tap(Keys::BYP); R.wait(0.5);
    CHECK(R.v().snap.bypassed, "BYPASS key bypasses");
    { const int n = R.flips(4.0, [&]() { return R.v().snap.bypassed; }); CHECK(n == 0 && R.v().snap.bypassed, "bypass stays on for 4 s (%d changes)", n); }
    CHECK(R.p.apvts.getRawParameterValue("bypass")->load() > 0.5f, "host bypass parameter follows the key");
    R.tap(Keys::BYP); R.wait(0.5);
    CHECK(!R.v().snap.bypassed, "BYPASS key again: live");
    { const int n = R.flips(4.0, [&]() { return R.v().snap.bypassed; }); CHECK(n == 0 && !R.v().snap.bypassed, "live stays live for 4 s (%d changes)", n); }
    CHECK(R.p.apvts.getRawParameterValue("bypass")->load() < 0.5f, "host bypass parameter follows back");

    std::printf("host bypass parameter\n");
    R.host("bypass", 1.f);
    CHECK(R.waitFor(3.0, [&]() { return R.v().snap.bypassed; }), "host bypass on reaches the firmware");
    { const int n = R.flips(4.0, [&]() { return R.v().snap.bypassed; }); CHECK(n == 0 && R.v().snap.bypassed, "stays bypassed for 4 s (%d changes)", n); }
    R.host("bypass", 0.f);
    CHECK(R.waitFor(3.0, [&]() { return !R.v().snap.bypassed; }), "host bypass off reaches the firmware");
    { const int n = R.flips(4.0, [&]() { return R.v().snap.bypassed; }); CHECK(n == 0 && !R.v().snap.bypassed, "stays live for 4 s (%d changes)", n); }

    std::printf("PARAM mode and the soft knob\n");
    R.tap(Keys::F4); R.wait(0.5);
    CHECK(R.v().snap.mode == 1, "PARAM key enters PARAM mode (mode %d): \"%s\"", R.v().snap.mode, R.disp().c_str());
    R.tap(Keys::digit(6)); R.wait(0.3);
    { const auto a = R.v(); const int r = a.snap.paramRow, c = a.snap.paramCol;
      CHECK(r == 0 && c == 6, "digit 6 shows parameter 0.6: \"%s\"", a.snap.display.c_str());
      const int w0 = a.snap.word[0][6]; R.p.softKnob(-4); R.wait(0.8);
      const auto b = R.v(); CHECK(b.snap.word[0][6] == w0 - 4, "soft knob -4: word %d -> %d, \"%s\" -> \"%s\"", w0, b.snap.word[0][6], a.snap.display.c_str(), b.snap.display.c_str());
      CHECK(b.snap.display != a.snap.display, "the display shows the new value");
      const float hp = R.p.apvts.getRawParameterValue("p06")->load(); const double want = (double)(b.snap.word[0][6] - b.snap.lo[0][6]) / (b.snap.hi[0][6] - b.snap.lo[0][6]);
      CHECK(std::fabs(hp - want) < 0.01, "host parameter 0.6 follows the soft knob (%.3f, word says %.3f)", hp, want);
      R.p.softKnob(+4); R.wait(0.8); CHECK(R.v().snap.word[0][6] == w0, "soft knob +4 returns it (%d)", R.v().snap.word[0][6]); }
    R.tap(Keys::F1); R.wait(0.3); CHECK(R.v().snap.paramRow == 1, "ROW UP in PARAM mode: parameter row 1: \"%s\"", R.disp().c_str());
    R.tap(Keys::F2); R.wait(0.3);
    R.tap(Keys::F4); R.wait(0.5); CHECK(R.v().snap.mode == 2, "PARAM key again: back to PGM mode (%d)", R.v().snap.mode);

    std::printf("host parameter -> firmware\n");
    { const auto a = R.v(); const int lo = a.snap.lo[0][6], hi = a.snap.hi[0][6];
      R.host("p06", 0.f); R.wait(1.0); CHECK(R.v().snap.word[0][6] == lo, "host sets 0.6 to its minimum: word %d (lo %d)", R.v().snap.word[0][6], lo);
      R.host("p06", 1.f); R.wait(1.0); CHECK(R.v().snap.word[0][6] == hi, "host sets 0.6 to its maximum: word %d (hi %d)", R.v().snap.word[0][6], hi);
      const int n = R.flips(2.0, [&]() { return R.v().snap.word[0][6] == hi; }); CHECK(n == 0, "and it stays there (%d changes)", n);
      R.host("p06", (float)(a.snap.word[0][6] - lo) / (float)(hi - lo)); R.wait(1.0); CHECK(R.v().snap.word[0][6] == a.snap.word[0][6], "host puts it back (%d)", R.v().snap.word[0][6]); }

    std::printf("REG mode, STORE, LOAD a register\n");
    { const std::string running = R.disp().substr(4);
      R.tap(Keys::F3); R.wait(0.5); CHECK(R.v().snap.mode == 4, "REG key enters REG mode (%d): \"%s\"", R.v().snap.mode, R.disp().c_str());
      R.tap(Keys::digit(7)); CHECK(head(R.disp()) == "0.7", "digit 7 selects register 0.7: \"%s\"", R.disp().c_str());
      R.p.store(); R.wait(3.0);
      std::printf("  (after STORE the display shows \"%s\")\n", R.disp().c_str());
      R.wait(1.5); const auto x = R.v();
      CHECK(x.snap.mode == 4 && head(x.snap.display) == "0.7" && !has(x.snap.display, "UNUSED"), "STORE files the program in register 0.7: \"%s\" (it was running \"%s\")", x.snap.display.c_str(), running.c_str());
      R.tap(Keys::digit(1)); CHECK(has(R.disp(), "UNUSED"), "register 0.1 is still empty: \"%s\"", R.disp().c_str());
      R.tap(Keys::LOAD); R.wait(1.0); CHECK(has(R.disp(), "UNUSED"), "LOAD on an empty register is refused by the firmware: \"%s\"", R.disp().c_str());
      R.tap(Keys::digit(7)); R.tap(Keys::LOAD); R.wait(3.0); CHECK(head(R.disp()) == "0.7" && !has(R.disp(), "UNUSED"), "LOAD register 0.7: \"%s\"", R.disp().c_str());
      R.tap(Keys::F3); R.wait(0.5); CHECK(R.v().snap.mode != 4, "REG key again leaves REG mode (mode %d, the firmware returns to the mode before)", R.v().snap.mode);
      if (R.v().snap.mode == 1) { R.tap(Keys::F4); R.wait(0.5); }
      CHECK(R.v().snap.mode == 2, "back in PGM mode (%d)", R.v().snap.mode); }

    std::printf("levels\n");
    R.tap(Keys::F2); R.tap(Keys::F2); R.tap(Keys::digit(0)); R.tap(Keys::LOAD); R.wait(3.0);
    CHECK(head(R.disp()) == "0.0", "ROW DOWN twice + 0 + LOAD: \"%s\"", R.disp().c_str());
    { R.wait(0.5); const double ref = R.outRms.load();
      R.host("input", 0.f); R.wait(2.5); const double off = R.outRms.load(); CHECK(off < ref * 0.1 + 1e-4, "input level 0 mutes the input (%.4f -> %.4f)", ref, off);
      R.host("input", 1.f); R.wait(1.0);
      R.host("outpad", 1.f); R.wait(1.0); const double pad = R.outRms.load(); CHECK(pad < R.outRms.load() + 1 && pad < ref * 0.2, "output -20 switch drops the output (%.4f -> %.4f)", ref, pad);
      R.host("outpad", 0.f); R.wait(0.5);
      R.host("inpad", 1.f); R.wait(2.5); const double ip = R.outRms.load(); CHECK(ip < ref * 0.5, "input -20 switch drops the level at the converter (%.4f -> %.4f)", ref, ip);
      R.host("inpad", 0.f); R.wait(0.5); }

    std::printf("MIDI\n");
    R.tap(Keys::digit(1)); R.tap(Keys::LOAD); R.wait(3.0); const std::string before = R.disp();
    R.p.setProgramChange(true); R.wait(1.5);
    CHECK(R.v().pgmChange, "PGM CHANGE switch reaches the firmware");
    R.midiMsg(juce::MidiMessage::programChange(1, 7)); R.wait(3.0);
    CHECK(R.disp() != before, "program change 7 loads register 0.7: \"%s\" -> \"%s\"", before.c_str(), R.disp().c_str());
    R.p.setMidiChannel(5); R.wait(1.5); CHECK(R.v().midiChannel == 5, "MIDI channel 5 (%d)", R.v().midiChannel);
    R.tap(Keys::F3); R.wait(0.5); if (R.v().snap.mode == 1) { R.tap(Keys::F4); R.wait(0.5); }
    R.tap(Keys::digit(0)); R.tap(Keys::LOAD); R.wait(3.0); const std::string b2 = R.disp(); std::printf("  (mode %d, \"%s\")\n", R.v().snap.mode, b2.c_str());
    R.midiMsg(juce::MidiMessage::programChange(1, 7)); R.wait(3.0); CHECK(R.disp() == b2, "program change on channel 1 ignored on channel 5: \"%s\"", R.disp().c_str());
    R.midiMsg(juce::MidiMessage::programChange(5, 7)); R.wait(3.0); CHECK(R.disp() != b2, "program change on channel 5 acts: \"%s\"", R.disp().c_str());
    R.p.setOmni(true); R.wait(1.5); CHECK(R.v().omni, "OMNI on");
    R.p.setOmni(false); R.p.setMidiChannel(1); R.p.setProgramChange(false); R.wait(1.5);
    CHECK(!R.v().omni && R.v().midiChannel == 1 && !R.v().pgmChange, "MIDI settings back (omni %d ch %d pc %d)", (int)R.v().omni, R.v().midiChannel, (int)R.v().pgmChange);

    std::printf("power\n");
    R.p.setPower(false); R.wait(0.5);
    CHECK(!R.v().live, "power off: not live");
    CHECK(R.outRms.load() < 1e-6, "power off: silent (%.6f)", R.outRms.load());
    R.p.setPower(true);
    CHECK(R.up(), "power on: comes up again \"%s\"", R.disp().c_str());
    R.wait(1.0); CHECK(!R.v().snap.bypassed, "power on: live");
    { const int n = R.flips(3.0, [&]() { return R.v().snap.bypassed; }); CHECK(n == 0, "no bypass toggling after power on (%d changes)", n); }
    // bypassed when the power goes off: the flag is in the battery RAM, as on the unit; it must not come back as a loop
    R.tap(Keys::BYP); R.wait(1.0); CHECK(R.v().snap.bypassed, "bypassed before a power cycle");
    R.wait(1.5);           // the RAM image is copied out every second
    R.p.powerCycle();
    CHECK(R.up(), "POWER CYCLE: comes up again \"%s\"", R.disp().c_str());
    R.wait(1.0);
    { const int n = R.flips(4.0, [&]() { return R.v().snap.bypassed; }); CHECK(n == 0, "no bypass toggling after the power cycle (%d changes, bypassed %d)", n, (int)R.v().snap.bypassed); }
    { const bool fw = R.v().snap.bypassed, hp = R.p.apvts.getRawParameterValue("bypass")->load() > 0.5f; CHECK(fw == hp, "host bypass agrees with the firmware after the power cycle (fw %d host %d)", (int)fw, (int)hp); }
    if (R.v().snap.bypassed) { R.tap(Keys::BYP); R.wait(1.0); }
    CHECK(!R.v().snap.bypassed && R.outRms.load() > 0.005, "and can be taken out of bypass (rms %.3f)", R.outRms.load());

    std::printf("resonant chord with no input\n");
    { if (R.v().snap.mode != 2) { R.tap(Keys::F3); R.wait(0.5); if (R.v().snap.mode == 1) { R.tap(Keys::F4); R.wait(0.5); } }
      R.tap(Keys::F1); R.tap(Keys::F1); R.tap(Keys::digit(0)); R.tap(Keys::LOAD); R.wait(3.0); std::printf("  (program \"%s\")\n", R.disp().c_str());
      R.inLevel = 0.f; R.wait(1.0); double r0 = R.outRms.load(); R.wait(5.0); double r1 = R.outRms.load();
      CHECK(r1 < r0 * 0.05, "rings out: rms %.5f 1 s after the input stops, %.5f 6 s after", r0, r1); R.inLevel = 0.3f; }

    std::printf("registers: export, CLEAR MEMORY, import\n");
    { auto inReg = [&]() { R.tap(Keys::F3); R.wait(0.5); if (R.v().snap.mode != 4) { R.tap(Keys::F3); R.wait(0.5); } };
      auto regName = [&](int col) { R.tap(Keys::digit(col)); return R.disp(); };
      const juce::File bank = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("moonverb_controls_bank.syx"); bank.deleteFile();
      R.wait(1.5); CHECK(R.p.exportBank(bank) && bank.getSize() > 100, "EXPORT BANK writes the registers (%lld bytes)", (long long)bank.getSize());
      R.p.clearMemory(); CHECK(R.up(), "CLEAR MEMORY: the unit restarts \"%s\"", R.disp().c_str()); R.wait(1.0);
      inReg(); const std::string empty = regName(7); CHECK(has(empty, "UNUSED"), "after CLEAR MEMORY register 0.7 is empty: \"%s\"", empty.c_str());
      CHECK(!R.v().snap.bypassed, "and the unit is live after CLEAR MEMORY");
      R.p.importBank(bank); R.wait(6.0);
      const std::string back = regName(7); CHECK(!has(back, "UNUSED") && has(back, "MAJOR"), "IMPORT BANK brings register 0.7 back: \"%s\"", back.c_str());
      R.tap(Keys::F3); R.wait(0.5); if (R.v().snap.mode == 1) { R.tap(Keys::F4); R.wait(0.5); }
      bank.deleteFile(); }

    std::printf("state: save, restore into a new instance\n");
    { R.tap(Keys::BYP); R.wait(1.0); R.tap(Keys::BYP); R.wait(1.5);
      juce::MemoryBlock mb; R.p.getStateInformation(mb); CHECK(mb.getSize() > 1000, "state saved (%d bytes)", (int)mb.getSize());
      Rig Q(rate, block); Q.p.setPlayConfigDetails(2, 2, rate, block); Q.p.setStateInformation(mb.getData(), (int)mb.getSize()); Q.p.prepareToPlay(rate, block); Q.start();
      CHECK(Q.v().romsOk, "the restored instance finds its images");
      CHECK(Q.up(), "and comes up: \"%s\"", Q.disp().c_str()); Q.wait(1.0);
      { const int n = Q.flips(3.0, [&]() { return Q.v().snap.bypassed; }); CHECK(n == 0 && !Q.v().snap.bypassed, "restored live and steady (%d changes)", n); }
      Q.tap(Keys::F3); Q.wait(0.5); if (Q.v().snap.mode != 4) { Q.tap(Keys::F3); Q.wait(0.5); }
      Q.tap(Keys::digit(7)); CHECK(has(Q.disp(), "MAJOR"), "with its registers: \"%s\"", Q.disp().c_str());
      CHECK(Q.outRms.load() > 0.005, "and audio (rms %.3f)", Q.outRms.load()); }

    std::printf("FORGET\n");
    R.p.forgetRoms(); R.wait(1.0);
    CHECK(!R.v().live && !R.v().romsOk, "FORGET: no firmware, not live (\"%s\")", R.v().status.c_str());
    CHECK(R.outRms.load() < 1e-6, "and silent (%.6f)", R.outRms.load());

    std::printf(failures ? "FAILED %d of %d\n" : "ok: all %d checks\n", failures ? failures : checks, checks);
    return failures ? 1 : 0;
}
