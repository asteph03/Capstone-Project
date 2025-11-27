// DELAY PROCESSOR

#include <JuceHeader.h>

using namespace juce;

namespace PARAMS
{
    #define PARAMETER_ID(str) constexpr const char* str { #str };
    
    PARAMETER_ID(DelayTime)
    PARAMETER_ID(DelayTimeSync)
    PARAMETER_ID(DelayFeedback)
    PARAMETER_ID(DelayMix)
    PARAMETER_ID(DelayBypass)
    PARAMETER_ID(DelaySync)
    PARAMETER_ID(DelayType)
    PARAMETER_ID(DelayAdjust)
}

StringArray delayTypes =
{
    "Mono",
    "Stereo",
    "Ping-Pong",
};

enum typeIndex
{
    Mono = 0,
    Stereo = 1,
    PingPong = 2,
};

StringArray delayTimes =
{
    "2",
    "1",
    "1/2",
    "1/4",
    "1/8",
    "1/16",
    "1/32",
    "1/64",
};

enum timeIndex
{
    Whole = 0,
    Half = 1,
    Quarter = 2,
    Eighth = 3,
    Sixteenth = 4,
    ThirtySecond = 5
};

StringArray adjust =
{
    "Straight",
    "Dotted",
    "Triplet",
};

enum adjIndex
{
    Straight = 0,
    Dotted = 1,
    Triplet = 2,
};

class DelayProcessor
{
public:
    DelayProcessor() {}
    
    void prepare(dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        lagrange.prepare(spec);
        
        for (auto& power : powerBlend)
            power.reset(sampleRate, 1.0);
        for (auto& mixer : mixerBlend)
            mixer.reset(sampleRate, 1.0);
        
        for (auto& dly : delayValue)
            dly.reset(spec.sampleRate, 0.1);
    }
    
