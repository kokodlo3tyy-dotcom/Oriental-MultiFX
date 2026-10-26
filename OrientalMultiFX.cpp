#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

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
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("body",    "Body Warmth (220Hz)", 0.0f, 1.0f, 0.45f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("deharsh", "Pick De-Harsh (3.4kHz)", 0.0f, 1.0f, 0.55f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("drive",   "Anatolian Drive",     0.0f, 1.0f, 0.35f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("shimmer", "BBD Shimmer Chorus",  0.0f, 1.0f, 0.30f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("delay",   "Stage Echo Mix",      0.0f, 1.0f, 0.30f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("dtime",   "Echo Time (ms)",      120.0f, 650.0f, 360.0f));
        return { params.begin(), params.end() };
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = currentSampleRate;
        spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
        spec.numChannels = 1;

        for (int ch = 0; ch < 2; ++ch)
        {
            hpFilter[ch].prepare (spec);
            bodyFilter[ch].prepare (spec);
            notchFilter[ch].prepare (spec);
            lpFilter[ch].prepare (spec);

            *hpFilter[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, 75.0f);
            *lpFilter[ch].coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, 7500.0f);
            delayDampState[ch] = 0.0f;
        }

        delayBuffer.setSize (2, (int)(currentSampleRate * 2.0) + 1);
        delayBuffer.clear();
        chorusBuffer.setSize (2, (int)(currentSampleRate * 0.1) + 1);
        chorusBuffer.clear();

        writePos = 0;
        chorusWritePos = 0;
        lfoPhase = 0.0f;
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int totalNumInputChannels  = getTotalNumInputChannels();
        const int totalNumOutputChannels = getTotalNumOutputChannels();
        const int numSamples = buffer.getNumSamples();

        for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
            buffer.clear (i, 0, numSamples);

        float bodyVal    = *treeState.getRawParameterValue ("body");
        float deharshVal = *treeState.getRawParameterValue ("deharsh");
        float driveVal   = *treeState.getRawParameterValue ("drive");
        float shimmerVal = *treeState.getRawParameterValue ("shimmer");
        float delayVal   = *treeState.getRawParameterValue ("delay");
        float dtimeMs    = *treeState.getRawParameterValue ("dtime");

        auto bodyCoeffs  = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            currentSampleRate, 220.0f, 1.2f, juce::Decibels::decibelsToGain (bodyVal * 5.5f));
        auto notchCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            currentSampleRate, 3400.0f, 2.8f, juce::Decibels::decibelsToGain (-deharshVal * 8.5f));

        for (int ch = 0; ch < 2; ++ch)
        {
            *bodyFilter[ch].coefficients  = *bodyCoeffs;
            *notchFilter[ch].coefficients = *notchCoeffs;
        }

        const int delayBufSize  = delayBuffer.getNumSamples();
        const int chorusBufSize = chorusBuffer.getNumSamples();
        const int delaySamples  = juce::jlimit (1, delayBufSize - 1, (int)(currentSampleRate * (dtimeMs / 1000.0f)));
        const float lfoInc      = (float)(2.0 * juce::MathConstants<double>::pi * 0.95 / currentSampleRate);

        // 2.5kHz Low-pass coefficient for dark stage echo repeats
        const float dampCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi * 2500.0f / (float)currentSampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            lfoPhase += lfoInc;
            if (lfoPhase > juce::MathConstants<float>::twoPi)
                lfoPhase -= juce::MathConstants<float>::twoPi;

            for (int ch = 0; ch < juce::jmin (2, totalNumInputChannels); ++ch)
            {
                float* channelData = buffer.getWritePointer (ch);
                float* dData       = delayBuffer.getWritePointer (ch);
                float* cData       = chorusBuffer.getWritePointer (ch);

                float x = channelData[i];

                // 1. Pre-EQ: 75Hz HighPass -> 220Hz Body -> 3.4kHz De-Harsh Notch -> 7.5kHz Smooth LowPass
                x = hpFilter[ch].processSample (x);
                x = bodyFilter[ch].processSample (x);
                x = notchFilter[ch].processSample (x);
                x = lpFilter[ch].processSample (x);

                // 2. Anatolian Asymmetric Saturation
                float gainIn = 1.0f + driveVal * 3.5f;
                float driven = x * gainIn;
                float sat = (driven >= 0.0f) ? (driven / (1.0f + driven))
                                             : (driven / (1.0f + std::abs (driven) * 0.80f));
                sat /= (1.0f + driveVal * 0.6f); // Auto gain compensation

                // 3. BBD Shimmer Chorus (Stereo Phase Offset)
                cData[chorusWritePos] = sat;
                float chPhase = lfoPhase + (ch == 1 ? juce::MathConstants<float>::halfPi : 0.0f);
                float modDelaySamples = (0.011f + std::sin (chPhase) * 0.0025f) * (float)currentSampleRate;
                float cReadPos = (float)chorusWritePos - modDelaySamples;
                if (cReadPos < 0.0f) cReadPos += (float)chorusBufSize;
                int idx0 = (int)cReadPos;
                int idx1 = (idx0 + 1) % chorusBufSize;
                float frac = cReadPos - (float)idx0;
                float chorusSig = cData[idx0] * (1.0f - frac) + cData[idx1] * frac;

                float postChorus = sat + (chorusSig * shimmerVal * 0.65f);

                // 4. Damped Stage Echo (2.5kHz High-Cut on repeats)
                int dReadPos = writePos - delaySamples;
                if (dReadPos < 0) dReadPos += delayBufSize;
                float rawDelayed = dData[dReadPos];
                delayDampState[ch] = (1.0f - dampCoeff) * rawDelayed + dampCoeff * delayDampState[ch];
                float dampedDelayed = delayDampState[ch];

                dData[writePos] = postChorus + (dampedDelayed * 0.36f);

                channelData[i] = postChorus + (dampedDelayed * delayVal);
            }

            if (++writePos >= delayBufSize) writePos = 0;
            if (++chorusWritePos >= chorusBufSize) chorusWritePos = 0;
        }
    }

    const juce::String getName() const override { return "Oriental MultiFX"; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
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
        if (xmlState.get() != nullptr)
            if (xmlState->hasTagName (treeState.state.getType()))
                treeState.replaceState (juce::ValueTree::fromXml (*xmlState));
    }

private:
    juce::AudioProcessorValueTreeState treeState;
    juce::dsp::IIR::Filter<float> hpFilter[2], bodyFilter[2], notchFilter[2], lpFilter[2];
    juce::AudioBuffer<float> delayBuffer, chorusBuffer;
    float delayDampState[2] = { 0.0f, 0.0f };
    int writePos = 0, chorusWritePos = 0;
    double currentSampleRate = 44100.0;
    float lfoPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrientalMultiFXAudioProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrientalMultiFXAudioProcessor();
}
