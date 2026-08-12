#pragma once

#include <juce_core/juce_core.h>

namespace inlay::internal {
    class Browser {
    public:
        virtual ~Browser() = default;

        virtual void openURL(juce::URL url) = 0;
    };
} // namespace inlay::internal
