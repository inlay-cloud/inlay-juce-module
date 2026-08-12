#pragma once

#include "CommandProcessor.h"

#include "../../modules/inlay_product_unlocking/internal/UnlockerImpl.h"

#include <functional>
#include <memory>

namespace inlay::emulator
{
class Emulator final : private juce::ChangeListener, private CommandTarget
{
public:
    Emulator (juce::File runDirectory, EmulatorConfig, std::function<void (juce::String)> fatal);
    ~Emulator() override;

    juce::Result start();
    void stop (const juce::String& reason);

private:
    friend class EmulatorTests;

    class EventBrowser;

    InvocationResult invoke (const juce::String& commandId, const juce::String& method,
                             const juce::var& arguments) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    static juce::String redactErrorForEvent (const juce::String&);
    static juce::var redactCommandOutput (juce::var);

    juce::File runDirectory;
    EmulatorConfig config;
    std::function<void (juce::String)> fatal;
    EventWriter events;
    CommandStateStore states;
    juce::ChangeBroadcaster broadcaster;
    std::unique_ptr<inlay::internal::UnlockerImpl> unlocker;
    std::unique_ptr<CommandProcessor> commands;
    bool started = false;
    bool stopped = false;
};
} // namespace inlay::emulator
