#pragma once

#include "Arpeggiator.h"

class ArpeggiatorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ArpeggiatorEditor (Arpeggiator& processorToEdit);

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void updateUnlockState();

    Arpeggiator& processor;
    std::unique_ptr<juce::GenericAudioProcessorEditor> genericEditor;
    inlay::DefaultUI defaultUI;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArpeggiatorEditor)
};
