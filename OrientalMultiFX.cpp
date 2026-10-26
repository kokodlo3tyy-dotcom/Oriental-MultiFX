#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

//==============================================================================
// AUDIO PROCESSOR (THE eBAGLAMA / ORIENTAL MULTI-FX ENGINE)
//==============================================================================
class OrientalMultiFXAudioProcessor : public juce::AudioProcessor
{
public:
    OrientalMultiFXAudioProcessor()
        : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          treeState (*this, nullptr, "PARAMETERS", createParameterLayout())
    {}

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

        // Pickup Match (4 Knobs)
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("pk_bass",   "Pickup Bass",     -12.0f, 12.0f, 2.5f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("pk_lowmid", "Pickup LowMids",  -12.0f, 12.0f, 1.5f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("pk_pres",   "Pickup Presence", -12.0f, 12.0f, -4.5f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("pk_air",    "Pickup Air",      -12.0f, 12.0f, 2.0f));

        // Preamp & Cab
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("amp_gain",  "Preamp Gain",     0.0f, 100.0f, 38.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("amp_mid",   "Preamp Mid",      -12.0f, 12.0f, 2.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("amp_treb",  "Preamp Treble",   -12.0f, 12.0f, 1.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("cab_mix",   "Cabinet 2x12 IR", 0.0f, 100.0f, 75.0f));

        // Compressor & Octave
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("comp_amt",  "Comp Amount",     0.0f, 100.0f, 45.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("comp_blend","Comp Blend",      0.0f, 100.0f, 70.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("oct_mix",   "Sub Octave Mix",  0.0f, 100.0f, 0.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("gate_thr",  "Noise Gate",      0.0f, 100.0f, 15.0f));

        // Modulation & Phaser
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("phase_mix", "Phaser Depth",    0.0f, 100.0f, 25.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("mod_mix",   "Shimmer Chorus",  0.0f, 100.0f, 35.0f));

        // Tape Delay
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("dly_mix",   "Delay Mix",       0.0f, 100.0f, 28.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("dly_time",  "Delay Time (ms)", 100.0f, 750.0f, 360.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("dly_fb",    "Delay Feedback",  0.0f, 85.0f, 35.0f));

        // Plate Reverb
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("rev_mix",   "Reverb Mix",      0.0f, 100.0f, 30.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("rev_size",  "Reverb Size",     0.0f, 100.0f, 65.0f));
        p.push_back (std::make_unique<juce::AudioParameterFloat> ("out_lvl",   "Output Level",    -12.0f, 12.0f, 0.0f));

        // Stompbox Bypass Toggles (Bottom Pedalboard)
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_gate",   "Gate On",    true));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_pickup", "Pickup On",  true));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_oct",    "Octave On",  false));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_comp",   "Comp On",    true));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_drive",  "Drive On",   true));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_phase",  "Phase On",   true));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_mod",    "Mod On",     true));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_delay",  "Delay On",   true));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_reverb", "Reverb On",  true));
        p.push_back (std::make_unique<juce::AudioParameterBool> ("on_boost",  "Solo Boost", false));

        return { p.begin(), p.end() };
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        juce::dsp::ProcessSpec spec { sr, (juce::uint32) samplesPerBlock, 1 };
        for (int ch = 0; ch < 2; ++ch)
        {
            hpFilter[ch].prepare (spec);
            pkBass[ch].prepare (spec);
            pkLowMid[ch].prepare (spec);
            pkPres[ch].prepare (spec);
            pkAir[ch].prepare (spec);
            ampMid[ch].prepare (spec);
            ampTreb[ch].prepare (spec);
            cabLp[ch].prepare (spec);
            cabRes[ch].prepare (spec);

            *hpFilter[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 75.0f);
            *cabLp[ch].coefficients    = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 6200.0f, 0.85f);
            *cabRes[ch].coefficients   = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 185.0f, 1.4f, 1.35f);

            compEnv[ch] = 0.0f;
            gateEnv[ch] = 0.0f;
            octFlip[ch] = 1.0f;
            prevZeroCross[ch] = 0.0f;
            octSmooth[ch] = 0.0f;
            phaserState[ch][0] = phaserState[ch][1] = 0.0f;
            delayDamp[ch] = 0.0f;
        }

        delayBuffer.setSize (2, (int)(sr * 2.0) + 1);
        delayBuffer.clear();
        chorusBuffer.setSize (2, (int)(sr * 0.1) + 1);
        chorusBuffer.clear();

        reverb.setSampleRate (sr);
        writePos = 0;
        chorusWritePos = 0;
        lfoPhase = 0.0f;
        phaserPhase = 0.0f;
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int numCh = juce::jmin (2, getTotalNumInputChannels());
        const int numSamples = buffer.getNumSamples();

        for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
            buffer.clear (i, 0, numSamples);

        // Read parameters
        float pBass   = *treeState.getRawParameterValue ("pk_bass");
        float pLowMid = *treeState.getRawParameterValue ("pk_lowmid");
        float pPres   = *treeState.getRawParameterValue ("pk_pres");
        float pAir    = *treeState.getRawParameterValue ("pk_air");

        float aGain   = *treeState.getRawParameterValue ("amp_gain") * 0.01f;
        float aMid    = *treeState.getRawParameterValue ("amp_mid");
        float aTreb   = *treeState.getRawParameterValue ("amp_treb");
        float cabMix  = *treeState.getRawParameterValue ("cab_mix") * 0.01f;

        float cAmt    = *treeState.getRawParameterValue ("comp_amt") * 0.01f;
        float cBlend  = *treeState.getRawParameterValue ("comp_blend") * 0.01f;
        float octMix  = *treeState.getRawParameterValue ("oct_mix") * 0.01f;
        float gateThr = *treeState.getRawParameterValue ("gate_thr") * 0.01f;

        float phsMix  = *treeState.getRawParameterValue ("phase_mix") * 0.01f;
        float modMix  = *treeState.getRawParameterValue ("mod_mix") * 0.01f;

        float dlyMix  = *treeState.getRawParameterValue ("dly_mix") * 0.01f;
        float dlyTime = *treeState.getRawParameterValue ("dly_time");
        float dlyFb   = *treeState.getRawParameterValue ("dly_fb") * 0.01f;

        float revMix  = *treeState.getRawParameterValue ("rev_mix") * 0.01f;
        float revSize = *treeState.getRawParameterValue ("rev_size") * 0.01f;
        float outGain = juce::Decibels::decibelsToGain ((float)*treeState.getRawParameterValue ("out_lvl"));

        bool onGate   = *treeState.getRawParameterValue ("on_gate") > 0.5f;
        bool onPickup = *treeState.getRawParameterValue ("on_pickup") > 0.5f;
        bool onOct    = *treeState.getRawParameterValue ("on_oct") > 0.5f;
        bool onComp   = *treeState.getRawParameterValue ("on_comp") > 0.5f;
        bool onDrive  = *treeState.getRawParameterValue ("on_drive") > 0.5f;
        bool onPhase  = *treeState.getRawParameterValue ("on_phase") > 0.5f;
        bool onMod    = *treeState.getRawParameterValue ("on_mod") > 0.5f;
        bool onDelay  = *treeState.getRawParameterValue ("on_delay") > 0.5f;
        bool onReverb = *treeState.getRawParameterValue ("on_reverb") > 0.5f;
        bool onBoost  = *treeState.getRawParameterValue ("on_boost") > 0.5f;

        if (onBoost) outGain *= juce::Decibels::decibelsToGain (3.5f);

        // Update Filter Coefficients
        auto cBass   = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, 160.0f, 0.8f, juce::Decibels::decibelsToGain (pBass));
        auto cLowMid = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 320.0f, 1.3f, juce::Decibels::decibelsToGain (pLowMid));
        auto cPres   = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 3400.0f, 2.5f, juce::Decibels::decibelsToGain (pPres));
        auto cAir    = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 5800.0f, 0.8f, juce::Decibels::decibelsToGain (pAir));
        auto cAMid   = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 1100.0f, 1.0f, juce::Decibels::decibelsToGain (aMid));
        auto cATreb  = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 3800.0f, 0.8f, juce::Decibels::decibelsToGain (aTreb));

        for (int ch = 0; ch < 2; ++ch)
        {
            *pkBass[ch].coefficients   = *cBass;
            *pkLowMid[ch].coefficients = *cLowMid;
            *pkPres[ch].coefficients   = *cPres;
            *pkAir[ch].coefficients    = *cAir;
            *ampMid[ch].coefficients   = *cAMid;
            *ampTreb[ch].coefficients  = *cATreb;
        }

        const int delayBufSize  = delayBuffer.getNumSamples();
        const int chorusBufSize = chorusBuffer.getNumSamples();
        const int delaySamples  = juce::jlimit (1, delayBufSize - 1, (int)(sr * (dlyTime / 1000.0f)));
        const float lfoInc      = (float)(2.0 * juce::MathConstants<double>::pi * 0.85 / sr);
        const float phaserInc   = (float)(2.0 * juce::MathConstants<double>::pi * 0.45 / sr);
        const float dampCoeff   = std::exp (-2.0f * juce::MathConstants<float>::pi * 2400.0f / (float)sr);

        float peakIn = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            lfoPhase += lfoInc;
            if (lfoPhase > juce::MathConstants<float>::twoPi) lfoPhase -= juce::MathConstants<float>::twoPi;

            phaserPhase += phaserInc;
            if (phaserPhase > juce::MathConstants<float>::twoPi) phaserPhase -= juce::MathConstants<float>::twoPi;

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* data  = buffer.getWritePointer (ch);
                float* dData = delayBuffer.getWritePointer (ch);
                float* cData = chorusBuffer.getWritePointer (ch);

                float x = data[i];
                peakIn = juce::jmax (peakIn, std::abs (x));

                x = hpFilter[ch].processSample (x);

                // 1. Noise Gate
                if (onGate && gateThr > 0.01f)
                {
                    float absX = std::abs (x);
                    gateEnv[ch] = 0.995f * gateEnv[ch] + 0.005f * absX;
                    float thr = gateThr * 0.05f;
                    float gGain = gateEnv[ch] > thr ? 1.0f : (gateEnv[ch] / (thr + 0.0001f));
                    x *= gGain * gGain;
                }

                // 2. Sub-Octave Generator (For Saz / Buzuq thickness)
                if (onOct && octMix > 0.01f)
                {
                    if ((prevZeroCross[ch] < 0.0f && x >= 0.0f))
                        octFlip[ch] = -octFlip[ch];
                    prevZeroCross[ch] = x;
                    float rawSub = std::abs (x) * octFlip[ch];
                    octSmooth[ch] = 0.96f * octSmooth[ch] + 0.04f * rawSub;
                    x = x + octSmooth[ch] * octMix * 0.8f;
                }

                // 3. Pickup Match EQ
                if (onPickup)
                {
                    x = pkBass[ch].processSample (x);
                    x = pkLowMid[ch].processSample (x);
                    x = pkPres[ch].processSample (x);
                    x = pkAir[ch].processSample (x);
                }

                // 4. Studio Compressor (Parallel Blend)
                if (onComp && cAmt > 0.01f)
                {
                    float absX = std::abs (x);
                    compEnv[ch] = (absX > compEnv[ch]) ? (0.99f * compEnv[ch] + 0.01f * absX)
                                                       : (0.9992f * compEnv[ch] + 0.0008f * absX);
                    float cGain = 1.0f / (1.0f + cAmt * 3.5f * compEnv[ch]);
                    float wetComp = x * cGain * (1.0f + cAmt * 0.65f);
                    x = x * (1.0f - cBlend) + wetComp * cBlend;
                }

                // 5. Baglama Preamp & Vintage 2x12 Cabinet Voicing
                if (onDrive)
                {
                    float driveIn = x * (1.0f + aGain * 4.2f);
                    float sat = (driveIn >= 0.0f) ? (driveIn / (1.0f + driveIn))
                                                  : (driveIn / (1.0f + std::abs (driveIn) * 0.82f));
                    sat /= (1.0f + aGain * 0.55f);
                    sat = ampMid[ch].processSample (sat);
                    sat = ampTreb[ch].processSample (sat);

                    float cab = cabRes[ch].processSample (sat);
                    cab = cabLp[ch].processSample (cab);
                    x = sat * (1.0f - cabMix) + cab * cabMix;
                }

                // 6. Analog Phaser
                if (onPhase && phsMix > 0.01f)
                {
                    float apCoeff = 0.35f + 0.3f * std::sin (phaserPhase + ch * 0.8f);
                    float y1 = -apCoeff * x + phaserState[ch][0];
                    phaserState[ch][0] = x + apCoeff * y1;
                    x = x + y1 * phsMix * 0.65f;
                }

                // 7. BBD Shimmer Chorus
                if (onMod && modMix > 0.01f)
                {
                    cData[chorusWritePos] = x;
                    float chPhase = lfoPhase + (ch == 1 ? juce::MathConstants<float>::halfPi : 0.0f);
                    float modDelay = (0.011f + std::sin (chPhase) * 0.0025f) * (float)sr;
                    float cRead = (float)chorusWritePos - modDelay;
                    if (cRead < 0.0f) cRead += (float)chorusBufSize;
                    int idx0 = (int)cRead;
                    int idx1 = (idx0 + 1) % chorusBufSize;
                    float frac = cRead - (float)idx0;
                    float cSig = cData[idx0] * (1.0f - frac) + cData[idx1] * frac;
                    x += cSig * modMix * 0.65f;
                }

                // 8. Tape Delay
                if (onDelay && dlyMix > 0.01f)
                {
                    int dRead = writePos - delaySamples;
                    if (dRead < 0) dRead += delayBufSize;
                    float rawDelayed = dData[dRead];
                    delayDamp[ch] = (1.0f - dampCoeff) * rawDelayed + dampCoeff * delayDamp[ch];
                    float damped = delayDamp[ch];
                    dData[writePos] = x + (damped * dlyFb);
                    x += damped * dlyMix;
                }
                else
                {
                    dData[writePos] = x;
                }

                data[i] = x * outGain;
            }

            if (++writePos >= delayBufSize) writePos = 0;
            if (++chorusWritePos >= chorusBufSize) chorusWritePos = 0;
        }

        // 9. Stereo Plate Reverb
        if (onReverb && revMix > 0.01f)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = revSize;
            rp.damping  = 0.45f;
            rp.wetLevel = revMix * 0.55f;
            rp.dryLevel = 1.0f;
            rp.width    = 1.0f;
            reverb.setParameters (rp);

            if (numCh == 2)
                reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
            else if (numCh == 1)
                reverb.processMono (buffer.getWritePointer (0), numSamples);
        }

        currentInputLevel.store (peakIn);
    }

    void loadFactoryPreset (int index)
    {
        auto setP = [this](const juce::String& id, float plainVal)
        {
            if (auto* param = treeState.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (plainVal));
        };

        // Enable core modules by default
        setP ("on_gate", 1.0f); setP ("on_pickup", 1.0f); setP ("on_comp", 1.0f);
        setP ("on_drive", 1.0f); setP ("on_mod", 1.0f); setP ("on_delay", 1.0f); setP ("on_reverb", 1.0f);

        switch (index)
        {
            case 1: // Taksim Lead
                setP ("pk_bass", 2.5f); setP ("pk_lowmid", 1.5f); setP ("pk_pres", -4.5f); setP ("pk_air", 2.0f);
                setP ("amp_gain", 42.0f); setP ("amp_mid", 2.8f); setP ("amp_treb", 1.5f); setP ("cab_mix", 78.0f);
                setP ("on_oct", 0.0f); setP ("on_phase", 1.0f); setP ("phase_mix", 22.0f); setP ("mod_mix", 32.0f);
                setP ("dly_mix", 34.0f); setP ("dly_time", 360.0f); setP ("dly_fb", 38.0f);
                setP ("rev_mix", 32.0f); setP ("rev_size", 68.0f);
                break;

            case 2: // Shimmer Uzun Hava
                setP ("pk_bass", 3.5f); setP ("pk_lowmid", 2.0f); setP ("pk_pres", -5.5f); setP ("pk_air", 3.5f);
                setP ("amp_gain", 22.0f); setP ("amp_mid", 1.0f); setP ("amp_treb", 2.5f); setP ("cab_mix", 70.0f);
                setP ("on_oct", 0.0f); setP ("on_phase", 1.0f); setP ("phase_mix", 35.0f); setP ("mod_mix", 55.0f);
                setP ("dly_mix", 42.0f); setP ("dly_time", 460.0f); setP ("dly_fb", 48.0f);
                setP ("rev_mix", 58.0f); setP ("rev_size", 88.0f);
                break;

            case 3: // Taverna Nights
                setP ("pk_bass", 2.0f); setP ("pk_lowmid", 3.0f); setP ("pk_pres", -3.8f); setP ("pk_air", 1.0f);
                setP ("amp_gain", 52.0f); setP ("amp_mid", 3.5f); setP ("amp_treb", 0.5f); setP ("cab_mix", 85.0f);
                setP ("on_oct", 0.0f); setP ("on_phase", 1.0f); setP ("phase_mix", 45.0f); setP ("mod_mix", 40.0f);
                setP ("dly_mix", 26.0f); setP ("dly_time", 290.0f); setP ("dly_fb", 30.0f);
                setP ("rev_mix", 25.0f); setP ("rev_size", 55.0f);
                break;

            case 4: // Arabesk Gece
                setP ("pk_bass", 3.0f); setP ("pk_lowmid", 2.5f); setP ("pk_pres", -5.0f); setP ("pk_air", 1.5f);
                setP ("amp_gain", 36.0f); setP ("amp_mid", 2.0f); setP ("amp_treb", 1.0f); setP ("cab_mix", 80.0f);
                setP ("on_oct", 1.0f); setP ("oct_mix", 25.0f); setP ("on_phase", 0.0f); setP ("mod_mix", 45.0f);
                setP ("dly_mix", 36.0f); setP ("dly_time", 400.0f); setP ("dly_fb", 40.0f);
                setP ("rev_mix", 42.0f); setP ("rev_size", 75.0f);
                break;

            case 5: // Dugun Halay (Heavy Electro Saz)
                setP ("pk_bass", 4.0f); setP ("pk_lowmid", 1.5f); setP ("pk_pres", -3.0f); setP ("pk_air", 2.5f);
                setP ("amp_gain", 65.0f); setP ("amp_mid", 4.0f); setP ("amp_treb", 2.0f); setP ("cab_mix", 85.0f);
                setP ("on_oct", 1.0f); setP ("oct_mix", 38.0f); setP ("on_phase", 1.0f); setP ("phase_mix", 50.0f);
                setP ("dly_mix", 24.0f); setP ("dly_time", 240.0f); setP ("dly_fb", 25.0f);
                setP ("rev_mix", 22.0f); setP ("rev_size", 50.0f);
                break;

            case 6: // Electric Buzuq & Oud Clean
                setP ("pk_bass", 3.5f); setP ("pk_lowmid", 1.0f); setP ("pk_pres", -6.0f); setP ("pk_air", 3.0f);
                setP ("amp_gain", 15.0f); setP ("amp_mid", 0.5f); setP ("amp_treb", 2.0f); setP ("cab_mix", 60.0f);
                setP ("on_oct", 0.0f); setP ("on_phase", 0.0f); setP ("mod_mix", 28.0f);
                setP ("dly_mix", 28.0f); setP ("dly_time", 350.0f); setP ("dly_fb", 32.0f);
                setP ("rev_mix", 35.0f); setP ("rev_size", 70.0f);
                break;
        }
    }

    const juce::String getName() const override { return "The eBaglama Oriental FX"; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() const override { return 1; }
    int getCurrentProgram() const override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        auto state = treeState.copyState();
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
        if (xmlState != nullptr && xmlState->hasTagName (treeState.state.getType()))
            treeState.replaceState (juce::ValueTree::fromXml (*xmlState));
    }

    juce::AudioProcessorValueTreeState treeState;
    std::atomic<float> currentInputLevel { 0.0f };

