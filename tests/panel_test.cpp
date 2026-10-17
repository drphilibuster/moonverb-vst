// The panel's functions, driven through the processor as the editor drives them, with the audio callback running in real time and the message thread
// dispatching: preset names, stepping, choosing, storing; every parameter of a program; reset; masters; the host's program list; bypass; state. Ends with a
// snapshot of the editor (panel.png). Needs $MOONVERB_ROMS; without it the test says SKIP and passes.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } else if (!quiet) { std::printf("  ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)
static bool quiet = false;

struct Rig {
    MoonVerbProcessor p; double rate; int block; std::thread audio; std::atomic<bool> stop{false}; std::atomic<double> outRms{0};
    Rig(double r, int b) : rate(r), block(b) {}
    void start() {
        audio = std::thread([this]() {
            juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; long long n = 0; double acc = 0; long long accN = 0;
            auto next = std::chrono::steady_clock::now();
            while (!stop.load()) {
                for (int s = 0; s < block; s++) { const float x = 0.3f * (float)std::sin(2 * M_PI * 440.0 * (double)(n + s) / rate) * (((n + s) / (long long)(rate * 0.2)) % 2 ? 0.f : 1.f); buf.setSample(0, s, x); buf.setSample(1, s, x); }
                midi.clear(); p.processBlock(buf, midi);
                for (int s = 0; s < block; s++) { const double y = buf.getSample(0, s); acc += y * y; }
                if ((accN += block) >= rate * 0.4) { outRms = std::sqrt(acc / accN); acc = 0; accN = 0; }
                n += block; next += std::chrono::microseconds((long long)(1e6 * block / rate)); std::this_thread::sleep_until(next);
            }
        });
    }
    ~Rig() { stop = true; if (audio.joinable()) audio.join(); }
    void wait(double s) { const auto end = juce::Time::getMillisecondCounterHiRes() + s * 1000.0; while (juce::Time::getMillisecondCounterHiRes() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil(10); }
    bool waitFor(double s, const std::function<bool()>& f) { const auto end = juce::Time::getMillisecondCounterHiRes() + s * 1000.0; while (juce::Time::getMillisecondCounterHiRes() < end) { if (f()) return true; juce::MessageManager::getInstance()->runDispatchLoopUntil(10); } return f(); }
    MoonVerbProcessor::View v() { return p.view(); }
    bool up() { return waitFor(120.0, [this]() { const auto x = v(); return x.live && x.snap.tableReady && x.snap.display.find('.') != std::string::npos; }); }
    // a load is done when the processor says so; returns the seconds it took
    double loaded(double timeout = 8.0) { const double t0 = juce::Time::getMillisecondCounterHiRes(); wait(0.05); waitFor(timeout, [this]() { const auto x = v(); return !x.loading && x.snap.tableReady && x.baseValid; }); return (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0; }
};

static std::string trimmed(const std::string& s) { const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' '); return a == std::string::npos ? std::string() : s.substr(a, b - a + 1); }
static std::string squeeze(const std::string& s) { std::string o; for (char ch : s) if (ch != ' ' || (!o.empty() && o.back() != ' ')) o += ch; return trimmed(o); }
static std::string past(const std::string& disp) { const size_t a = disp.find_first_of("0123456789"); return a == std::string::npos || a > 3 ? trimmed(disp) : disp.substr(a); }   // V3's indicator glyphs come first
static std::string where(const std::string& disp) { const std::string t = past(disp); return t.size() >= 3 ? t.substr(0, 3) : t; }
static std::string slotStr(int s) { return std::to_string(s / 10) + "." + std::to_string(s % 10); }
// the display shows "1.3 CIRCULAR DLYS": does it show this slot, and the start of this name?
static bool shows(const std::string& disp, int slot, const std::string& name) {
    const std::string t = past(disp); if (where(t) != slotStr(slot)) return false;
    const std::string rest = squeeze(t.substr(3)), nm = squeeze(name); const size_t n = std::min<size_t>(8, std::min(rest.size(), nm.size()));
    return n > 0 && rest.compare(0, n, nm, 0, n) == 0;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const char* roms = std::getenv("MOONVERB_ROMS");
    if (!roms) { std::printf("SKIP: set MOONVERB_ROMS to a folder holding your firmware images\n"); return 0; }
    const double rate = argc > 1 ? std::atof(argv[1]) : 44100.0; const int block = argc > 2 ? std::atoi(argv[2]) : 512;
    Rig R(rate, block);
    R.p.setPlayConfigDetails(2, 2, rate, block); R.p.loadRomFolder(juce::File(roms)); R.p.prepareToPlay(rate, block); R.start();

    std::printf("power-up and names\n");
    CHECK(R.up(), "unit comes up: \"%s\"", R.v().snap.display.c_str());
    CHECK(R.waitFor(90.0, [&]() { return R.v().namesReady; }), "factory names read");
    R.loaded();
    std::vector<int> factory; { const auto x = R.v(); for (int i = 0; i < MoonVerbProcessor::FACTORY_SLOTS; i++) if (!x.factory[i].empty()) factory.push_back(i); }
    { const auto x = R.v(); std::printf("  %d factory presets; ", (int)factory.size()); for (int i = 0; i < 6 && i < (int)factory.size(); i++) std::printf("%s %s, ", slotStr(factory[i]).c_str(), x.factory[factory[i]].c_str()); std::printf("...\n"); }
    CHECK(factory.size() >= 40, "a full factory bank (%d)", (int)factory.size());
    { const auto x = R.v(); CHECK(!x.user && x.slot == 0 && shows(x.snap.display, 0, x.factory[0]), "running FACTORY 0.0, named as the display shows: \"%s\" / \"%s\"", x.factory[0].c_str(), x.snap.display.c_str()); }

    std::printf("next / previous: every factory preset in turn\n");
    { double worst = 0, sum = 0; int n = 0, bad = 0, silent = 0;
      for (size_t i = 1; i < factory.size(); i++) {
          R.p.stepPreset(+1); const double t = R.loaded(); worst = std::max(worst, t); sum += t; n++;
          const auto x = R.v();
          if (!x.user && x.slot == factory[i] && !x.loading && x.baseValid && !shows(x.snap.display, factory[i], x.factory[factory[i]])) std::printf("  (%s %s is running; the firmware's display shows \"%s\")\n", slotStr(factory[i]).c_str(), x.factory[factory[i]].c_str(), x.snap.display.c_str());
          else if (x.user || x.slot != factory[i] || x.loading || !x.baseValid) { bad++; std::printf("  FAIL: step to %s: running %s %d, display \"%s\", name \"%s\"\n", slotStr(factory[i]).c_str(), x.user ? "USER" : "FACTORY", x.slot, x.snap.display.c_str(), x.factory[factory[i]].c_str()); }
          if (R.outRms.load() < 1e-4) silent++;
      }
      checks++; if (bad) failures++;
      std::printf("  %s %d steps: %d wrong, load time average %.2f s, worst %.2f s\n", bad ? "FAIL:" : "ok:  ", n, bad, sum / std::max(1, n), worst);
      CHECK(silent <= 3, "audio after the loads (%d near silent; resonators and gates can be quiet)", silent); }
    R.p.stepPreset(+1); R.loaded(); { const auto x = R.v(); CHECK(!x.user && x.slot == factory[0], "next after the last factory preset wraps to the first (no user presets yet): %s", slotStr(x.slot).c_str()); }
    R.p.stepPreset(-1); R.loaded(); { const auto x = R.v(); CHECK(!x.user && x.slot == factory.back(), "previous from the first wraps to the last: %s", slotStr(x.slot).c_str()); }
    R.p.stepPreset(-1); R.loaded(); { const auto x = R.v(); CHECK(x.slot == factory[factory.size() - 2], "previous again: %s", slotStr(x.slot).c_str()); }

    std::printf("choosing from the list\n");
    { const int pick = factory[factory.size() / 2]; R.p.loadPreset(false, pick); const double t = R.loaded(); const auto x = R.v();
      CHECK(x.slot == pick && shows(x.snap.display, pick, x.factory[pick]), "choose %s %s: loads with no LOAD, %.2f s: \"%s\"", slotStr(pick).c_str(), x.factory[pick].c_str(), t, x.snap.display.c_str()); }
    { const int n = R.v().slot; R.p.loadPreset(true, 3); R.wait(1.0); CHECK(!R.v().user && R.v().slot == n, "an empty user slot is not loaded"); }

    std::printf("every parameter of a reverb\n");
    { int hall = -1; const auto x0 = R.v(); for (int s : factory) if (x0.factory[s].find("HALL") != std::string::npos) { hall = s; break; } if (hall < 0) hall = factory[0];
      R.p.loadPreset(false, hall); R.loaded(); R.wait(0.6);
      const auto x = R.v(); int cells = 0, wrongLo = 0, wrongHi = 0, noCap = 0, wrongReset = 0, clamped = 0;
      for (int r = 0; r < MoonVerbProcessor::ROWS; r++) for (int c = 0; c < MoonVerbProcessor::COLS; c++) {
          if (!x.snap.valid[r][c] || x.snap.master[r][c]) continue;
          cells++; const juce::String id = "p" + juce::String(r) + juce::String(c); auto* prm = R.p.apvts.getParameter(id);
          const std::string cap0 = x.snap.caption[r][c];
          prm->setValueNotifyingHost(0.f); R.wait(0.5); auto a = R.v(); if (a.snap.word[r][c] != a.snap.lo[r][c]) { wrongLo++; std::printf("    %d.%d %s: min word %d, lo %d\n", r, c, x.snap.name[r][c].c_str(), a.snap.word[r][c], a.snap.lo[r][c]); }
          prm->setValueNotifyingHost(1.f); R.wait(0.5); a = R.v();
          if (a.snap.word[r][c] != a.snap.hi[r][c]) {                                     // the firmware may stop short of the table's top (its own clamp): ask it for more
              const int w1 = a.snap.word[r][c]; R.p.ask([r, c](moonverb::Voice& vv) { vv.control.editNow(r, c, +8); }); R.wait(0.6); a = R.v();
              if (a.snap.word[r][c] == w1) { clamped++; std::printf("    %d.%d %s: the firmware stops at %d (table max %d), as its own edit does\n", r, c, x.snap.name[r][c].c_str(), w1, a.snap.hi[r][c]); }
              else { wrongHi++; std::printf("    %d.%d %s: max word %d, hi %d\n", r, c, x.snap.name[r][c].c_str(), a.snap.word[r][c], a.snap.hi[r][c]); } }
          if (a.snap.caption[r][c] == cap0 && a.snap.word[r][c] != x.snap.word[r][c]) { noCap++; std::printf("    %d.%d %s: caption \"%s\" at word %d and at %d\n", r, c, x.snap.name[r][c].c_str(), cap0.c_str(), x.snap.word[r][c], a.snap.word[r][c]); }
          R.p.resetCell(r, c); R.wait(0.5); a = R.v(); if (a.snap.word[r][c] != x.base[r][c]) { if (wrongReset++ < 2) std::printf("    reset %d.%d %s: word %d, base %d (view base %d), lo %d hi %d, host %.3f; live %d ready %d valid %d epoch %llu loading %d baseValid %d disp \"%s\" status \"%s\"\n", r, c, x.snap.name[r][c].c_str(), a.snap.word[r][c], x.base[r][c], a.base[r][c], a.snap.lo[r][c], a.snap.hi[r][c], prm->getValue(), (int)a.live, (int)a.snap.tableReady, (int)a.snap.valid[r][c], (unsigned long long)a.snap.epoch, (int)a.loading, (int)a.baseValid, a.snap.display.c_str(), a.status.c_str()); }
      }
      { int vv = 0; for (int r = 0; r < 5; r++) for (int c = 0; c < 9; c++) vv += x.snap.valid[r][c]; std::printf("  (%s: %d plain parameters; tableReady %d, valid %d, epoch %llu, display \"%s\", loading %d base %d)\n", x.factory[hall].c_str(), cells, (int)x.snap.tableReady, vv, (unsigned long long)x.snap.epoch, x.snap.display.c_str(), (int)x.loading, (int)x.baseValid); }
      CHECK(cells >= 15, "the program has its parameters (%d)", cells);
      CHECK(wrongLo == 0 && wrongHi == 0, "every knob reaches the firmware's min and max (%d / %d wrong)", wrongLo, wrongHi);
      CHECK(noCap == 0, "every caption follows its value (%d did not)", noCap);
      CHECK(wrongReset == 0, "double-click reset puts every one back (%d wrong)", wrongReset);
      R.wait(0.5); CHECK(!R.v().modified, "and the preset is no longer EDITED");
      // a master moves its children, and their knobs follow
      int mr = -1, mc = -1; for (int r = 1; r < MoonVerbProcessor::ROWS && mr < 0; r++) for (int c = 0; c < MoonVerbProcessor::COLS; c++) if (x.snap.valid[r][c] && x.snap.master[r][c]) { mr = r; mc = c; break; }
      if (mr >= 0) {
          int child = -1; for (int c = 0; c < MoonVerbProcessor::COLS; c++) if (c != mc && x.snap.valid[mr][c] && !x.snap.master[mr][c]) { child = c; break; }
          const int w0 = x.snap.word[mr][child];
          R.p.apvts.getParameter("p" + juce::String(mr) + juce::String(mc))->setValueNotifyingHost(0.95f); R.wait(1.5);
          const auto b = R.v(); const float hp = R.p.apvts.getRawParameterValue("p" + juce::String(mr) + juce::String(child))->load();
          const float want = (float)(b.snap.word[mr][child] - b.snap.lo[mr][child]) / (float)std::max(1, b.snap.hi[mr][child] - b.snap.lo[mr][child]);
          CHECK(b.snap.word[mr][child] != w0, "master %d.%d %s moves %d.%d (%d -> %d)", mr, mc, x.snap.name[mr][mc].c_str(), mr, child, w0, b.snap.word[mr][child]);
          CHECK(std::fabs(hp - juce::jlimit(0.f, 1.f, want)) < 0.02f, "and that knob follows (%.3f vs %.3f)", hp, want);
          CHECK(b.modified, "the preset shows EDITED");
          R.p.resetCell(mr, mc); R.wait(1.5);
      } else std::printf("  (no master in this program)\n"); }

    std::printf("one step (the arrow keys), RATE\n");
    { const auto x = R.v(); int r0 = -1, c0 = -1; for (int r = 0; r < 5 && r0 < 0; r++) for (int c = 0; c < 9; c++) if (x.snap.valid[r][c] && !x.snap.master[r][c] && x.snap.word[r][c] > x.snap.lo[r][c] + 12 && x.snap.word[r][c] < x.snap.hi[r][c] - 12) { r0 = r; c0 = c; break; }
      const int w0 = x.snap.word[r0][c0];
      R.p.stepCell(r0, c0, +1); R.wait(0.6); const int w1 = R.v().snap.word[r0][c0];
      R.p.stepCell(r0, c0, -10); R.wait(0.6); const int w2 = R.v().snap.word[r0][c0];
      CHECK(w1 == w0 + 1 && w2 == w0 - 9, "%d.%d %s: +1 -> %d, -10 -> %d (from %d)", r0, c0, x.snap.name[r0][c0].c_str(), w1, w2, w0);
      R.p.resetCell(r0, c0); R.wait(0.6); }
    { int bpmSlot = -1; const auto x0 = R.v(); for (int s : factory) if (x0.factory[s].find("BPM") != std::string::npos) { bpmSlot = s; break; }
      R.p.loadPreset(false, bpmSlot); R.loaded(); R.wait(0.5); auto x = R.v();
      CHECK(x.rateRow >= 0, "%s has a RATE (cell %d.%d, word %d \"%s\")", x.factory[bpmSlot].c_str(), x.rateRow, x.rateCol, x.rateRow >= 0 ? x.snap.word[x.rateRow][x.rateCol] : -1, x.rateRow >= 0 ? x.snap.caption[x.rateRow][x.rateCol].c_str() : "");
      if (x.rateRow >= 0) {
          const int rr = x.rateRow, rc = x.rateCol, wHz = x.snap.word[rr][rc];
          R.p.setRateMode(MoonVerbProcessor::RATE_SYNC); R.wait(1.2); x = R.v();
          CHECK(x.snap.word[rr][rc] == MoonVerbProcessor::rateWord(x.hostBpm, x.rateDiv) && x.snap.caption[rr][rc].find(std::to_string((int)std::lround(x.hostBpm / 1.0))) != std::string::npos,
                "SYNC %s at %.0f BPM: RATE word %d, the firmware says \"%s\"", MoonVerbProcessor::rateDivName(x.rateDiv), x.hostBpm, x.snap.word[rr][rc], x.snap.caption[rr][rc].c_str());
          R.p.stepRateDiv(+1); R.wait(1.2); x = R.v();
          CHECK(x.snap.word[rr][rc] == MoonVerbProcessor::rateWord(x.hostBpm, x.rateDiv), "next note value %s: word %d \"%s\"", MoonVerbProcessor::rateDivName(x.rateDiv), x.snap.word[rr][rc], x.snap.caption[rr][rc].c_str());
          R.p.stepRateDiv(+1); R.p.stepRateDiv(+1); R.p.stepRateDiv(+1); R.wait(1.2); x = R.v();
          CHECK(MoonVerbProcessor::rateReachable(x.hostBpm, x.rateDiv), "note values the firmware cannot play are skipped (%s, %.0f BPM)", MoonVerbProcessor::rateDivName(x.rateDiv), MoonVerbProcessor::rateBpm(x.hostBpm, x.rateDiv));
          R.p.setRateMode(MoonVerbProcessor::RATE_HZ); R.p.resetCell(rr, rc); R.wait(1.2); x = R.v();
          CHECK(x.snap.word[rr][rc] == wHz, "HZ mode leaves RATE to its knob (back to %d)", x.snap.word[rr][rc]);
      } }

    std::printf("store\n");
    { const int running = R.v().slot; const std::string nm = R.v().factory[running];
      R.p.storePreset(7); R.loaded(10.0); R.wait(1.0); auto x = R.v();
      CHECK(x.user && x.slot == 7 && x.reg[7] == nm, "STORE into empty user 0.7: \"%s\" (running %s)", x.reg[7].c_str(), nm.c_str());
      R.p.loadPreset(false, factory[0]); R.loaded();
      R.p.storePreset(7); R.loaded(10.0); R.wait(1.0); x = R.v();
      CHECK(x.reg[7] == x.factory[factory[0]], "STORE over it: \"%s\"", x.reg[7].c_str());
      R.p.loadPreset(false, factory[1]); R.loaded(); R.p.loadPreset(true, 7); R.loaded(); x = R.v();
      CHECK(x.user && x.slot == 7 && squeeze(x.snap.display).find(squeeze(x.reg[7]).substr(0, 6)) != std::string::npos, "load user 0.7: \"%s\"", x.snap.display.c_str());
      R.p.loadPreset(false, factory.back()); R.loaded(); R.p.stepPreset(+1); R.loaded(); x = R.v();
      CHECK(x.user && x.slot == 7, "next after the last factory preset goes to the user presets (user %d slot %d)", (int)x.user, x.slot);
      R.p.stepPreset(+1); R.loaded(); x = R.v(); CHECK(!x.user && x.slot == factory[0], "and next after the last user preset wraps to factory %s", slotStr(x.slot).c_str()); }

    std::printf("the host's program list\n");
    { CHECK(R.p.getNumPrograms() == 120, "120 programs (%d)", R.p.getNumPrograms());
      CHECK(R.p.getProgramName(13).contains(juce::String(R.v().factory[13]).substring(0, 6)) || R.v().factory[13].empty(), "program 13: \"%s\"", R.p.getProgramName(13).toRawUTF8());
      CHECK(R.p.getProgramName(77).contains("USER 0.7"), "program 77: \"%s\"", R.p.getProgramName(77).toRawUTF8());
      const int pick = factory[5]; R.p.setCurrentProgram(pick); R.loaded(); CHECK(R.v().slot == pick && R.p.getCurrentProgram() == pick, "setCurrentProgram(%d) loads it", pick); }

    std::printf("bypass, power\n");
    { R.p.apvts.getParameter("bypass")->setValueNotifyingHost(1.f); CHECK(R.waitFor(3.0, [&]() { return R.v().snap.bypassed; }), "BYPASS on");
      int flips = 0; bool last = true; for (int i = 0; i < 60; i++) { R.wait(0.05); const bool b = R.v().snap.bypassed; if (b != last) flips++; last = b; } CHECK(flips == 0, "and steady (%d changes)", flips);
      R.p.apvts.getParameter("bypass")->setValueNotifyingHost(0.f); CHECK(R.waitFor(3.0, [&]() { return !R.v().snap.bypassed; }), "BYPASS off");
      const int slot = R.v().slot; R.p.setPower(false); R.wait(0.5); CHECK(!R.v().live, "power off");
      R.p.setPower(true); CHECK(R.up(), "power on"); R.loaded(); CHECK(R.v().slot == slot && R.v().baseValid, "same preset after power on (%d)", R.v().slot); }

    std::printf("state\n");
    { R.p.loadPreset(false, factory[3]); R.loaded(); R.p.apvts.getParameter("p06")->setValueNotifyingHost(0.2f); R.wait(1.5);
      const auto a = R.v(); juce::MemoryBlock mb; R.p.getStateInformation(mb);
      std::printf("  (saving: bypassed %d, host bypass %.0f)\n", (int)a.snap.bypassed, R.p.apvts.getRawParameterValue("bypass")->load());
      // as Live does it: the plug-in is prepared (and starts powering up on empty memory) and runs, THEN the host hands it the saved state
      Rig Q(rate, block); Q.p.setPlayConfigDetails(2, 2, rate, block); Q.p.prepareToPlay(rate, block); Q.start(); Q.wait(0.3); Q.p.setStateInformation(mb.getData(), (int)mb.getSize());
      CHECK(Q.up(), "restored instance comes up"); Q.loaded(); Q.waitFor(30.0, [&]() { return Q.v().namesReady; });
      const auto b = Q.v();
      CHECK(!b.user && b.slot == factory[3], "same preset (%s)", slotStr(b.slot).c_str());
      CHECK(b.snap.word[0][6] == a.snap.word[0][6], "same values (0.6 word %d vs %d)", b.snap.word[0][6], a.snap.word[0][6]);
      CHECK(b.namesReady && b.factory[factory[3]] == a.factory[factory[3]], "names known at once (cached)");
      CHECK(b.reg[7] == a.reg[7] && !b.reg[7].empty(), "with its user presets (0.7 \"%s\")", b.reg[7].c_str());
      Q.wait(1.5); CHECK(!Q.v().snap.bypassed && Q.outRms.load() > 1e-4, "live, with audio (bypassed %d, rms %.5f)", (int)Q.v().snap.bypassed, Q.outRms.load()); }

    std::printf("the editor\n");
    { std::unique_ptr<juce::AudioProcessorEditor> ed(R.p.createEditor()); R.wait(0.5);
      juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
      const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile("panel.png"); out.deleteFile();
      { juce::FileOutputStream fo(out); if (fo.openedOk()) juce::PNGImageFormat().writeImageToStream(img, fo); }
      CHECK(out.existsAsFile(), "snapshot %s", out.getFullPathName().toRawUTF8()); }

    std::printf(failures ? "FAILED %d of %d\n" : "ok: all %d checks\n", failures ? failures : checks, checks);
    return failures ? 1 : 0;
}
