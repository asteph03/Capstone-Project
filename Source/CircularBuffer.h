/*
  ==============================================================================

    CircularBuffer.h
    Created: 23 Jan 2026 1:05:45pm
    Author:  Aidan Stephenson

  ==============================================================================
*/

#pragma once

using namespace juce;

class CircularBuffer
{
public:
    
    void prepare(dsp::ProcessSpec& spec)
    {
        double bufferSize = spec.sampleRate * 2.0;
        int numChannels = spec.numChannels;
        
        circularBuffer.setSize(numChannels, (int)bufferSize);
    }
    
    int getSize()
    {
        int size = circularBuffer.getNumSamples();
        return size;
    }
    
    void clearBuffer()
    {
        circularBuffer.clear();
    }
    
    float read(int channel, int index)
    {
        return circularBuffer.getSample(channel, index);
    }
    
    void fillBuffer(AudioBuffer<float>& buffer)
    {
        int bufferSize = buffer.getNumSamples();
        int circularBufferSize = circularBuffer.getNumSamples();
        
        int numChannels = circularBuffer.getNumChannels();
        
        for (int channel = 0; channel < numChannels; channel++)
        {
            auto* input = buffer.getReadPointer(channel);
            
            if (circularBufferSize > bufferSize + writePos)
            {
                // enough space in circularBuffer for input buffer -> no need to wrap
                circularBuffer.copyFromWithRamp(channel, writePos, input, bufferSize, 0.1f, 0.1f);
            }
            else
            {
                // not enough space for input buffer -> wrap to first index
                int preWrapSamples = circularBufferSize - writePos;
                int postWrapSamples = bufferSize - preWrapSamples;
                
                circularBuffer.copyFromWithRamp(channel, writePos, input, preWrapSamples, 0.1f, 0.1f);
                circularBuffer.copyFromWithRamp(channel, 0, input, postWrapSamples, 0.1f, 0.1f);
            }
        }
        
        //DBG("writePos = " << writePos);
        //DBG("circularBufferSize = " << circularBufferSize);
        //DBG("bufferSize = " << bufferSize);
        
        writePos += bufferSize;
        writePos = writePos % circularBufferSize;
    }
    
    int writePos = { 0 };
    
private:
    AudioBuffer<float> circularBuffer;
};
