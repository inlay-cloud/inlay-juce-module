#include "ArpeggiatorEditor.h"

ArpeggiatorEditor::ArpeggiatorEditor (Arpeggiator& processorToEdit)
    : juce::AudioProcessorEditor (processorToEdit),
      processor (processorToEdit),
      genericEditor (std::make_unique<juce::GenericAudioProcessorEditor> (processorToEdit)),
      unlockerUI (processorToEdit.getUnlocker())
{
    addAndMakeVisible (*genericEditor);
    addAndMakeVisible (unlockerUI);

    setWantsKeyboardFocus (true);
    setSize (genericEditor->getWidth(), genericEditor->getHeight());
    updateUnlockState();
}

void ArpeggiatorEditor::resized()
{
    auto bounds = getLocalBounds();
    genericEditor->setBounds (bounds);
    unlockerUI.setBounds (bounds);
    unlockerUI.toFront (false);
}

bool ArpeggiatorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress ('L', juce::ModifierKeys::commandModifier, 0))
    {
        processor.getUnlocker().logout();
        return true;
    }

    return false;
}

void ArpeggiatorEditor::updateUnlockState()
{
    const auto shouldUnlockerUI = ! processor.isUnlocked();

    if (unlockerUI.isVisible() != shouldUnlockerUI)
        unlockerUI.setVisible (shouldUnlockerUI);

    if (shouldUnlockerUI)
        unlockerUI.toFront (false);
}

juce::AudioProcessorEditor* Arpeggiator::createEditor()
{
    return new ArpeggiatorEditor (*this);
}
