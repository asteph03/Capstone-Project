/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
CapstonePluginAudioProcessor::CapstonePluginAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    parameters = std::make_unique<juce::AudioProcessorValueTreeState>(*this, /*undoManager.get()*/ nullptr, "Params", createParameterLayout());
}

CapstonePluginAudioProcessor::~CapstonePluginAudioProcessor()
{
}

std::unique_ptr<juce::AudioProcessorParameterGroup> CapstonePluginAudioProcessor::createParameterLayout()
{
    // parameters group of all modules
    std::unique_ptr<juce::AudioProcessorParameterGroup> params = std::make_unique<juce::AudioProcessorParameterGroup>("Parameters", "", "");

    // Module name
    // juce::String name = delayProcessor.getName();
    // get params from module processor
    delayProcessor.addParams(*params);

    return params;
}

//==============================================================================
const juce::String CapstonePluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CapstonePluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CapstonePluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CapstonePluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CapstonePluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CapstonePluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int CapstonePluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void CapstonePluginAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String CapstonePluginAudioProcessor::getProgramName (int index)
{
    return {};
}

void CapstonePluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void CapstonePluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = getTotalNumInputChannels();
    
    delayProcessor.prepare(spec);
    
}

void CapstonePluginAudioProcessor::update()
{
    delayProcessor.update(*parameters);
}

void CapstonePluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CapstonePluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif
//==============================================================================

void CapstonePluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    auto audioBlock = dsp::AudioBlock<float>(buffer);
    auto context = dsp::ProcessContextReplacing<float>(audioBlock);
    
    delayProcessor.process(context);
}

//==============================================================================
bool CapstonePluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* CapstonePluginAudioProcessor::createEditor()
{
    //return new CapstonePluginAudioProcessorEditor (*this);
    return new GenericAudioProcessorEditor (*this);
}

//==============================================================================
void CapstonePluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void CapstonePluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CapstonePluginAudioProcessor();
}
