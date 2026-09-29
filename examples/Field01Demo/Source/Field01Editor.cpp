#include "Field01Editor.h"
#include "Field01Assets.h"

namespace
{
constexpr float contentInset = 2.0f;

std::unique_ptr<juce::Drawable> loadDrawable (const char* resourceName)
{
    int bytes = 0;
    if (const auto* data = Field01Assets::getNamedResource (resourceName, bytes); data != nullptr && bytes > 0)
        return juce::Drawable::createFromImageData (data, static_cast<size_t> (bytes));

    return {};
}

// Resize the panel geometry while keeping the original border and corner widths.
std::unique_ptr<juce::Drawable> loadPanelDrawable (float width, float height)
{
    int bytes = 0;
    const auto* data = Field01Assets::getNamedResource ("field01runtimebase_svg", bytes);
    if (data == nullptr || bytes <= 0)
        return {};

    auto svg = juce::parseXML (juce::String::fromUTF8 (data, bytes));
    if (svg == nullptr)
        return {};

    svg->setAttribute ("width", width);
    svg->setAttribute ("height", height);
    svg->setAttribute ("viewBox", "0 0 " + juce::String (width) + " " + juce::String (height));
    if (auto* frame = svg->getChildByAttribute ("id", "panel-frame"))
    {
        const auto resizeRect = [frame, width, height] (const char* id, float widthInset, float heightInset)
        {
            if (auto* rect = frame->getChildByAttribute ("id", id))
            {
                rect->setAttribute ("width", width - widthInset);
                rect->setAttribute ("height", height - heightInset);
            }
        };
        resizeRect ("panel-shell", 0.0f, 0.0f);
        resizeRect ("panel-rim", 4.0f, 4.0f);
        resizeRect ("panel-face", 16.0f, 20.0f);
        if (auto* bottom = frame->getChildByAttribute ("id", "panel-bottom"))
            bottom->setAttribute ("d", "M24 " + juce::String (height - 13.0f)
                                      + " L" + juce::String (width - 24.0f) + " " + juce::String (height - 13.0f));
    }
    if (auto* texture = svg->getChildByAttribute ("id", "panel-texture"))
    {
        texture->setAttribute ("width", width - 20.0f);
        texture->setAttribute ("height", height - 26.0f);
    }
    return juce::Drawable::createFromSVG (*svg);
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
      verticalLayout (juce::SystemStats::getEnvironmentVariable ("LAYOUT", "") == "vertical"),
      designWidth (verticalLayout ? 648.0f : 1080.0f),
      designHeight (verticalLayout ? 1152.0f : 700.0f),
      runtimeBase (loadPanelDrawable (designWidth, designHeight)),
      panelScrew (loadDrawable ("panelscrew_svg")),
      panelTitle (loadDrawable ("paneltitle_svg")),
      panelBrand (loadDrawable ("panelbrand_svg")),
      scope (loadDrawable ("scope_svg")),
      waveLabel (loadDrawable ("wavelabel_svg")),
      rangeLabels (loadDrawable ("rangelabels_svg")),
      oscillatorLabel (loadDrawable ("oscillatorlabel_svg")),
      oscillatorDescription (loadDrawable ("oscillatordescription_svg")),
      dialFaces { loadDrawable ("dialfrequency_svg"), loadDrawable ("dialdrift_svg"),
                  loadDrawable ("dialcolour_svg"), loadDrawable ("diallevel_svg") },
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
    if (verticalLayout)
    {
        setResizeLimits (360, 640, 810, 1440);
        getConstrainer()->setFixedAspectRatio (9.0 / 16.0);
        setSize (437, 777);
    }
    else
    {
        setResizeLimits (648, 420, 1620, 1050);
        setSize (1080, 700);
    }
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

// Positions are in artwork coordinates and shared by painting and hit targets.
juce::Point<float> Field01Editor::dialPosition (int index) const
{
    constexpr std::array<float, 4> horizontalX { 87.0f, 336.0f, 584.0f, 833.0f };
    return verticalLayout ? juce::Point<float> { 110.0f + (index % 2) * 268.0f, 570.0f + (index / 2) * 264.0f }
                          : juce::Point<float> { horizontalX[static_cast<size_t> (index)], 462.0f };
}

juce::Point<float> Field01Editor::wavePosition() const
{
    return verticalLayout ? juce::Point<float> { 54.0f, 403.0f } : juce::Point<float> { 752.0f, 185.0f };
}

juce::Point<float> Field01Editor::rangePosition() const
{
    return verticalLayout ? juce::Point<float> { 320.0f, 369.0f } : juce::Point<float> { 752.0f, 272.0f };
}

juce::Rectangle<float> Field01Editor::scopeBounds() const
{
    return verticalLayout ? juce::Rectangle<float> { 54.0f, 140.0f, 540.0f, 246.0f * 540.0f / 650.0f }
                          : juce::Rectangle<float> { 54.0f, 140.0f, 650.0f, 246.0f };
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
    g.addTransform (juce::AffineTransform::scale (currentScale).translated (offset.x, offset.y));

    drawDrawable (g, runtimeBase.get(), { 0.0f, 0.0f, designWidth, designHeight });

    drawDrawable (g, panelTitle.get(), { 52.0f, 38.0f, 214.0f, 52.0f });
    drawDrawable (g, panelBrand.get(), { designWidth - 176.0f, 44.0f, 122.0f, 38.0f });
    for (const auto x : { 21.0f, designWidth - 41.0f })
        for (const auto y : { 22.0f, designHeight - 50.0f })
            drawDrawable (g, panelScrew.get(), { x, y, 20.0f, 28.0f });

    const auto wave = wavePosition();
    const auto range = rangePosition();
    const auto controlsTop = verticalLayout ? 517.0f : 415.0f;
    g.setColour (juce::Colour (0xffa6aa97));
    g.drawLine (54.0f, 111.0f, designWidth - 54.0f, 111.0f);
    g.drawLine (54.0f, controlsTop, designWidth - 54.0f, controlsTop);
    if (! verticalLayout)
    {
        g.setColour (juce::Colour (0xffb7baa6));
        g.drawLine (752.0f, 258.0f, 1026.0f, 258.0f);
    }
    g.setColour (juce::Colour (0xffc3c5b1));
    if (verticalLayout)
    {
        g.drawLine (324.0f, 596.0f, 324.0f, 713.0f);
        g.drawLine (324.0f, 860.0f, 324.0f, 977.0f);
    }
    else
        for (const auto x : { 291.0f, 540.0f, 788.0f })
            g.drawLine (x, 488.0f, x, 605.0f);

    drawDrawable (g, oscillatorLabel.get(), { 54.0f, controlsTop + 14.0f, 76.0f, 18.0f });
    drawDrawable (g, oscillatorDescription.get(), { designWidth - 202.0f, controlsTop + 8.0f, 148.0f, 18.0f });
    drawDrawable (g, scope.get(), scopeBounds());
    drawDrawable (g, waveLabel.get(), { wave.x, wave.y - 34.0f, 122.0f, 18.0f });
    drawDrawable (g, rangeLabels.get(), { range.x, range.y, 274.0f, 88.0f });
    for (int index = 0; index < 4; ++index)
    {
        const auto position = dialPosition (index);
        drawDrawable (g, dialFaces[static_cast<size_t> (index)].get(), { position.x, position.y, 160.0f, 174.0f });
    }

    const auto waveform = juce::roundToInt (fieldProcessor.getParameters().getRawParameterValue ("waveform")->load());
    drawDrawable (g, waveSelectors[static_cast<size_t> (juce::jlimit (0, 2, waveform))].get(), { wave.x, wave.y, 274.0f, 57.0f });
    const auto audioRange = fieldProcessor.getParameters().getRawParameterValue ("range")->load() >= 0.5f;
    drawDrawable (g, rangeSwitches[static_cast<size_t> (audioRange ? 1 : 0)].get(), { range.x + 92.0f, range.y + 48.0f, 90.0f, 40.0f });

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
    const std::array<juce::String, 4> values {
        frequencyText, dialValue ("drift", " %"), dialValue ("colour", " %"),
        juce::String (fieldProcessor.getParameters().getRawParameterValue ("level")->load(), 1) + " dB"
    };
    for (int index = 0; index < 4; ++index)
    {
        const auto position = dialPosition (index);
        g.drawFittedText (values[static_cast<size_t> (index)], juce::roundToInt (position.x + 13.0f),
                          juce::roundToInt (position.y + 177.0f), 134, 16, juce::Justification::centred, 1);
    }

    // Keep the live trace and readouts in the same coordinate system as the scope SVG.
    const auto scopeArea = scopeBounds();
    const auto scopeScale = scopeArea.getWidth() / 650.0f;
    juce::Graphics::ScopedSaveState scopeState (g);
    g.addTransform (juce::AffineTransform::translation (-54.0f, -140.0f)
                        .scaled (scopeScale).translated (scopeArea.getX(), scopeArea.getY()));

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
    const std::array<juce::Slider*, 4> dials { &frequency, &drift, &colour, &level };
    for (int index = 0; index < 4; ++index)
    {
        const auto position = dialPosition (index);
        dials[static_cast<size_t> (index)]->setBounds (scaledBounds (position.x, position.y, 160.0f, 160.0f));
    }
    const auto wave = wavePosition();
    for (int index = 0; index < static_cast<int> (waveformButtons.size()); ++index)
        waveformButtons[static_cast<size_t> (index)].setBounds (scaledBounds (wave.x + index * 91.0f, wave.y, 90.0f, 57.0f));
    const auto range = rangePosition();
    rangeButton.setBounds (scaledBounds (range.x + 28.0f, range.y + 37.0f,
                                         216.0f, 62.0f));
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
