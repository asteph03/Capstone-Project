/*
  ==============================================================================

    GrainProcessor.h
    Created: 13 Jan 2026 2:40:52pm
    Author:  Aidan Stephenson

  ==============================================================================
*/

/*
 ================== TODO LIST ==================
 2. Fix Freeze Mode
 3. Add reverb
 4. Implement delay properly
 5. Replace makeshift pitch-shifter with some real shi
 
 AFTER DSP:
 MAKE GUI
 
 */

#pragma once

#include <vector>
#include <JuceHeader.h>
#include "CircularBuffer.h"
using namespace juce;

namespace PARAMS
{
    #define PARAMETER_ID(str) constexpr const char* str { #str };

    PARAMETER_ID(GrainMix)
    PARAMETER_ID(GrainBypass)
    PARAMETER_ID(GrainSize)
    PARAMETER_ID(GrainDensity)
    PARAMETER_ID(GrainPitch)
    PARAMETER_ID(GrainEnvelope)
    PARAMETER_ID(GrainFreeze)
    PARAMETER_ID(GrainDelay)
    PARAMETER_ID(GrainFeedback)
    PARAMETER_ID(GrainStereo)
    PARAMETER_ID(GrainOnset)
}

struct Grain
{
    double currentPos;
    int grainSize, envPos;
    bool isFinished = false;
    float playbackSpeed = 1.f;
    float spreadL, spreadR;
    int replayCount = 0;
};

StringArray envelopeTypes =
{
    "Parabolic",
    "Trapezoidal",
    "Cosine Bell",
};

enum envelopeIndex
{
    Parabolic = 0,
    Trapezoidal = 1,
    CosineBell = 2,
};

class GrainProcessor
{
public:
    GrainProcessor() {}
    
    void prepare(dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        numChannels = spec.numChannels;
        circularBuffer.prepare(spec);
        grainPool.reserve(20);
        outputGain.prepare(spec);
        outputGain.setGainDecibels(20.f);
        
        const int numPoints = 1024;
        parabolicEnvelope.initialise ([numPoints](float index) {
                // Map the incoming index (0 to 1023) back to 0.0-1.0 for the cos math
                float x = index / (float)(numPoints - 1);
                return 1.f * (1.0f - std::cos(2.0f * pi * x));
            }, numPoints);
        
        trapezoidEnvelope.initialise ([numPoints](float index) {
            float x = index / (float)(numPoints - 1);
            if (x <= 0.25) // ATTACK
                return 8.f * x;
            else if (x >= 0.75) // RELEASE
                return (-8.f * x) + 8.f;
            else // SUSTAIN
                return 2.f;
            }, numPoints);
        
        bellEnvelope.initialise ([numPoints](float index) {
            float x = index / (float)(numPoints - 1);
            if (x <= 0.25 || x >= 0.75) // x <= 0.25 ATTACK, x >= 0.75 RELEASE
                return 1.f + std::cos(pi + (pi * (x / 0.25f)));
            else  // SUSTAIN
                return 2.f;
            }, numPoints);
    }
    