    void addParams(AudioProcessorParameterGroup& params)
    {
        params.addChild(std::make_unique<AudioParameterBool>(ParameterID(PARAMS::DelayBypass, 1), "Bypass", false));
        params.addChild(std::make_unique<AudioParameterBool>(ParameterID(PARAMS::DelaySync, 1), "Tempo Sync", false));
        params.addChild(std::make_unique<AudioParameterChoice>(ParameterID(PARAMS::DelayType, 1), "Type", delayTypes, Mono));
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::DelayMix, 1), "Mix", NormalisableRange<float>(0.f, 100.f, 1.f), 50.f));
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::DelayTime, 1), "Time (ms)", NormalisableRange<float>(1.f, 2000.f, 1.f, 0.5f), 100.f));
        params.addChild(std::make_unique<AudioParameterChoice>(ParameterID(PARAMS::DelayTimeSync, 1), "Time (sync)", delayTimes, Quarter));
        params.addChild(std::make_unique<AudioParameterChoice>(ParameterID(PARAMS::DelayAdjust, 1), "Type", adjust, Straight));
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::DelayFeedback, 1), "Feedback", NormalisableRange<float>(0.f, 100.f, 1.f), 50.f));
    }
    
    bool update(AudioProcessorValueTreeState& params)
    {
        bool bypass = (bool)params.getRawParameterValue(PARAMS::DelayBypass)->load();
        float mix = params.getRawParameterValue(PARAMS::DelayMix)->load() * 0.01f;
        float feedback = params.getRawParameterValue(PARAMS::DelayFeedback)->load() * 0.01f;
        
        setMix(mix);
        setFeedback(feedback);
        setBypassed(bypass);
        
        bool sync = (bool)params.getRawParameterValue(PARAMS::DelaySync)->load();
        int type = (int)params.getRawParameterValue(PARAMS::DelayType)->load();
        float msTime = params.getRawParameterValue(PARAMS::DelayTime)->load();
        int syncTime = (int)params.getRawParameterValue(PARAMS::DelayTimeSync)->load();
        int adj = (int)params.getRawParameterValue(PARAMS::DelayAdjust)->load();
        
        for (int ch=0; ch<2; ch++) {
            float d = calcDelay(type, syncTime, adj, msTime, sync, ch);
            delayValue[ch].setTargetValue(d);
        }
        
        return bypass;
    }
    
    float getDelay()
    {
        return delayValue[0].getTargetValue();
    }

    void setMix(float wetValue)
    {
        for (auto& mixer : mixerBlend)
            mixer.setTargetValue(wetValue);
    }

    void setFeedback(float fb)
    {
        for (auto& feedback : delayFeedbackVolume)
            feedback.setTargetValue(fb);
    }

    void setBypassed(bool bypass)
    {
        for (auto& power : powerBlend)
        power.setTargetValue(bypass ? 0.f : 1.f);
    }
    
    void reset()
    {
        lagrange.reset();
        for (int i = 0; i < 2; i++) {
            powerBlend[i].reset(sampleRate, 1.0);
            mixerBlend[i].reset(sampleRate, 1.0);
        }

        std::fill(lastDelayEffectOutput.begin(), lastDelayEffectOutput.end(), 0.0f);
    }
    
    void process(dsp::ProcessContextReplacing<float> context)
    {
        const auto& inputBlock = context.getInputBlock();
        const auto& outputBlock = context.getOutputBlock();
        const auto numSamples = inputBlock.getNumSamples();
        const auto numChannels = inputBlock.getNumChannels();
        
        if (powerBlend[0].getCurrentValue() > 0.0001 || powerBlend[0].getTargetValue() > 0.5)
        {
            if (isPingPong)
            {
                auto* samplesInL = inputBlock.getChannelPointer(0);
                auto* samplesInR = inputBlock.getChannelPointer(1);
                auto* samplesOutL = outputBlock.getChannelPointer(0);
                auto* samplesOutR = outputBlock.getChannelPointer(1);
                
                for (size_t i = 0; i < numSamples; i++)
                {
                    const float inputL = samplesInL[i];
                    const float inputR = samplesInR[i];
                    
                    
                }
            }
            else
            {
                
            }
        }
        else
        {
            
        }
    }
    
    float calcDelay(int typeI, int timeI, int adjI, float ms, bool sync, int channel)
    {
        /*
        delayValue -> Samples per delay
        Samples per delay = ms per delay * sampleRate * 0.001
        ms per delay = ms per beat * note val * timeAdjust
        ms per beat = 60000 / bpm
        */
        
        float delayVal = 0.f;
        
        if (!sync) //IF DELAY BY MILISECONDS
        {
            switch(typeI)
            {
            case Mono:
                delayVal = jmin(ms * sampleRate * 0.001f, (float)delayMaxSamples);
                isPingPong = false;
                break;
            case Stereo:
                delayVal = jmin((ms + channel * 5) * sampleRate * 0.001f, (float)delayMaxSamples);
                isPingPong = false;
                break;
            case PingPong:
                delayVal = jmin(ms * sampleRate * 0.001f, (float)delayMaxSamples);
                isPingPong = true;
                break;
            }
        }
        else //SYNC DELAY
        {
            float timing;
            float mpd; //MILISECONDS PER DELAY
            
            switch (timeI)
            {
                default:
                case Whole: mpd = (60000 / bpm) * 4 * timing; break;
                case Half: mpd = (60000 / bpm) * 2 * timing; break;
                case Quarter: mpd = (60000 / bpm) * timing; break;
                case Eighth: mpd = (60000 / bpm) * 0.5f * timing; break;
                case Sixteenth: mpd = (60000 / bpm) * 0.25f * timing; break;
                case ThirtySecond: mpd = (60000 / bpm) * 0.125f * timing; break;
            }
            
            switch (typeI)
            {
            case Mono:
                delayVal = jmin(mpd * sampleRate * 0.001f, (float)delayMaxSamples);
                isPingPong = false;
                break;
            case Stereo:
                delayVal = jmin((mpd + channel*5) * sampleRate * 0.001f, (float)delayMaxSamples);
                isPingPong = false;
                break;
            case PingPong:
                delayVal = jmin(mpd * sampleRate * 0.001f, (float)delayMaxSamples);
                isPingPong = true;
                break;
            }
        }
        
        return delayVal;
    }
    
    void setBpm(double tempo)
    {
        bpm = tempo;
    }
    
private:
    static constexpr auto delayMaxSamples = 480000; // 5s @ 96k sample rate
    static constexpr auto delayMaxTimeMs = 5000.f; // 5s max sample time

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> lagrange{ delayMaxSamples };
    std::array<juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>, 2> delayValue;
    std::array<juce::LinearSmoothedValue<float>, 2> delayFeedbackVolume;
    std::array<juce::LinearSmoothedValue<float>, 2> powerBlend, mixerBlend;

    std::array<float, 2> lastDelayEffectOutput;
    float sampleRate, feedback;
    bool isPingPong;
        
    float bpm = 120.0;
};
