#pragma once

#include "Protocol.h"

#include <juce_cryptography/juce_cryptography.h>

namespace inlay::emulator
{
juce::Result writeJsonAtomically (const juce::File& target, const juce::var& value);

class EventWriter
{
public:
    EventWriter (juce::File eventsDir, juce::String runId);

    juce::Result emit (const juce::String& type, const juce::var& payload,
                       const juce::String& commandId = {});
    bool hasCommandCompletion (const juce::String& commandId) const;

private:
    juce::File eventsDir;
    juce::String runId;
    juce::int64 nextId = 1;
};

enum class CommandPhase { processing, completed };

struct StoredCommand
{
    juce::String commandId;
    juce::int64 sequence = 0;
    CommandPhase phase = CommandPhase::processing;
    std::optional<InvocationResult> outcome;
    bool completionEmitted = false;
};

class CommandStateStore
{
public:
    explicit CommandStateStore (juce::File stateDir);

    juce::Result validateAll() const;
    juce::Result markProcessing (const juce::String& commandId, juce::int64 sequence);
    juce::Result markCompleted (const juce::String& commandId, const InvocationResult& outcome);
    std::optional<StoredCommand> load (const juce::String& commandId) const;
    bool exists (const juce::String& commandId) const;
    juce::Result setCompletionEmitted (const juce::String& commandId);

private:
    juce::File stateDir;

    juce::File fileFor (const juce::String& commandId) const;
    juce::Result save (const StoredCommand&) const;
};
} // namespace inlay::emulator
