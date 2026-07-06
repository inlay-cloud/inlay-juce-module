#include "ArpeggiatorEditor.h"

ArpeggiatorEditor::ArpeggiatorEditor (Arpeggiator& processorToEdit)
    : juce::AudioProcessorEditor (processorToEdit),
      processor (processorToEdit),
      genericEditor (std::make_unique<juce::GenericAudioProcessorEditor> (processorToEdit)),
      defaultUI (processorToEdit.getUnlocker())
{
    addAndMakeVisible (*genericEditor);
    addAndMakeVisible (defaultUI);

    setWantsKeyboardFocus (true);
    setSize (genericEditor->getWidth(), genericEditor->getHeight());
    updateUnlockState();
}

void ArpeggiatorEditor::resized()
{
    auto bounds = getLocalBounds();
    genericEditor->setBounds (bounds);
    defaultUI.setBounds (bounds);
    defaultUI.toFront (false);
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
    const auto shouldShowDefaultUI = ! processor.isUnlocked();

    if (defaultUI.isVisible() != shouldShowDefaultUI)
        defaultUI.setVisible (shouldShowDefaultUI);

    if (shouldShowDefaultUI)
        defaultUI.toFront (false);
}

juce::AudioProcessorEditor* Arpeggiator::createEditor()
{
    return new ArpeggiatorEditor (*this);
}
