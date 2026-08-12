#pragma once

#include "../../modules/inlay_product_unlocking/Unlocker.h"

#include <optional>

namespace inlay::emulator
{
constexpr int protocolVersion = 1;

struct EmulatorConfig
{
    juce::String runId, apiUrl, productId, publicKey;
    juce::File storageDir;
    juce::String deviceIDToUse;
};

struct SafeError
{
    juce::String code, message;
};

struct InvocationResult
{
    juce::String outcome;
    juce::var output;
    std::optional<SafeError> error;
};

juce::Result loadConfig (const juce::File& runDirectory, EmulatorConfig&);
juce::Result validateWritableRunLocalDirectory (const juce::File& runDirectory,
                                                 const juce::File& directory,
                                                 const juce::String& name);
juce::var serializeStatus (inlay::Unlocker::Status);
juce::var serializeAppUpdate (const std::optional<inlay::Unlocker::AppUpdate>&);
juce::var makeUnlockerErrorPayload (const juce::String& message);
juce::String redactProtocolText (const juce::String&);
juce::var redactProtocolValue (juce::var);
InvocationResult redactInvocationResult (InvocationResult);
juce::String makeOccurredAt();
juce::String makeSequenceFilename (juce::int64);
} // namespace inlay::emulator
