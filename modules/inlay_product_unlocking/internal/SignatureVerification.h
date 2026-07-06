#pragma once

#include <juce_core/juce_core.h>

namespace inlay::internal {
bool verifyRSASHA256Signature (const juce::MemoryBlock& payload,
                               const juce::MemoryBlock& signature,
                               const juce::String& publicKey);
} // namespace inlay::internal
