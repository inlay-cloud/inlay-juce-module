#include "Arpeggiator.h"

namespace {
    inlay::Unlocker::Config makeUnlockerConfig() {
        inlay::Unlocker::Config config;
        // Replace these values with your company's Inlay credentials and product ID.
        config.productId = "01KP44MGXGAJGBZS9KTWT3Z0NZ";
        config.publicKey = "10001,c7d83d349e3a51300895076870856e8afca6a875c2e74d0c47fc27a1c4e9e039bc7aa62467bcb775e24e9b518a094d685cae72a396c1e854cb7fda36c75a580679f8b1d9badf3e30fa03d98e1aebfa089fde2c6e845be8d76c1e24c5af19ba52475db548bd3c7cf77926cd2b2a02d10f833306c4e3d31aa1be8fdc8f4c12f6007c4e9708ec779dfdb41e51639321f7ed2022a18f664af8bd31e4eb7594c4e3e0bb4acb22e222a7fadb71b52db257b7913916f218479a5f275798973ebfe21711cb3aa38beaca8c399e39093ea284a82c2b3bd42ac654749b078fdc16f69b25dea8b02c743f1f164a06fc401a924943e3c6b056fc6a7f057e285ab9ae762f09bf";
        config.apiURL = "https://api-dev.inlay.cloud";

        return config;
    }
} // namespace

Arpeggiator::Arpeggiator()
    : AudioProcessor (BusesProperties()),
      unlocker (makeUnlockerConfig())
{
    addParameter (speed = new juce::AudioParameterFloat ("speed", "Arpeggiator Speed", 0.0, 1.0, 0.5));
    unlocker.startup();
}

void Arpeggiator::prepareToPlay (double sampleRate, int)
{
    notes.clear();
    currentNote = 0;
    lastNoteValue = -1;
    time = 0;
    rate = static_cast<float> (sampleRate);
}

void Arpeggiator::releaseResources()
{
}

void Arpeggiator::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    jassert (buffer.getNumChannels() == 0);

    auto numSamples = buffer.getNumSamples();
    auto noteDuration = static_cast<int> (std::ceil (rate * 0.25f * (0.1f + (1.0f - (*speed)))));

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
            notes.add (msg.getNoteNumber());
        else if (msg.isNoteOff())
            notes.removeValue (msg.getNoteNumber());
    }

    midi.clear();

    if ((time + numSamples) >= noteDuration)
    {
        auto offset = juce::jmax (0, juce::jmin ((int) (noteDuration - time), numSamples - 1));

        if (lastNoteValue > 0)
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, lastNoteValue), offset);
            lastNoteValue = -1;
        }

        if (notes.size() > 0)
        {
            currentNote = (currentNote + 1) % notes.size();
            lastNoteValue = notes[currentNote];
            midi.addEvent (juce::MidiMessage::noteOn (1, lastNoteValue, (juce::uint8) 127), offset);
        }
    }

    time = (time + numSamples) % noteDuration;
}

bool Arpeggiator::isMidiEffect() const
{
    return true;
}

bool Arpeggiator::hasEditor() const
{
    return true;
}

bool Arpeggiator::isLocked() const
{
    return unlocker.isLocked();
}

bool Arpeggiator::isUnlocked() const
{
    return ! unlocker.isLocked();
}

inlay::Unlocker& Arpeggiator::getUnlocker()
{
    return unlocker;
}

const inlay::Unlocker& Arpeggiator::getUnlocker() const
{
    return unlocker;
}

const juce::String Arpeggiator::getName() const
{
    return "Arpeggiator";
}

bool Arpeggiator::acceptsMidi() const
{
    return true;
}

bool Arpeggiator::producesMidi() const
{
    return true;
}

double Arpeggiator::getTailLengthSeconds() const
{
    return 0;
}

int Arpeggiator::getNumPrograms()
{
    return 1;
}

int Arpeggiator::getCurrentProgram()
{
    return 0;
}

void Arpeggiator::setCurrentProgram (int)
{
}

const juce::String Arpeggiator::getProgramName (int)
{
    return {};
}

void Arpeggiator::changeProgramName (int, const juce::String&)
{
}

void Arpeggiator::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream (destData, true).writeFloat (*speed);
}

void Arpeggiator::setStateInformation (const void* data, int sizeInBytes)
{
    speed->setValueNotifyingHost (juce::MemoryInputStream (data, static_cast<size_t> (sizeInBytes), false).readFloat());
}
