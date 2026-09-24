#include "Field01Editor.h"
#include "Field01Assets.h"

namespace
{
constexpr float designWidth = 1080.0f;
constexpr float designHeight = 700.0f;
constexpr float contentInset = 32.0f;

std::unique_ptr<juce::Drawable> loadDrawable (const char* resourceName)
{
    int bytes = 0;
    if (const auto* data = Field01Assets::getNamedResource (resourceName, bytes); data != nullptr && bytes > 0)
        return juce::Drawable::createFromImageData (data, static_cast<size_t> (bytes));

    return {};
}

} // namespace

void Field01Editor::DialLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                        float sliderPosProportional, float rotaryStartAngle,
                                                        float rotaryEndAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (width), static_cast<float> (height));
    const auto angle = juce::jmap (sliderPosProportional, rotaryStartAngle, rotaryEndAngle);
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto pointerLength = diameter * 0.11f;
    const auto pointerWidth = diameter * 0.025f;
    const auto pointerTop = bounds.getCentreY() - diameter * 0.255f;
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (angle, bounds.getCentreX(), bounds.getCentreY()));
    g.setColour (juce::Colour (0x55343a31));
    g.fillRoundedRectangle (bounds.getCentreX() - pointerWidth * 0.5f,
                            pointerTop + juce::jmax (0.75f, diameter * 0.006f),
                            pointerWidth,
                            pointerLength,
                            pointerWidth * 0.5f);
    g.setColour (juce::Colour (0xffe7ead1));
    g.fillRoundedRectangle (bounds.getCentreX() - pointerWidth * 0.5f,
                            pointerTop,
                            pointerWidth,
                            pointerLength,
                            pointerWidth * 0.5f);
}

Field01Editor::Field01Editor (Field01Processor& processorToEdit)
    : AudioProcessorEditor (processorToEdit),
      fieldProcessor (processorToEdit),
      runtimeBase (loadDrawable ("field01runtimebase_svg")),
      waveSelectors { loadDrawable ("waveselectorsine_svg"), loadDrawable ("waveselectortriangle_svg"), loadDrawable ("waveselectorpulse_svg") },
      rangeSwitches { loadDrawable ("rangelow_svg"), loadDrawable ("rangeaudio_svg") },
      dialLookAndFeel (std::make_unique<DialLookAndFeel>()),
      unlockerUI (processorToEdit.getUnlocker())
{
    configureDial (frequency, "frequency", 0.01);
    configureDial (drift, "drift", 0.01);
    configureDial (colour, "colour", 0.01);
    configureDial (level, "level", 0.1);

    auto& parameters = fieldProcessor.getParameters();
    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, "frequency", frequency);
    driftAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, "drift", drift);
    colourAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, "colour", colour);
    levelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, "level", level);

    for (int index = 0; index < static_cast<int> (waveformButtons.size()); ++index)
    {
        addAndMakeVisible (waveformButtons[static_cast<size_t> (index)]);
        waveformButtons[static_cast<size_t> (index)].onClick = [this, index]
        {
            if (auto* parameter = fieldProcessor.getParameters().getParameter ("waveform"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (index)));
        };
    }

    rangeButton.setClickingTogglesState (true);
    addAndMakeVisible (rangeButton);
    rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, "range", rangeButton);

    addChildComponent (unlockerUI);
    setResizable (true, true);
    setResizeLimits (648, 420, 1620, 1050);
    setSize (1080, 700);
    startTimerHz (30);
}

Field01Editor::~Field01Editor()
{
    stopTimer();
    frequency.setLookAndFeel (nullptr);
    drift.setLookAndFeel (nullptr);
    colour.setLookAndFeel (nullptr);
    level.setLookAndFeel (nullptr);
}

void Field01Editor::configureDial (juce::Slider& dial, const juce::String& parameterId, double interval)
{
    dial.setComponentID (parameterId);
    dial.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    dial.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dial.setRange (0.0, 1.0, interval);
    dial.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    dial.setLookAndFeel (dialLookAndFeel.get());
    addAndMakeVisible (dial);
}

