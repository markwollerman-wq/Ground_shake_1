#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Resample a stereo buffer to the host rate (done once per load, off the audio thread)
    std::shared_ptr<const juce::AudioBuffer<float>> resampleTo (const juce::AudioBuffer<float>& in,
                                                               double fromRate, double toRate)
    {
        auto out = std::make_shared<juce::AudioBuffer<float>>();
        if (std::abs (fromRate - toRate) < 0.5)
        {
            out->makeCopyOf (in);
            return out;
        }
        const double ratio = fromRate / toRate;
        const int outLen = (int) std::floor (in.getNumSamples() / ratio) - 4;
        out->setSize (2, juce::jmax (1, outLen));
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, in.getReadPointer (ch), out->getWritePointer (ch), out->getNumSamples());
        }
        return out;
    }
}

//==============================================================================
GroundShakeProcessor::GroundShakeProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    startTimer (500);
}

GroundShakeProcessor::~GroundShakeProcessor()
{
    alive->store (false);
    stopTimer();
    pool.removeAllJobs (true, 10000);
    livePlayback.store (nullptr);
}

bool GroundShakeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void GroundShakeProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    splitter.prepare (sampleRate);
    meter.reset();
    subPow = 0.0f;
    abFade = 0.0f;

    // reference must play at the host rate
    if (reference != nullptr && std::abs (resampledRate - sampleRate) > 0.5)
        requestResample (sampleRate);
}

void GroundShakeProcessor::requestResample (double sampleRate)
{
    auto ref = reference;
    auto flag = alive;
    pool.addJob ([this, ref, flag, sampleRate]
    {
        auto res = resampleTo (ref->audio, ref->sampleRate, sampleRate);
        juce::MessageManager::callAsync ([this, ref, flag, res, sampleRate]
        {
            if (! flag->load() || reference != ref) return;
            resampled = res;
            resampledRate = sampleRate;
            rebuildWindow();
        });
    });
}

//==============================================================================
void GroundShakeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    if (n == 0 || numCh == 0) return;

    // --- where is the host?
    double bpmNow = hostBpm.load(), ppq = 0.0;
    bool havePpq = false, playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpmNow = *b;
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; havePpq = true; }
        }
    if (! havePpq) playing = true;                       // standalone: free-running clock
    bpmNow = juce::jlimit (40.0, 300.0, bpmNow);
    const double stepsPerSample = bpmNow / 60.0 * 4.0 / sr;
    const double startSteps = havePpq ? ppq * 4.0 : freeRunSteps;

    // --- analyse YOUR mix (the input, before anything is replaced)
    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getReadPointer (numCh > 1 ? 1 : 0);
    const float subCoef = (float) std::exp (-1.0 / (0.03 * sr));     // ~30 ms for the shake
    const double msCoef = std::exp (-1.0 / (3.0 * sr));              // ~3 s for loudness
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        float feel, punch;
        splitter.process (m, feel, punch);
        const float fs = feel * feel, ps = punch * punch, ms = m * m;
        if (playing)
            meter.addSample (startSteps + i * stepsPerSample, fs, ps, ms);
        subPow = (fs + gs::kPunchWeight * ps) + subCoef * (subPow - (fs + gs::kPunchWeight * ps));
        mixMeanSquare = ms + msCoef * (mixMeanSquare - ms);
    }
    if (! havePpq) freeRunSteps += n * stepsPerSample;
    const double endSteps = startSteps + n * stepsPerSample;

    liveSubDb.store (gs::powerToDb (subPow / (float) juce::jmax (1.0e-9, mixMeanSquare)));
    hostSteps.store (endSteps);
    hostBpm.store (bpmNow);
    hostPlaying.store (playing && havePpq);

    // publish the step profile once per 16th
    const long slot = (long) std::floor (endSteps);
    if (slot != lastPublishedSlot)
    {
        const juce::SpinLock::ScopedTryLockType tl (profileLock);
        if (tl.isLocked())
        {
            publishedLive = meter.profile();
            publishedHasData = publishedLive.valid;
            lastPublishedSlot = slot;
        }
    }

    // --- A/B: level-matched reference playback
    auto* pb = livePlayback.load();
    const bool wantB = listenB.load() && pb != nullptr && pb->audio != nullptr && pb->windowLength > 0;
    if (! wantB && abFade <= 0.0f)
    {
        wasListening = false;
        refPosSec.store (-1.0);
        return;                                          // audio passes through untouched
    }
    if (pb == nullptr || pb->audio == nullptr || pb->windowLength <= 0)
    {
        abFade = 0.0f;
        return;
    }

    const auto& ra = *pb->audio;
    const int wStart = pb->windowStart, wLen = pb->windowLength, total = ra.getNumSamples();

    // line the reference loop up with your 8-bar loop when the tempos match; otherwise
    // it runs at its own speed (no time-stretching, so the sub stays untouched)
    const bool sameTempo = std::abs (pb->bpm - bpmNow) < 0.5;
    const auto loopPosSamples = [&] (double steps)
    {
        double s = std::fmod (steps, (double) gs::kLoopSteps);
        if (s < 0) s += gs::kLoopSteps;
        return (int) (s / 4.0 * 60.0 / pb->bpm * sr) % juce::jmax (1, wLen);
    };
    if (wantB && ! wasListening)
        refReadPos = sameTempo && havePpq ? loopPosSamples (startSteps) : 0;
    else if (sameTempo && havePpq && playing && std::abs (refReadPos - loopPosSamples (startSteps)) > (int) (0.05 * sr))
        refReadPos = loopPosSamples (startSteps);
    wasListening = wantB;

    const float gain = juce::jlimit (0.063f, 4.0f,
                                     std::sqrt ((float) mixMeanSquare / juce::jmax (1.0e-9f, pb->windowMeanSquare)));
    const float fadeStep = (float) (1.0 / (0.015 * sr));
    const float* rl = ra.getReadPointer (0);
    const float* rr = ra.getReadPointer (1);
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (numCh > 1 ? 1 : 0);
    const bool advance = playing || ! havePpq;

    for (int i = 0; i < n; ++i)
    {
        abFade = juce::jlimit (0.0f, 1.0f, abFade + (wantB ? fadeStep : -fadeStep));
        const int idx = juce::jlimit (0, total - 1, wStart + refReadPos);
        const float a = 1.0f - abFade, b = abFade * gain;
        outL[i] = outL[i] * a + rl[idx] * b;
        if (numCh > 1) outR[i] = outR[i] * a + rr[idx] * b;
        if (advance && ++refReadPos >= wLen) refReadPos = 0;
    }
    refPosSec.store ((wStart + refReadPos) / sr);
}

