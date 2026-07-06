#pragma once

#include <JuceHeader.h>
#include <inlay_product_unlocking/inlay_product_unlocking.h>

class Arpeggiator : public juce::AudioProcessor
{
public:
    Arpeggiator();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    bool isMidiEffect() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    bool isLocked() const;
    bool isUnlocked() const;
    inlay::Unlocker& getUnlocker();
    const inlay::Unlocker& getUnlocker() const;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    juce::AudioParameterFloat* speed = nullptr;
    int currentNote = 0;
    int lastNoteValue = 0;
    int time = 0;
    float rate = 0.0f;
    juce::SortedSet<int> notes;
    inlay::Unlocker unlocker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Arpeggiator)
};
