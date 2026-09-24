#pragma once

#include "Field01Processor.h"

class Field01Editor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit Field01Editor (Field01Processor&);
    ~Field01Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class HitButton final : public juce::Button
    {
    public:
        explicit HitButton (const juce::String& name) : Button (name) {}
        void paintButton (juce::Graphics&, bool, bool) override {}
    };

    class DialLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        DialLookAndFeel() = default;
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;

    private:
    };

    void timerCallback() override;
    void drawDrawable (juce::Graphics&, const juce::Drawable*, juce::Rectangle<float>) const;
    void configureDial (juce::Slider&, const juce::String&, double interval);
    juce::Rectangle<int> scaledBounds (float x, float y, float width, float height) const;
    float scale() const;
    juce::Point<float> origin() const;

    Field01Processor& fieldProcessor;
    std::unique_ptr<juce::Drawable> runtimeBase;
    std::array<std::unique_ptr<juce::Drawable>, 3> waveSelectors;
    std::array<std::unique_ptr<juce::Drawable>, 2> rangeSwitches;
    std::unique_ptr<DialLookAndFeel> dialLookAndFeel;

    juce::Slider frequency;
    juce::Slider drift;
    juce::Slider colour;
    juce::Slider level;
    std::array<HitButton, 3> waveformButtons { HitButton ("Sine"), HitButton ("Triangle"), HitButton ("Pulse") };
    HitButton rangeButton { "Range" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driftAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> colourAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rangeAttachment;
    inlay::DefaultUI unlockerUI;
    float scopePhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Field01Editor)
};
