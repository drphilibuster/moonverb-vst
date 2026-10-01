// Loads the installed plugin binaries the way a DAW does (JUCE's own plugin hosting): scans for MoonVerb in the user's VST3 and AU folders, instantiates each,
// checks its parameters and buses, runs audio and MIDI through it and round-trips its state. Needs the plugins installed (see README); needs no firmware images.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioPluginFormatManager fm; fm.addFormat(new juce::VST3PluginFormat()); fm.addFormat(new juce::AudioUnitPluginFormat());
    int found = 0;
    for (int f = 0; f < fm.getNumFormats(); f++) {
        juce::AudioPluginFormat& fmt = *fm.getFormat(f);
        juce::StringArray paths = fmt.searchPathsForPlugins(juce::FileSearchPath(), true, false);
        juce::OwnedArray<juce::PluginDescription> types;
        for (const auto& p : paths) if (p.containsIgnoreCase("MoonVerb")) fmt.findAllTypesForFile(types, p);
        for (auto* d : types) {
            found++;
            std::printf("%s: %s by %s (%s), %d in / %d out\n", fmt.getName().toRawUTF8(), d->name.toRawUTF8(), d->manufacturerName.toRawUTF8(), d->fileOrIdentifier.toRawUTF8(), d->numInputChannels, d->numOutputChannels);
            juce::String err;
            std::unique_ptr<juce::AudioPluginInstance> p = fm.createPluginInstance(*d, 48000.0, 512, err);
            CHECK(p != nullptr, "could not instantiate: %s", err.toRawUTF8());
            if (!p) continue;
            const int n = p->getParameters().size();
            std::printf("  %d parameters, bypass parameter %s, accepts MIDI %d\n", n, p->getBypassParameter() ? p->getBypassParameter()->getName(32).toRawUTF8() : "none", (int)p->acceptsMidi());
            CHECK(n == 4 + 45, "expected 49 parameters, found %d", n);
            CHECK(p->acceptsMidi(), "no MIDI input");
            p->setPlayConfigDetails(2, 2, 48000.0, 512); p->prepareToPlay(48000.0, 512);
            juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0); midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 10);
            for (int i = 0; i < 40; i++) { buf.clear(); p->processBlock(buf, midi); midi.clear(); }
            for (int c = 0; c < 2; c++) for (int s = 0; s < 512; s++) CHECK(std::isfinite(buf.getSample(c, s)), "non-finite sample");
            juce::MemoryBlock mb; p->getStateInformation(mb); CHECK(mb.getSize() > 100, "state too small (%d)", (int)mb.getSize());
            p->setStateInformation(mb.getData(), (int)mb.getSize());
            if (auto* b = p->getBypassParameter()) { b->setValueNotifyingHost(1.f); b->setValueNotifyingHost(0.f); }
            juce::AudioProcessorEditor* ed = p->createEditorIfNeeded(); CHECK(ed != nullptr, "no editor"); if (ed) std::printf("  editor %d x %d\n", ed->getWidth(), ed->getHeight());
            p->editorBeingDeleted(ed); delete ed; p->releaseResources();
        }
    }
    CHECK(found >= 1, "no MoonVerb found in the plug-in folders");
    std::printf(failures ? "FAILED (%d)\n" : "ok (%d plug-in(s) hosted)\n", failures ? failures : found);
    return failures ? 1 : 0;
}
