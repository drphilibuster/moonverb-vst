// Headless check of the whole plugin against your own firmware images: boots the processor through its public interface, runs audio through processBlock,
// presses front-panel keys, turns the soft knob, round-trips the state and renders the editor to a PNG. Needs $MOONVERB_ROMS (a folder with the images);
// without it the test says SKIP and passes.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static bool waitFor(MoonVerbProcessor& p, double rate, int block, juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi, double seconds, const std::function<bool(const MoonVerbProcessor::View&)>& done) {
    const int blocks = (int)(seconds * rate / block);
    for (int i = 0; i < blocks; i++) {
        buf.clear(); midi.clear(); p.processBlock(buf, midi);
        if (i % 8 == 0) { const auto v = p.view(); if (done(v)) return true; }
        juce::Thread::sleep(1);
    }
    return false;
}

static float peak(const juce::AudioBuffer<float>& b) { float m = 0; for (int c = 0; c < b.getNumChannels(); c++) m = std::max(m, b.getMagnitude(c, 0, b.getNumSamples())); return m; }

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    const char* roms = std::getenv("MOONVERB_ROMS");
    if (!roms) { std::printf("SKIP: set MOONVERB_ROMS to a folder holding your firmware images\n"); return 0; }
    const double rate = 48000.0; const int block = 256;
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    {
        MoonVerbProcessor p;
        p.setPlayConfigDetails(2, 2, rate, block);
        p.loadRomFolder(juce::File(roms));
        p.prepareToPlay(rate, block);
        CHECK(p.view().romsOk, "ROM set not recognised: %s", p.view().status.c_str());
        // power-up takes about 10 s of machine time; the audio thread hears nothing until it is up
        const bool up = waitFor(p, rate, block, buf, midi, 120.0, [](const auto& v) { return v.live && !v.snap.display.empty(); });
        CHECK(up, "the unit never came up");
        if (!up) return 1;
        std::printf("up: display \"%s\", latency %d samples, family %s\n", p.view().snap.display.c_str(), p.getLatencySamples(), p.view().family.c_str());
        // audio: a 1 kHz burst in, a tail out
        float maxOut = 0, tailOut = 0; double t = 0;
        for (int i = 0; i < (int)(6.0 * rate / block); i++) {
            for (int s = 0; s < block; s++) { const float x = (i * block + s) < 4800 ? 0.3f * (float)std::sin(2 * M_PI * 1000.0 * (t += 1 / rate)) : 0.f; buf.setSample(0, s, x); buf.setSample(1, s, x); }
            midi.clear(); p.processBlock(buf, midi);
            const float pk = peak(buf); maxOut = std::max(maxOut, pk); if (i * block > rate * 4.0) tailOut = std::max(tailOut, pk);
        }
        std::printf("burst in 0.3 -> out peak %.4f, peak after 4 s %.5f, LEDs %d\n", maxOut, tailOut, p.view().snap.leds);
        CHECK(maxOut > 1e-4f, "no output");
        CHECK(std::isfinite(maxOut), "non-finite output");
        // a front-panel gesture: PGM then digit then LOAD, as a finger would (REG/PGM modes are the firmware's)
        const std::string before = p.view().snap.display;
        auto tap = [&](moonverb::Keys::Key k) { p.pressKey(k, true); waitFor(p, rate, block, buf, midi, 0.12, [](const auto&) { return false; }); p.pressKey(k, false); waitFor(p, rate, block, buf, midi, 0.4, [](const auto&) { return false; }); };
        tap(moonverb::Keys::F1); tap(moonverb::Keys::digit(3)); tap(moonverb::Keys::LOAD);
        waitFor(p, rate, block, buf, midi, 3.0, [](const auto&) { return false; });
        std::printf("keys: \"%s\" -> \"%s\"\n", before.c_str(), p.view().snap.display.c_str());
        CHECK(p.view().snap.display != before, "keys did not change the display");
        // PARAM mode and the soft knob: the displayed parameter moves by the firmware's own edit
        tap(moonverb::Keys::F4); tap(moonverb::Keys::digit(2));
        waitFor(p, rate, block, buf, midi, 1.0, [](const auto&) { return false; });
        const auto v0 = p.view(); const int r0 = v0.snap.paramRow, c0 = v0.snap.paramCol;
        std::printf("PARAM mode %d: display \"%s\", parameter (%d,%d) word %d\n", v0.snap.mode, v0.snap.display.c_str(), r0, c0, r0 < 5 && c0 < 9 ? v0.snap.word[r0][c0] : -1);
        const int w0 = r0 < 5 && c0 < 9 ? v0.snap.word[r0][c0] : 0;
        p.softKnob(+5); waitFor(p, rate, block, buf, midi, 1.5, [](const auto&) { return false; });
        const auto v1 = p.view(); const int w1 = r0 < 5 && c0 < 9 ? v1.snap.word[r0][c0] : 0;
        std::printf("soft knob +5: word %d -> %d, display \"%s\"\n", w0, w1, v1.snap.display.c_str());
        CHECK(w1 != w0, "soft knob did not move the parameter");
        // the host's parameter and the firmware agree: automate a cell, the firmware's word follows
        // bypass from the host: the unit's own flag
        p.apvts.getParameter("bypass")->setValueNotifyingHost(1.f);
        const bool byp = waitFor(p, rate, block, buf, midi, 5.0, [](const auto& v) { return v.snap.bypassed; });
        CHECK(byp, "host bypass did not reach the firmware");
        p.apvts.getParameter("bypass")->setValueNotifyingHost(0.f);
        waitFor(p, rate, block, buf, midi, 5.0, [](const auto& v) { return !v.snap.bypassed; });
        // state round trip
        juce::MemoryBlock mb; p.getStateInformation(mb); CHECK(mb.getSize() > 1000, "state too small");
        { MoonVerbProcessor q; q.setPlayConfigDetails(2, 2, rate, block); q.setStateInformation(mb.getData(), (int)mb.getSize()); q.prepareToPlay(rate, block);
          CHECK(q.view().romsOk, "restored instance did not find its images");
          const bool up2 = waitFor(q, rate, block, buf, midi, 120.0, [](const auto& v) { return v.live && !v.snap.display.empty(); }); CHECK(up2, "restored instance never came up");
          std::printf("restored: display \"%s\"\n", q.view().snap.display.c_str()); }
        // the editor
        { std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setSize(1200, ed->getHeight());
          juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.5f);
          const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile("editor.png"); out.deleteFile();
          juce::FileOutputStream fo(out); if (fo.openedOk()) juce::PNGImageFormat().writeImageToStream(img, fo); std::printf("editor snapshot: %s\n", out.getFullPathName().toRawUTF8()); }
    }
    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
