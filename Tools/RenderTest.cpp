// Headless end-to-end test: plays one audio file through Ground Shake as "your mix"
// with a fake host transport, loads another as the reference, and saves a PNG of the UI.
// Usage: GroundShakeRenderTest yourMix.wav reference.wav out.png [listenB]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

struct FakeHead : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (true);
        return p;
    }
};

static float rms (const juce::AudioBuffer<float>& b)
{
    double s = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) s += b.getSample (c, i) * b.getSample (c, i);
    return (float) std::sqrt (s / (b.getNumChannels() * (double) b.getNumSamples()));
}

int main (int argc, char* argv[])
{
    if (argc < 4) { std::printf ("usage: mix.wav ref.wav out.png [listenB]\n"); return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    const bool listen = argc > 4;

    gs::ReferenceTrack mix;  juce::String err;
    if (! gs::loadReference (juce::File (juce::String::fromUTF8 (argv[1])), mix, err)) { std::printf ("mix: %s\n", err.toRawUTF8()); return 1; }
    std::printf ("your mix: %s  (tempo used for the fake host: %.2f BPM)\n", mix.name.toRawUTF8(), mix.detectedBpm);

    GroundShakeProcessor proc;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, mix.sampleRate, block);
    proc.prepareToPlay (mix.sampleRate, block);
    FakeHead head;  head.bpm = mix.detectedBpm;
    proc.setPlayHead (&head);

    proc.loadReferenceAsync (juce::File (juce::String::fromUTF8 (argv[2])));
    for (int i = 0; i < 400 && (proc.isLoading() || ! proc.hasReference()); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    if (! proc.hasReference()) { std::printf ("reference failed: %s\n", proc.getLoadError().toRawUTF8()); return 1; }
    std::printf ("reference: %s  BPM %.2f  bars %d  window bar %d\n", proc.getReference()->name.toRawUTF8(),
                 proc.getBpm(), proc.getNumReferenceBars(), proc.getWindowStartBar() + 1);
    proc.setListenToReference (listen);

    // play the mix from its first bar line for 8 bars (or the whole clip), twice
    const int start = (int) (mix.firstDownbeatSec * mix.sampleRate);
    const int len = juce::jmin (mix.audio.getNumSamples() - start, (int) (8 * 240.0 / mix.detectedBpm * mix.sampleRate));
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    float inR = 0, outR = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int pos = 0; pos + block <= len; pos += block)
        {
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, mix.audio, c, start + pos, block);
            const float a = rms (buf);
            head.ppq = (pos + pass * len) / mix.sampleRate * head.bpm / 60.0;
            proc.processBlock (buf, midi);
            if (pass == 1) { inR += a * a; outR += rms (buf) * rms (buf); }
        }
    std::printf ("level check (pass 2): input RMS %.4f  output RMS %.4f  (%s)\n", std::sqrt (inR), std::sqrt (outR),
                 listen ? "B = reference, should be close to input = level-matched" : "A = untouched, must be equal");

    const auto live = proc.getLiveProfile();
    std::printf ("shake: you %.2f dB  ref %.2f dB  diff %+.2f dB\n", live.shakeDb(),
                 proc.getReferenceProfile().shakeDb(), live.shakeDb() - proc.getReferenceProfile().shakeDb());
    std::printf ("you  steps:");
    for (int s = 0; s < 16; ++s) std::printf ("%s%5.1f", s % 4 == 0 ? " |" : "", live.stepDb (s));
    std::printf ("\nref  steps:");
    for (int s = 0; s < 16; ++s) std::printf ("%s%5.1f", s % 4 == 0 ? " |" : "", proc.getReferenceProfile().stepDb (s));
    std::printf ("\n");

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1000, 700);
    // let the feet animate while a little more audio plays
    for (int i = 0; i < 20; ++i)
    {
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, mix.audio, c, start + (i * block) % (len - block), block);
        head.ppq += block / mix.sampleRate * head.bpm / 60.0;
        proc.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (17);
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File outFile (juce::String::fromUTF8 (argv[3]));
    outFile.deleteFile();
    juce::FileOutputStream os (outFile);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("saved %s\n", argv[3]);
    ed.reset();
    return 0;
}