private:
    juce::dsp::IIR::Filter<float> hpFilter[2], pkBass[2], pkLowMid[2], pkPres[2], pkAir[2];
    juce::dsp::IIR::Filter<float> ampMid[2], ampTreb[2], cabLp[2], cabRes[2];
    juce::AudioBuffer<float> delayBuffer, chorusBuffer;
    juce::Reverb reverb;

    float compEnv[2] = {0}, gateEnv[2] = {0}, octFlip[2] = {1, 1}, prevZeroCross[2] = {0}, octSmooth[2] = {0};
    float phaserState[2][2] = {{0}};
    float delayDamp[2] = {0};
    int writePos = 0, chorusWritePos = 0;
    double sr = 44100.0;
    float lfoPhase = 0.0f, phaserPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrientalMultiFXAudioProcessor)
};

//==============================================================================
// CUSTOM LOOK AND FEEL (GLOWING ARCS & STOMPBOXES LIKE "THE eBAGLAMA")
//==============================================================================
class eBaglamaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    eBaglamaLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, const float rotaryStartAngle, const float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto lineW = 3.5f;
        auto arcRadius = radius - lineW * 0.5f;

        // Background track arc
        juce::Path bgArc;
        bgArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius,
                             0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff252a36));
        g.strokePath (bgArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Inner dark knob body
        auto knobRadius = arcRadius - 5.0f;
        g.setColour (juce::Colour (0xff12151e));
        g.fillEllipse (bounds.getCentreX() - knobRadius, bounds.getCentreY() - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
        g.setColour (juce::Colour (0xff2b3140));
        g.drawEllipse (bounds.getCentreX() - knobRadius, bounds.getCentreY() - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.2f);

        // Glowing Value Arc
        if (slider.isEnabled())
        {
            juce::Path valArc;
            valArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius,
                                  0.0f, rotaryStartAngle, toAngle, true);
            g.setColour (accent);
            g.strokePath (valArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Indicator dot
        juce::Point<float> thumbPoint (bounds.getCentreX() + (knobRadius - 4.0f) * std::cos (toAngle - juce::MathConstants<float>::halfPi),
                                       bounds.getCentreY() + (knobRadius - 4.0f) * std::sin (toAngle - juce::MathConstants<float>::halfPi));
        g.setColour (accent);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (thumbPoint));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn, bool isHighlighted, bool) override
    {
        auto bounds = btn.getLocalBounds().toFloat().reduced (2.0f);
        auto accent = btn.findColour (juce::ToggleButton::tickColourId);
        bool on = btn.getToggleState();

        g.setColour (on ? accent.withAlpha (0.22f) : juce::Colour (0xff151922));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (on ? accent : juce::Colour (0xff2c3344));
        g.drawRoundedRectangle (bounds, 6.0f, on ? 2.0f : 1.0f);

        // Top LED bar inside pedal button
        auto ledBounds = bounds.withHeight (4.0f).reduced (12.0f, 0.0f).translated (0.0f, 5.0f);
        g.setColour (on ? accent : juce::Colour (0xff2a2f3d));
        g.fillRoundedRectangle (ledBounds, 2.0f);

        g.setColour (on ? juce::Colours::white : juce::Colours::grey);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (btn.getButtonText(), bounds.translated (0, 4), juce::Justification::centred, false);

        if (isHighlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (bounds, 6.0f);
        }
    }
};

