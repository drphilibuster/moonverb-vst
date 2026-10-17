// The plugin as a DAW runs it: the audio callback on its own thread, in real time, while the message thread dispatches (the processor's timer, which hands the
// knobs the firmware moved back to the host, only runs then). plugin_test drives processBlock from the message thread with no dispatch loop, so it never sees
// the two threads interleave. Needs $MOONVERB_ROMS; without it the test says SKIP and passes.
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include "PluginProcessor.h"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const char* roms = std::getenv("MOONVERB_ROMS");
    if (!roms) { std::printf("SKIP: set MOONVERB_ROMS to a folder holding your firmware images\n"); return 0; }
    const double rate = argc > 1 ? std::atof(argv[1]) : 48000.0; const int block = argc > 2 ? std::atoi(argv[2]) : 256;
    const double seconds = 14.0;
    MoonVerbProcessor p;
    p.setPlayConfigDetails(2, 2, rate, block);
    p.loadRomFolder(juce::File(roms));
    p.prepareToPlay(rate, block);
    CHECK(p.view().romsOk, "ROM set not recognised: %s", p.view().status.c_str());

    // wait for the power-up on the message thread, with the audio thread already running silence (a DAW never stops calling)
    std::atomic<bool> stop{false}; std::atomic<int> phase{0};
    const int windows = (int)(seconds / 0.5); std::vector<double> rms(windows, 0.0); std::atomic<double> busyAvg{0}, busyMax{0}; std::atomic<int> over50{0}, over80{0}, blocksLive{0};
    std::thread audio([&]() {
        juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; long long n = 0, t0 = -1; double acc = 0; long long accN = 0;
        auto next = std::chrono::steady_clock::now();
        while (!stop.load()) {
            const bool live = phase.load() == 1;
            if (live && t0 < 0) t0 = n;
            for (int s = 0; s < block; s++) {
                const long long i = n + s; const float x = live ? 0.3f * (float)std::sin(2 * M_PI * 440.0 * i / rate) * ((i / (long long)(rate * 0.25)) % 2 ? 0.f : 1.f) : 0.f;
                buf.setSample(0, s, x); buf.setSample(1, s, x);
            }
            midi.clear();
            const auto b0 = std::chrono::steady_clock::now(); p.processBlock(buf, midi);
            const double load = std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count() / (block / rate);
            if (live) { busyAvg = busyAvg * 0.999 + load * 0.001; if (load > busyMax) busyMax = load; blocksLive++; if (load > 0.5) over50++; if (load > 0.8) { over80++; if (std::getenv("MV_SPIKES")) std::printf("spike %.0f%% at %.3f s\n", 100 * load, (n - t0) / rate); } }
            if (live) {
                for (int s = 0; s < block; s++) { const double y = buf.getSample(0, s); acc += y * y; }
                accN += block;
                const int w = (int)((n - t0) / (rate * 0.5));
                if (accN >= rate * 0.5 && w - 1 < windows && w >= 1) { rms[w - 1] = std::sqrt(acc / accN); acc = 0; accN = 0; }
            }
            n += block;
            next += std::chrono::microseconds((long long)(1e6 * block / rate));
            std::this_thread::sleep_until(next);
        }
    });
    bool up = false;
    for (int i = 0; i < 1200 && !up; i++) { juce::MessageManager::getInstance()->runDispatchLoopUntil(100); const auto v = p.view(); up = v.live && !v.snap.display.empty() && v.snap.tableReady; }
    CHECK(up, "the unit never came up");
    std::printf("up: \"%s\"\n", p.view().snap.display.c_str());
    const uint64_t e0 = p.view().snap.epoch;
    phase = 1;
    for (int i = 0; i < (int)(seconds * 10) + 5; i++) juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    stop = true; audio.join();
    const auto v = p.view();
    std::printf("after %.0f s: \"%s\", program epoch %llu -> %llu, bypassed %d\n", seconds, v.snap.display.c_str(), (unsigned long long)e0, (unsigned long long)v.snap.epoch, (int)v.snap.bypassed);
    std::printf("audio thread load: average %.0f%% of real time, worst block %.0f%%, %d of %d blocks over 50%%, %d over 80%%\n", 100 * busyAvg.load(), 100 * busyMax.load(), over50.load(), blocksLive.load(), over80.load());
    std::printf("output RMS per 0.5 s (input 0.3 sine bursts, RMS ~0.15):");
    int dead = 0;
    for (int w = 0; w < windows; w++) { std::printf(" %.3f", rms[w]); if (w >= 2 && rms[w] < 0.01) dead++; }
    std::printf("\n");
    CHECK(dead == 0, "%d of %d half-second windows near silent", dead, windows - 2);
    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
