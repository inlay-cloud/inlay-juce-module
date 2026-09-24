#pragma once

#include <JuceHeader.h>
#include <inlay_product_unlocking/inlay_product_unlocking.h>

class Field01Processor final : public juce::AudioProcessor
{
public:
    Field01Processor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;
    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isUnlocked() const;
    inlay::Unlocker& getUnlocker();
    juce::AudioProcessorValueTreeState& getParameters();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    inlay::Unlocker unlocker;
    juce::AudioProcessorValueTreeState parameters;
    double currentSampleRate = 44100.0;
    double phase = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> levelGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Field01Processor)
};