//==============================================================================
gs::StepProfile GroundShakeProcessor::getLiveProfile() const
{
    const juce::SpinLock::ScopedLockType sl (profileLock);
    return publishedLive;
}

bool GroundShakeProcessor::liveHasData() const
{
    const juce::SpinLock::ScopedLockType sl (profileLock);
    return publishedHasData;
}

float GroundShakeProcessor::getRefSubDbNow() const
{
    if (reference == nullptr || ! refProfile.valid) return -60.0f;
    const auto& ref = *reference;
    const double hopSec = ref.hop / ref.sampleRate;

    double t = refPosSec.load();                         // where the reference is playing
    if (t < 0)                                           // not playing: follow your loop's step
    {
        const double steps = std::fmod (hostSteps.load(), (double) gs::kLoopSteps);
        t = getGridStartSec() + windowStartBar * 240.0 / bpm + steps / 4.0 * 60.0 / bpm;
    }
    const auto f = (size_t) juce::jmax (0.0, t / hopSec);
    if (f >= ref.feelEnv.size()) return -60.0f;
    // short average around the position, like the live meter's ~30 ms
    float p = 0;
    int c = 0;
    for (size_t i = f > 3 ? f - 3 : 0; i <= f && i < ref.feelEnv.size(); ++i, ++c)
        p += ref.feelEnv[i] + gs::kPunchWeight * ref.punchEnv[i];
    return gs::powerToDb ((p / juce::jmax (1, c)) / juce::jmax (1.0e-9f, refProfile.fullTotal));
}

//==============================================================================
void GroundShakeProcessor::loadReferenceAsync (const juce::File& file)
{
    loading.store (true);
    loadError.clear();
    if (onReferenceChanged) onReferenceChanged();

    auto flag = alive;
    const double hostRate = sr;
    pool.addJob ([this, file, flag, hostRate]
    {
        auto ref = std::make_shared<gs::ReferenceTrack>();
        juce::String err;
        const bool ok = gs::loadReference (file, *ref, err);
        std::shared_ptr<const juce::AudioBuffer<float>> res;
        if (ok) res = resampleTo (ref->audio, ref->sampleRate, hostRate);

        juce::MessageManager::callAsync ([this, flag, ok, ref, res, err, hostRate]
        {
            if (! flag->load()) return;
            loading.store (false);
            if (! ok)
            {
                loadError = err;
                if (onReferenceChanged) onReferenceChanged();
                return;
            }
            reference = ref;
            resampled = res;
            resampledRate = hostRate;
            bpm = pendingBpm > 0 ? pendingBpm : ref->detectedBpm;
            gridOffsetSec = pendingOffset;
            windowStartBar = juce::jmax (0, pendingStartBar);
            pendingBpm = 0;  pendingOffset = 0;  pendingStartBar = -1;
            rebuildWindow();
            if (std::abs (resampledRate - sr) > 0.5)     // host rate changed while loading
                requestResample (sr);
            if (onReferenceChanged) onReferenceChanged();
        });
    });
}