    void addParams(AudioProcessorParameterGroup& params)
    {
        params.addChild(std::make_unique<AudioParameterBool>(ParameterID(PARAMS::GrainBypass, 1), "Bypass", false));
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::GrainMix, 1), "Mix", NormalisableRange<float>(0.f, 100.f, 1.f), 50.f));
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::GrainSize, 1), "Size", NormalisableRange<float>(20.f, 100.f, 0.1f, 1.1f), 50.f));
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::GrainDensity, 1), "Density", NormalisableRange<float>(2.f, 20.f, 0.01f), 10.f));
        
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::GrainPitch, 1), "Pitch", NormalisableRange<float>(-12.f, 12.f, 1.f), 0.f));
        params.addChild(std::make_unique<AudioParameterChoice>(ParameterID(PARAMS::GrainEnvelope, 1), "Envelope", envelopeTypes, Parabolic));
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::GrainStereo, 1), "Stereo", NormalisableRange<float>(0.f, 100.f, 1.f), 0.f));
        params.addChild(std::make_unique<AudioParameterFloat>(ParameterID(PARAMS::GrainOnset, 1), "Onset Spray", NormalisableRange<float>(0.f, 100.f, 1.f), 0.f));
        
        params.addChild(std::make_unique<AudioParameterBool>(ParameterID(PARAMS::GrainFreeze, 1), "Freeze", false));
    }
    
    bool update(AudioProcessorValueTreeState& params)
    {
        bool bypass = (bool)params.getRawParameterValue(PARAMS::GrainBypass)->load();
        float mix = params.getRawParameterValue(PARAMS::GrainMix)->load() * 0.01f;
        float size = params.getRawParameterValue(PARAMS::GrainSize)->load();
        float density = params.getRawParameterValue(PARAMS::GrainDensity)->load();
        float pitch = params.getRawParameterValue(PARAMS::GrainPitch)->load();
        float spray = params.getRawParameterValue(PARAMS::GrainOnset)->load();
        
        for (auto& mixer : mixerBlend)
            mixer.setTargetValue(mix);
        
        for (auto& power : powerBlend)
            power.setTargetValue(bypass ? 0.f : 1.f);
        
        setScheduler(size, density, spray);
        
        grainPitch = pitch;
        
        envelopeType = (int)params.getRawParameterValue(PARAMS::GrainEnvelope)->load();
        stereoRange = (int)params.getRawParameterValue(PARAMS::GrainStereo)->load();
        
        grainFreeze = (bool)params.getRawParameterValue(PARAMS::GrainFreeze)->load();
        
        return true;
    }
    
    void setScheduler(float size, int density, float spray)
    {
        // size parameter is in miliseconds, convert from ms to samples
        paramGrainSize = (size * sampleRate) / 1000.f;
        
        // density will apply to delay line, # of grains played back per unit of time (set at 1 second for now, but maybe change to a unit in beats?)
        grainDensity = density;
        samplesPerGrain = sampleRate / grainDensity;
        sprayFactor = spray;
    }
    
    void spawnGrain(int index)
    {
        samplesSinceSpawn++;
        
        if (samplesSinceSpawn >= nextSpawn && grainPool.size() <= 20) {
            Grain newGrain;
            newGrain.currentPos = (index - 4401.0);
            newGrain.grainSize = paramGrainSize;
            newGrain.envPos = 0;
            newGrain.playbackSpeed = std::pow(2.f, grainPitch / 12.f);
            
            int randomPos = randomSpawn.nextInt(Range<int>(-1 * stereoRange, stereoRange+1));
            float stereo = (float)randomPos / 100.f;
            
            if (stereo == 0.f) {
                newGrain.spreadL = 1.f;
                newGrain.spreadR = 1.f;
            }
            else if (stereo > 0.f) {
                newGrain.spreadL = 1.f - stereo;
                newGrain.spreadR = 1.f;
            }
            else { // stereo < 0.f
                newGrain.spreadL = 1.f;
                newGrain.spreadR = 1.f + stereo;
            }
            
            if (newGrain.currentPos < 0) {
                newGrain.currentPos += circularBuffer.getSize();
            }
            
            grainPool.push_back(newGrain);
            
            int randomSpray = randomSpawn.nextInt(Range<int>(-1 * sprayFactor, sprayFactor+1));
            float spray = randomSpray / 100.f;
            
            samplesSinceSpawn = 0;
            
            nextSpawn = samplesPerGrain + ((samplesPerGrain / 1.5f) * spray);
        }
    }
    
    void cleanGrainPool()
    {
        grainPool.erase(std::remove_if(grainPool.begin(), grainPool.end(),
                [](const Grain& g) { return g.isFinished; }),
                grainPool.end());
    }
    
    void reset()
    {
        circularBuffer.clearBuffer();
    }
    
    float calculateEnvelope(int tableIndex)
    {
        float envVal;
        
        switch(envelopeType)
        {
            case Parabolic:
                envVal = parabolicEnvelope[tableIndex];
                break;
            case Trapezoidal:
                envVal = trapezoidEnvelope[tableIndex];
                break;
            case CosineBell:
                envVal = bellEnvelope[tableIndex];
                break;
        }
        
        return envVal;
    }
    
    void process(juce::AudioBuffer<float>& buffer)
    {
        auto numSamples = buffer.getNumSamples();
        numChannels = buffer.getNumChannels();
        
        circularBuffer.fillBuffer(buffer);
        writePosition = circularBuffer.writePos;
        
        auto* channelDataL = buffer.getWritePointer(0);
        auto* channelDataR = buffer.getWritePointer(1);
        
        for (int i = 0; i < numSamples; i++)
        {
            auto inputL = channelDataL[i];
            auto inputR = channelDataR[i];
            
            spawnGrain((writePosition + i) % circularBuffer.getSize());
            
            float outputL = 0.f;
            float outputR = 0.f;
            for (auto& grain : grainPool) {
                if (grain.isFinished) continue;
                
                if (grain.envPos < 0) {
                    grain.envPos++;
                    continue;
                }

                // Calculate the floating-point read position
                double preciseIndex = grain.currentPos + (grain.envPos * grain.playbackSpeed);
                    
                // Wrap the index around the circular buffer
                double wrappedIndex = std::fmod(preciseIndex, (double)circularBuffer.getSize());

                // Get the two integer neighbors (i and i+1)
                int indexA = static_cast<int>(wrappedIndex);
                int indexB = (indexA + 1) % circularBuffer.getSize();
                    
                // Calculate the fraction (how far we are between A and B)
                float fraction = static_cast<float>(wrappedIndex - indexA);
                
                // Calculate the envelope value given the grain position
                float p = (float)grain.envPos / (float)grain.grainSize;
                int tableIndex = p * (parabolicEnvelope.getNumPoints() - 1);
                float envelope = 0.5f * calculateEnvelope(tableIndex);

                // Fetch samples and interpolate: Output = A + (fraction * (B - A))
                float sampleLA = circularBuffer.read(0, indexA);
                float sampleLB = circularBuffer.read(0, indexB);
                float intrpL = sampleLA + fraction * (sampleLB - sampleLA);
                outputL += intrpL * envelope * grain.spreadL;

                float sampleRA = circularBuffer.read(1, indexA);
                float sampleRB = circularBuffer.read(1, indexB);
                float intrpR = sampleRA + fraction * (sampleRB - sampleRA);
                outputR += intrpR * envelope * grain.spreadR;
                
                if (grainFreeze) {
                    outputL *= 0.7f;
                    outputR *= 0.7f;
                }
                
                // Age the grain
                grain.envPos++;
                
                if (grain.envPos >= grain.grainSize) {
                    if (!grainFreeze) {
                        grain.isFinished = true;
                    }
                    else {
                        grain.envPos = -0.5 * samplesPerGrain;
                        grain.replayCount++;
                        
                        if (grain.replayCount >= 20)
                            grain.isFinished = true;
                    }
                }
                        
            }
            
            outputL = outputGain.processSample(outputL);
            outputR = outputGain.processSample(outputR);
            
            float wet = mixerBlend[0].getTargetValue();
            float dry = 1.f - wet;
            
            
            if (powerBlend[0].getCurrentValue() > 0.0001 || powerBlend[0].getTargetValue() > 0.5)
            {
                channelDataL[i] = (dry * inputL) + (wet * (outputL));
                channelDataR[i] = (dry * inputR) + (wet * (outputR));
            }
            else
            {
                channelDataL[i] = inputL;
                channelDataR[i] = inputR;
            }
        }
        
        cleanGrainPool();
    }
    
private:
    static constexpr auto bufferMaxSamples = 480000; // 5s @ 96k sample rate
    static constexpr float pi = juce::MathConstants<float>::pi;
    
    CircularBuffer circularBuffer;
    std::array<juce::LinearSmoothedValue<float>, 2> powerBlend, mixerBlend;
    
    std::vector<Grain> grainPool;
    juce::dsp::LookupTable<float> parabolicEnvelope, trapezoidEnvelope, bellEnvelope;
    dsp::Gain<float> outputGain;
    juce::Random randomSpawn;
    
    float sampleRate, paramGrainSize, grainDensity, samplesPerGrain, grainPitch, sprayFactor, nextSpawn;
    int numChannels, envelopeType, stereoRange;
    int counter = 0;
    int samplesSinceSpawn = 0;
    int writePosition = { 0 };
    bool grainFreeze = false;
};
