#pragma once

#include "EventWriter.h"

#include <functional>

namespace inlay::emulator
{
class CommandTarget
{
public:
    virtual ~CommandTarget() = default;
    virtual InvocationResult invoke (const juce::String& commandId, const juce::String& method,
                                     const juce::var& arguments) = 0;
};

class CommandProcessor : private juce::Timer
{
public:
    CommandProcessor (juce::File runDirectory, juce::String runId, EventWriter&, CommandStateStore&,
                      CommandTarget&, std::function<void (juce::String)> fatal);
    ~CommandProcessor() override;

    juce::Result preflight();
    void start();
    void stop();
    void processPendingNow();

private:
    void timerCallback() override;
    void reportFatal (const juce::String&);
    bool processSequence (juce::int64 sequence);
    juce::Result saveCursor (juce::int64 nextSequence) const;
    juce::Result loadCursor();

    juce::File runDirectory;
    juce::String runId;
    EventWriter& events;
    CommandStateStore& states;
    CommandTarget& target;
    std::function<void (juce::String)> fatal;
    juce::int64 nextSequence = 1;
    bool cursorLoaded = false;
    bool stopped = false;
};
} // namespace inlay::emulator
