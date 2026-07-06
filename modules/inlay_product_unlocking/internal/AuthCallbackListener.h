#pragma once

#include <atomic>

#include <juce_core/juce_core.h>

namespace inlay::internal {
    class AuthCallbackListener {
    public:
        AuthCallbackListener() = delete;

        explicit AuthCallbackListener(const juce::String &allowedRedirectUrlPrefix);

        ~AuthCallbackListener() = default;

        juce::Result createListenerOnRandomPort();

        juce::String getEndpointURL() const;

        void close();

        bool waitForRequest(const std::atomic_bool &shouldStop);

    private:
        juce::String readHttpRequest(juce::StreamingSocket &socket) const;

        void writeHttpResponse(juce::StreamingSocket &socket,
                               const juce::String &statusLine,
                               const juce::String &body) const;

        int createListenerPort();

    private:
        juce::StreamingSocket _socket;
        int _listenerPort = 0;
        const juce::String _allowedRedirectUrlPrefix;
    };
} // namespace inlay::internal
