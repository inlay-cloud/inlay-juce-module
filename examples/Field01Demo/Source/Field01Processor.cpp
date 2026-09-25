#include "Field01Processor.h"
#include "Field01Editor.h"

namespace
{
constexpr auto productId = "01M2T43K1H7CW2MRAKDQDNC33E";
constexpr auto publicKey = "10001,c7d83d349e3a51300895076870856e8afca6a875c2e74d0c47fc27a1c4e9e039bc7aa62467bcb775e24e9b518a094d685cae72a396c1e854cb7fda36c75a580679f8b1d9badf3e30fa03d98e1aebfa089fde2c6e845be8d76c1e24c5af19ba52475db548bd3c7cf77926cd2b2a02d10f833306c4e3d31aa1be8fdc8f4c12f6007c4e9708ec779dfdb41e51639321f7ed2022a18f664af8bd31e4eb7594c4e3e0bb4acb22e222a7fadb71b52db257b7913916f218479a5f275798973ebfe21711cb3aa38beaca8c399e39093ea284a82c2b3bd42ac654749b078fdc16f69b25dea8b02c743f1f164a06fc401a924943e3c6b056fc6a7f057e285ab9ae762f09bf";

inlay::Unlocker::Config makeUnlockerConfig()
{
    inlay::Unlocker::Config config;
    config.productId = productId;
    config.publicKey = publicKey;
    config.apiURL = "https://api-dev.inlay.cloud";
    return config;
}

float makeWaveform (int waveform, double phase)
{
    const auto sine = std::sin (phase);

    switch (waveform)
    {
        case 1: return static_cast<float> (2.0 / juce::MathConstants<double>::pi * std::asin (sine));
        case 2: return sine >= 0.0 ? 1.0f : -1.0f;
        default: return static_cast<float> (sine);
    }
}
} // namespace

Field01Processor::Field01Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      unlocker (makeUnlockerConfig()),
      parameters (*this, nullptr, "FIELD01", createParameterLayout())
{
    unlocker.startup();
}

juce::AudioProcessorValueTreeState::ParameterLayout Field01Processor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> ("waveform", "Waveform", juce::StringArray { "Sine", "Triangle", "Pulse" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> ("range", "Audio Range", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("frequency", "Frequency", juce::NormalisableRange<float> (20.0f, 2000.0f, 0.01f, 0.35f), 220.0f, "Hz"));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("drift", "Drift", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.12f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("colour", "Colour", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.58f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("level", "Level", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -12.0f, "dB"));
    return layout;
}

void Field01Processor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    phase = 0.0;
    levelGain.reset (sampleRate, 0.02);
    levelGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("level")->load()));
}

void Field01Processor::releaseResources() {}

void Field01Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (! isUnlocked())
        return;

    const auto frequency = parameters.getRawParameterValue ("frequency")->load();
    const auto drift = parameters.getRawParameterValue ("drift")->load();
    const auto colour = parameters.getRawParameterValue ("colour")->load();
    levelGain.setTargetValue (juce::Decibels::decibelsToGain (parameters.getRawParameterValue ("level")->load()));
    const auto waveform = juce::roundToInt (parameters.getRawParameterValue ("waveform")->load());
    const auto isAudioRange = parameters.getRawParameterValue ("range")->load() >= 0.5f;
    const auto baseFrequency = isAudioRange ? frequency : frequency * 0.02f;
    const auto phaseIncrement = juce::MathConstants<double>::twoPi * baseFrequency / currentSampleRate;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto level = levelGain.getNextValue();
        const auto detune = 1.0 + 0.02 * static_cast<double> (drift) * std::sin (phase * 0.173);
        const auto primary = makeWaveform (waveform, phase);
        const auto overtone = makeWaveform (waveform, phase * 2.0);
        const auto output = level * ((1.0f - colour * 0.35f) * primary + colour * 0.35f * overtone);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, sample, output);

        phase = std::fmod (phase + phaseIncrement * detune, juce::MathConstants<double>::twoPi);
    }
}

bool Field01Processor::hasEditor() const { return true; }
bool Field01Processor::isUnlocked() const { return ! unlocker.isLocked(); }
inlay::Unlocker& Field01Processor::getUnlocker() { return unlocker; }
juce::AudioProcessorValueTreeState& Field01Processor::getParameters() { return parameters; }
const juce::String Field01Processor::getName() const { return "FIELD / 01"; }
bool Field01Processor::acceptsMidi() const { return false; }
bool Field01Processor::producesMidi() const { return false; }
bool Field01Processor::isMidiEffect() const { return false; }
double Field01Processor::getTailLengthSeconds() const { return 0.0; }
int Field01Processor::getNumPrograms() { return 1; }
int Field01Processor::getCurrentProgram() { return 0; }
void Field01Processor::setCurrentProgram (int) {}
const juce::String Field01Processor::getProgramName (int) { return {}; }
void Field01Processor::changeProgramName (int, const juce::String&) {}

void Field01Processor::getStateInformation (juce::MemoryBlock& destination)
{
    if (const auto state = parameters.copyState(); state.isValid())
        juce::MemoryOutputStream (destination, true).writeString (state.toXmlString());
}

void Field01Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = juce::parseXML (juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes)); xml != nullptr)
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