//==============================================================================
// CUSTOM EDITOR UI (MODULAR CARDS + ELECTRIC BAGLAMA GRAPHIC + STOMPBOX BAR)
//==============================================================================
class OrientalMultiFXEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    OrientalMultiFXEditor (OrientalMultiFXAudioProcessor& p)
        : AudioProcessorEditor (&p), proc (p)
    {
        setLookAndFeel (&lnf);
        setSize (1060, 650);

        // Preset ComboBox
        presetBox.addItem ("01 - Taksim Lead", 1);
        presetBox.addItem ("02 - Shimmer Uzun Hava", 2);
        presetBox.addItem ("03 - Taverna Nights", 3);
        presetBox.addItem ("04 - Arabesk Gece", 4);
        presetBox.addItem ("05 - Dugun Halay (Electro Saz)", 5);
        presetBox.addItem ("06 - Electric Buzuq & Oud Clean", 6);
        presetBox.setSelectedId (1, juce::dontSendNotification);
        presetBox.onChange = [this]() { proc.loadFactoryPreset (presetBox.getSelectedId()); };
        addAndMakeVisible (presetBox);

        // Colors matching "The eBaglama" video
        juce::Colour gold   (0xfff5a623);
        juce::Colour orange (0xffff6b35);
        juce::Colour purple (0xff9b5de5);
        juce::Colour cyan   (0xff00bbf9);
        juce::Colour green  (0xff00f5d4);

        // Card 1: Pickup Match (Gold)
        addKnob (kPkBass,   lPkBass,   "pk_bass",   "BASS",     gold);
        addKnob (kPkLowMid, lPkLowMid, "pk_lowmid", "LOW MIDS", gold);
        addKnob (kPkPres,   lPkPres,   "pk_pres",   "PRESENCE", gold);
        addKnob (kPkAir,    lPkAir,    "pk_air",    "AIR",      gold);

        // Card 2: Preamp & Cab (Orange)
        addKnob (kAmpGain,  lAmpGain,  "amp_gain",  "GAIN",     orange);
        addKnob (kAmpMid,   lAmpMid,   "amp_mid",   "MID",      orange);
        addKnob (kAmpTreb,  lAmpTreb,  "amp_treb",  "TREBLE",   orange);
        addKnob (kCabMix,   lCabMix,   "cab_mix",   "CAB 2x12", orange);

        // Card 3: Compressor, Gate & Sub-Octave (Purple)
        addKnob (kGateThr,  lGateThr,  "gate_thr",  "GATE",     purple);
        addKnob (kCompAmt,  lCompAmt,  "comp_amt",  "COMP",     purple);
        addKnob (kCompBlend,lCompBlend,"comp_blend","BLEND",    purple);
        addKnob (kOctMix,   lOctMix,   "oct_mix",   "OCTAVE",   purple);

        // Card 4: Phaser & Shimmer Mod (Cyan)
        addKnob (kPhaseMix, lPhaseMix, "phase_mix", "PHASER",   cyan);
        addKnob (kModMix,   lModMix,   "mod_mix",   "SHIMMER",  cyan);
        addKnob (kOutLvl,   lOutLvl,   "out_lvl",   "OUTPUT",   cyan);

        // Card 5: Tape Delay (Green)
        addKnob (kDlyMix,   lDlyMix,   "dly_mix",   "MIX",      green);
        addKnob (kDlyTime,  lDlyTime,  "dly_time",  "TIME",     green);
        addKnob (kDlyFb,    lDlyFb,    "dly_fb",    "FEEDBACK", green);

        // Card 6: Plate Reverb (Purple)
        addKnob (kRevMix,   lRevMix,   "rev_mix",   "MIX",      purple);
        addKnob (kRevSize,  lRevSize,  "rev_size",  "SIZE",     purple);

        // Bottom Stompboxes
        addPedal (bGate,   "on_gate",   "GATE",   cyan);
        addPedal (bPickup, "on_pickup", "PICKUP", gold);
        addPedal (bOct,    "on_oct",    "OCTAVE", purple);
        addPedal (bComp,   "on_comp",   "COMP",   purple);
        addPedal (bDrive,  "on_drive",  "DRIVE",  orange);
        addPedal (bPhase,  "on_phase",  "PHASE",  cyan);
        addPedal (bMod,    "on_mod",    "MOD",    green);
        addPedal (bDelay,  "on_delay",  "DELAY",  green);
        addPedal (bReverb, "on_reverb", "REVERB", purple);
        addPedal (bBoost,  "on_boost",  "BOOST",  orange);

        startTimerHz (24);
    }

    ~OrientalMultiFXEditor() override
    {
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        // Deep Dark Background
        g.fillAll (juce::Colour (0xff0b0d13));

        // Top Header Bar
        g.setColour (juce::Colour (0xff121620));
        g.fillRect (0, 0, getWidth(), 52);
        g.setColour (juce::Colour (0xff23293a));
        g.drawLine (0.0f, 52.0f, (float)getWidth(), 52.0f, 1.0f);

        // Brand Title
        g.setColour (juce::Colour (0xff4cc9f0));
        g.setFont (juce::Font (22.0f, juce::Font::bold | juce::Font::italic));
        g.drawText ("The eBaglama", 20, 10, 200, 32, juce::Justification::centredLeft);
        g.setColour (juce::Colours::grey);
        g.setFont (juce::Font (12.0f, juce::Font::plain));
        g.drawText ("ORIENTAL SAZ / BUZUQ / OUD PEDALBOARD", 185, 12, 280, 30, juce::Justification::centredLeft);

        // Left Signal Chain Column
        auto leftRect = juce::Rectangle<float> (14.0f, 64.0f, 165.0f, 470.0f);
        drawCard (g, leftRect, "SIGNAL CHAIN", juce::Colour (0xff4cc9f0));

        const char* chainNames[] = { "NOISE GATE", "SUB OCTAVE", "PICKUP MATCH", "COMPRESSOR",
                                     "BAGLAMA PREAMP", "CABINET 2x12", "ANALOG PHASER", "BBD SHIMMER",
                                     "TAPE DELAY", "PLATE REVERB" };
        juce::Colour chainCols[] = { juce::Colour(0xff00bbf9), juce::Colour(0xff9b5de5), juce::Colour(0xfff5a623),
                                     juce::Colour(0xff9b5de5), juce::Colour(0xffff6b35), juce::Colour(0xffff6b35),
                                     juce::Colour(0xff00bbf9), juce::Colour(0xff00f5d4), juce::Colour(0xff00f5d4),
                                     juce::Colour(0xff9b5de5) };

        for (int i = 0; i < 10; ++i)
        {
            auto pill = juce::Rectangle<float> (26.0f, 105.0f + i * 41.0f, 141.0f, 30.0f);
            g.setColour (chainCols[i].withAlpha (0.15f));
            g.fillRoundedRectangle (pill, 5.0f);
            g.setColour (chainCols[i]);
            g.drawRoundedRectangle (pill, 5.0f, 1.2f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            g.drawText (chainNames[i], pill.translated (8.0f, 0.0f), juce::Justification::centredLeft);
        }

        // Center-Left Electric Baglama Instrument Card
        auto instRect = juce::Rectangle<float> (190.0f, 64.0f, 175.0f, 470.0f);
        drawCard (g, instRect, "e-BAGLAMA STAGE", juce::Colour (0xfff5a623));
        drawBaglamaGraphic (g, instRect);

        // Right Modular Effect Cards (2 Rows x 3 Columns)
        drawCard (g, { 378.0f, 64.0f,  325.0f, 228.0f }, "01 / PICKUP MATCH (DE-HARSH)", juce::Colour (0xfff5a623));
        drawCard (g, { 715.0f, 64.0f,  330.0f, 228.0f }, "02 / PREAMP & VINTAGE 2x12 CAB", juce::Colour (0xffff6b35));
        drawCard (g, { 378.0f, 306.0f, 325.0f, 228.0f }, "03 / COMP, GATE & SUB-OCTAVE", juce::Colour (0xff9b5de5));
        drawCard (g, { 715.0f, 306.0f, 160.0f, 228.0f }, "04 / PHASER & MOD", juce::Colour (0xff00bbf9));
        drawCard (g, { 885.0f, 306.0f, 160.0f, 110.0f }, "05 / TAPE DELAY", juce::Colour (0xff00f5d4));
        drawCard (g, { 885.0f, 424.0f, 160.0f, 110.0f }, "06 / PLATE REVERB", juce::Colour (0xff9b5de5));

        // Bottom Stompbox Pedalboard Bar
        auto pedalBar = juce::Rectangle<float> (14.0f, 546.0f, (float)getWidth() - 28.0f, 92.0f);
        g.setColour (juce::Colour (0xff121620));
        g.fillRoundedRectangle (pedalBar, 8.0f);
        g.setColour (juce::Colour (0xff262d3d));
        g.drawRoundedRectangle (pedalBar, 8.0f, 1.2f);
    }

    void resized() override
    {
        presetBox.setBounds (getWidth() - 290, 12, 270, 28);

        // Card 1: Pickup Match (4 Knobs)
        placeKnob (kPkBass,   lPkBass,   390, 110, 72);
        placeKnob (kPkLowMid, lPkLowMid, 468, 110, 72);
        placeKnob (kPkPres,   lPkPres,   546, 110, 72);
        placeKnob (kPkAir,    lPkAir,    624, 110, 72);

        // Card 2: Preamp & Cab (4 Knobs)
        placeKnob (kAmpGain,  lAmpGain,  728, 110, 72);
        placeKnob (kAmpMid,   lAmpMid,   806, 110, 72);
        placeKnob (kAmpTreb,  lAmpTreb,  884, 110, 72);
        placeKnob (kCabMix,   lCabMix,   962, 110, 72);

        // Card 3: Comp, Gate & Octave (4 Knobs)
        placeKnob (kGateThr,  lGateThr,  390, 352, 72);
        placeKnob (kCompAmt,  lCompAmt,  468, 352, 72);
        placeKnob (kCompBlend,lCompBlend,546, 352, 72);
        placeKnob (kOctMix,   lOctMix,   624, 352, 72);

        // Card 4: Phaser, Mod & Out
        placeKnob (kPhaseMix, lPhaseMix, 722, 340, 66);
        placeKnob (kModMix,   lModMix,   798, 340, 66);
        placeKnob (kOutLvl,   lOutLvl,   760, 430, 60);

        // Card 5: Tape Delay
        placeKnob (kDlyMix,   lDlyMix,   890, 332, 48);
        placeKnob (kDlyTime,  lDlyTime,  940, 332, 48);
        placeKnob (kDlyFb,    lDlyFb,    990, 332, 48);

        // Card 6: Plate Reverb
        placeKnob (kRevMix,   lRevMix,   912, 450, 48);
        placeKnob (kRevSize,  lRevSize,  972, 450, 48);

        // Bottom 10 Stompbox Footswitches
        juce::ToggleButton* pedals[] = { &bGate, &bPickup, &bOct, &bComp, &bDrive, &bPhase, &bMod, &bDelay, &bReverb, &bBoost };
        int startX = 26;
        int pWidth = 100;
        for (int i = 0; i < 10; ++i)
            pedals[i]->setBounds (startX + i * 101, 558, pWidth - 6, 68);
    }

private:
    void timerCallback() override
    {
        repaint (190, 64, 175, 470);
    }

    void drawCard (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent)
    {
        g.setColour (juce::Colour (0xff141822));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xff252b3b));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);

        // Glowing top border line
        g.setColour (accent);
        g.fillRoundedRectangle (r.getX() + 10.0f, r.getY(), r.getWidth() - 20.0f, 2.5f, 1.0f);

        g.setColour (accent);
        g.setFont (juce::Font (11.5f, juce::Font::bold));
        g.drawText (title, (int)r.getX() + 12, (int)r.getY() + 8, (int)r.getWidth() - 24, 20, juce::Justification::centredLeft);
    }

    void drawBaglamaGraphic (juce::Graphics& g, juce::Rectangle<float> r)
    {
        float cx = r.getCentreX();
        float topY = r.getY() + 48.0f;

        // Headstock & Neck
        g.setColour (juce::Colour (0xff3d2b1f));
        g.fillRoundedRectangle (cx - 9.0f, topY, 18.0f, 220.0f, 3.0f);

        // Frets
        g.setColour (juce::Colours::silver.withAlpha (0.5f));
        for (int f = 0; f < 14; ++f)
            g.drawHorizontalLine ((int)(topY + 25.0f + f * 13.5f), cx - 8.0f, cx + 8.0f);

        // Pear-shaped Electric Baglama Body
        juce::Path body;
        body.addEllipse (cx - 54.0f, topY + 205.0f, 108.0f, 155.0f);
        g.setColour (juce::Colour (0xff1c1613));
        g.fillPath (body);
        g.setColour (juce::Colour (0xfff5a623).withAlpha (0.7f));
        g.strokePath (body, juce::PathStrokeType (2.0f));

        // Dual Pickups (Neck & Bridge)
        g.setColour (juce::Colour (0xff2b303c));
        g.fillRoundedRectangle (cx - 28.0f, topY + 235.0f, 56.0f, 18.0f, 3.0f);
        g.fillRoundedRectangle (cx - 28.0f, topY + 280.0f, 56.0f, 18.0f, 3.0f);

        // Strings
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawLine (cx - 5.0f, topY + 10.0f, cx - 5.0f, topY + 335.0f, 1.0f);
        g.drawLine (cx,        topY + 10.0f, cx,        topY + 335.0f, 1.0f);
        g.drawLine (cx + 5.0f, topY + 10.0f, cx + 5.0f, topY + 335.0f, 1.0f);

        // Input Signal Meter Glow
        float lvl = juce::jlimit (0.0f, 1.0f, proc.currentInputLevel.load() * 4.0f);
        g.setColour (juce::Colour (0xff00f5d4).withAlpha (0.25f + lvl * 0.7f));
        g.fillEllipse (cx - 8.0f, topY + 385.0f, 16.0f, 16.0f);
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText ("SIGNAL IN", (int)r.getX(), (int)(topY + 402.0f), (int)r.getWidth(), 18, juce::Justification::centred);
    }

    void addKnob (juce::Slider& s, juce::Label& l, const juce::String& paramID, const juce::String& name, juce::Colour col)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        s.setColour (juce::Slider::rotarySliderFillColourId, col);
        addAndMakeVisible (s);

        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (10.5f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (l);

        sliderAttach.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.treeState, paramID, s));
    }

    void placeKnob (juce::Slider& s, juce::Label& l, int x, int y, int size)
    {
        s.setBounds (x, y, size, size + 14);
        l.setBounds (x - 4, y + size + 12, size + 8, 16);
    }

    void addPedal (juce::ToggleButton& b, const juce::String& paramID, const juce::String& title, juce::Colour col)
    {
        b.setButtonText (title);
        b.setColour (juce::ToggleButton::tickColourId, col);
        addAndMakeVisible (b);
        btnAttach.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.treeState, paramID, b));
    }

    OrientalMultiFXAudioProcessor& proc;
    eBaglamaLookAndFeel lnf;
    juce::ComboBox presetBox;

    juce::Slider kPkBass, kPkLowMid, kPkPres, kPkAir;
    juce::Label  lPkBass, lPkLowMid, lPkPres, lPkAir;

    juce::Slider kAmpGain, kAmpMid, kAmpTreb, kCabMix;
    juce::Label  lAmpGain, lAmpMid, lAmpTreb, lCabMix;

    juce::Slider kGateThr, kCompAmt, kCompBlend, kOctMix;
    juce::Label  lGateThr, lCompAmt, lCompBlend, lOctMix;

    juce::Slider kPhaseMix, kModMix, kOutLvl;
    juce::Label  lPhaseMix, lModMix, lOutLvl;

    juce::Slider kDlyMix, kDlyTime, kDlyFb;
    juce::Label  lDlyMix, lDlyTime, lDlyFb;

    juce::Slider kRevMix, kRevSize;
    juce::Label  lRevMix, lRevSize;

    juce::ToggleButton bGate, bPickup, bOct, bComp, bDrive, bPhase, bMod, bDelay, bReverb, bBoost;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttach;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> btnAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrientalMultiFXEditor)
};

juce::AudioProcessorEditor* OrientalMultiFXAudioProcessor::createEditor()
{
    return new OrientalMultiFXEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrientalMultiFXAudioProcessor();
}