void GroundShakeProcessor::setBpm (double newBpm)
{
    if (newBpm < 40.0 || newBpm > 300.0) return;
    bpm = std::round (newBpm * 100.0) / 100.0;
    rebuildWindow();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::nudgeGrid (double seconds)
{
    gridOffsetSec = juce::jlimit (-2.0, 2.0, gridOffsetSec + seconds);
    rebuildWindow();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::setWindowStartBar (int bar)
{
    windowStartBar = bar;
    rebuildWindow();
    if (onReferenceChanged) onReferenceChanged();
}

int GroundShakeProcessor::getNumReferenceBars() const
{
    return reference ? gs::numBarsInReference (*reference, bpm, gridOffsetSec) : 0;
}

double GroundShakeProcessor::getGridStartSec() const
{
    return reference ? reference->firstDownbeatSec + gridOffsetSec : 0.0;
}

void GroundShakeProcessor::rebuildWindow()
{
    if (reference == nullptr || bpm <= 0) return;
    const int barsAvail = getNumReferenceBars();
    const int bars = juce::jlimit (1, gs::kBars, barsAvail);
    windowStartBar = juce::jlimit (0, juce::jmax (0, barsAvail - gs::kBars), windowStartBar);
    refProfile = gs::profileForWindow (*reference, bpm, gridOffsetSec, windowStartBar, bars);

    if (resampled == nullptr) return;
    auto p = std::make_unique<RefPlayback>();
    p->audio = resampled;
    p->sampleRate = resampledRate;
    p->bpm = bpm;
    const double barSec = 240.0 / bpm;
    const double startSec = juce::jmax (0.0, getGridStartSec() + windowStartBar * barSec);
    p->windowStart = juce::jlimit (0, resampled->getNumSamples() - 1, (int) std::round (startSec * resampledRate));
    p->windowLength = juce::jlimit (1, resampled->getNumSamples() - p->windowStart,
                                    (int) std::round (bars * barSec * resampledRate));
    p->windowMeanSquare = juce::jmax (1.0e-9f, refProfile.fullTotal);
    installPlayback (std::move (p));
}

void GroundShakeProcessor::installPlayback (std::unique_ptr<RefPlayback> p)
{
    if (ownedPlayback != nullptr)
        retired.emplace_back (std::move (ownedPlayback), juce::Time::getMillisecondCounter());
    ownedPlayback = std::move (p);
    livePlayback.store (ownedPlayback.get());
}

void GroundShakeProcessor::timerCallback()
{
    // free old playback objects once the audio thread can no longer be using them
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [now] (const auto& r) { return now - r.second > 1500; }),
                   retired.end());
}

//==============================================================================
juce::AudioProcessorEditor* GroundShakeProcessor::createEditor()
{
    return new GroundShakeEditor (*this);
}

void GroundShakeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("GroundShake");
    xml.setAttribute ("version", 1);
    if (reference) xml.setAttribute ("reference", reference->file.getFullPathName());
    xml.setAttribute ("bpm", bpm);
    xml.setAttribute ("offset", gridOffsetSec);
    xml.setAttribute ("startBar", windowStartBar);
    xml.setAttribute ("listenB", listenB.load());
    copyXmlToBinary (xml, dest);
}

void GroundShakeProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("GroundShake")) return;
    listenB.store (xml->getBoolAttribute ("listenB", false));
    const juce::File f (xml->getStringAttribute ("reference"));
    if (f.existsAsFile())
    {
        pendingBpm = xml->getDoubleAttribute ("bpm", 0.0);
        pendingOffset = xml->getDoubleAttribute ("offset", 0.0);
        pendingStartBar = xml->getIntAttribute ("startBar", 0);
        loadReferenceAsync (f);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GroundShakeProcessor();
}