float Field01Editor::scale() const
{
    const auto available = getLocalBounds().toFloat().reduced (contentInset);
    return juce::jmin (available.getWidth() / designWidth, available.getHeight() / designHeight);
}

juce::Point<float> Field01Editor::origin() const
{
    const auto currentScale = scale();
    return { (getWidth() - designWidth * currentScale) * 0.5f, (getHeight() - designHeight * currentScale) * 0.5f };
}

juce::Rectangle<int> Field01Editor::scaledBounds (float x, float y, float width, float height) const
{
    const auto currentScale = scale();
    const auto offset = origin();
    return juce::Rectangle<float> (offset.x + x * currentScale, offset.y + y * currentScale, width * currentScale, height * currentScale).toNearestInt();
}

void Field01Editor::drawDrawable (juce::Graphics& g, const juce::Drawable* drawable, juce::Rectangle<float> bounds) const
{
    if (drawable != nullptr)
        drawable->drawWithin (g, bounds, juce::RectanglePlacement::centred, 1.0f);
}

void Field01Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff242b24));
    const auto currentScale = scale();
    const auto offset = origin();
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::translation (offset.x, offset.y).scaled (currentScale));

    drawDrawable (g, runtimeBase.get(), { 0.0f, 0.0f, designWidth, designHeight });

    const auto waveform = juce::roundToInt (fieldProcessor.getParameters().getRawParameterValue ("waveform")->load());
    drawDrawable (g, waveSelectors[static_cast<size_t> (juce::jlimit (0, 2, waveform))].get(), { 752.0f, 185.0f, 274.0f, 57.0f });
    const auto audioRange = fieldProcessor.getParameters().getRawParameterValue ("range")->load() >= 0.5f;
    drawDrawable (g, rangeSwitches[static_cast<size_t> (audioRange ? 1 : 0)].get(), { 844.0f, 320.0f, 90.0f, 40.0f });

    const auto frequencyValue = fieldProcessor.getParameters().getRawParameterValue ("frequency")->load();
    const auto driftValue = fieldProcessor.getParameters().getRawParameterValue ("drift")->load();
    const auto colourValue = fieldProcessor.getParameters().getRawParameterValue ("colour")->load();
    const auto frequencyText = juce::String (frequencyValue, frequencyValue < 100.0f ? 1 : 0) + " Hz";
    const auto dialValue = [this] (const char* id, const juce::String& suffix)
    {
        return juce::String (fieldProcessor.getParameters().getRawParameterValue (id)->load() * 100.0f, 0) + suffix;
    };

    g.setFont (juce::Font (juce::FontOptions ("Andale Mono", 9.5f, juce::Font::plain)));
    g.setColour (juce::Colour (0xff777b69));
    g.drawFittedText (frequencyText, 100, 639, 134, 16, juce::Justification::centred, 1);
    g.drawFittedText (dialValue ("drift", " %"), 349, 639, 134, 16, juce::Justification::centred, 1);
    g.drawFittedText (dialValue ("colour", " %"), 597, 639, 134, 16, juce::Justification::centred, 1);
    g.drawFittedText (juce::String (fieldProcessor.getParameters().getRawParameterValue ("level")->load(), 1) + " dB", 846, 639, 134, 16, juce::Justification::centred, 1);

    const auto frequencyPosition = juce::jlimit (0.0f, 1.0f, (frequencyValue - 20.0f) / 1980.0f);
    const auto scopeCycles = (audioRange ? 1.2f : 0.45f) + frequencyPosition * (audioRange ? 2.0f : 0.75f);
    const auto displayWaveform = [waveform] (float phase)
    {
        const auto sine = std::sin (phase);
        if (waveform == 1)
            return 2.0f / juce::MathConstants<float>::pi * std::asin (sine);
        if (waveform == 2)
            return sine >= 0.0f ? 1.0f : -1.0f;
        return sine;
    };

    juce::Path trace;
    for (int point = 0; point <= 520; ++point)
    {
        const auto normalizedX = static_cast<float> (point) / 520.0f;
        const auto scopeDetune = 1.0f + 0.02f * driftValue * std::sin (scopePhase * 0.173f);
        const auto theta = normalizedX * juce::MathConstants<float>::twoPi * scopeCycles * scopeDetune + scopePhase;
        const auto primary = displayWaveform (theta);
        const auto overtone = displayWaveform (theta * 2.0f);
        const auto value = primary * (1.0f - colourValue * 0.35f) + overtone * colourValue * 0.35f;
        const auto x = 86.0f + normalizedX * 588.0f;
        const auto y = 262.5f - value * 51.0f;
        point == 0 ? trace.startNewSubPath (x, y) : trace.lineTo (x, y);
    }
    g.setColour (juce::Colour (0x22c6dca6));
    g.strokePath (trace, juce::PathStrokeType (7.0f));
    g.setColour (juce::Colour (0xffc6dca6));
    g.strokePath (trace, juce::PathStrokeType (1.65f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setFont (juce::Font (juce::FontOptions ("Andale Mono", 10.0f, juce::Font::plain)));
    g.setColour (juce::Colour (0xffc5d9ad));
    g.drawText ("OSC / A", 88, 166, 100, 18, juce::Justification::left);
    g.drawText (waveform == 0 ? "SINE" : waveform == 1 ? "TRIANGLE" : "PULSE", 570, 166, 100, 18, juce::Justification::right);
    g.fillEllipse (88.0f, 349.0f, 4.0f, 4.0f);
    g.drawText ("SIGNAL", 100, 342, 100, 18, juce::Justification::left);
    g.drawText (frequencyText, 580, 345, 92, 18, juce::Justification::right);
}

void Field01Editor::resized()
{
    frequency.setBounds (scaledBounds (87.0f, 462.0f, 160.0f, 160.0f));
    drift.setBounds (scaledBounds (336.0f, 462.0f, 160.0f, 160.0f));
    colour.setBounds (scaledBounds (584.0f, 462.0f, 160.0f, 160.0f));
    level.setBounds (scaledBounds (833.0f, 462.0f, 160.0f, 160.0f));
    for (int index = 0; index < static_cast<int> (waveformButtons.size()); ++index)
        waveformButtons[static_cast<size_t> (index)].setBounds (scaledBounds (752.0f + index * 91.0f, 185.0f, 90.0f, 57.0f));
    rangeButton.setBounds (scaledBounds (780.0f, 309.0f, 216.0f, 62.0f));
    unlockerUI.setBounds (getLocalBounds());
    unlockerUI.toFront (false);
}

bool Field01Editor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress ('L', juce::ModifierKeys::commandModifier, 0))
    {
        fieldProcessor.getUnlocker().logout();
        return true;
    }

    return false;
}

void Field01Editor::timerCallback()
{
    const auto scopeFrequency = fieldProcessor.getParameters().getRawParameterValue ("frequency")->load();
    const auto driftValue = fieldProcessor.getParameters().getRawParameterValue ("drift")->load();
    const auto isAudioRange = fieldProcessor.getParameters().getRawParameterValue ("range")->load() >= 0.5f;
    const auto motionRate = isAudioRange ? scopeFrequency : scopeFrequency * 0.02f;
    const auto scopeDetune = 1.0f + 0.02f * driftValue * std::sin (scopePhase * 0.173f);
    scopePhase = std::fmod (scopePhase + motionRate * scopeDetune * 0.00015f, juce::MathConstants<float>::twoPi);
    repaint();
}

juce::AudioProcessorEditor* Field01Processor::createEditor()
{
    return new Field01Editor (*this);
}
