#pragma once

#include <optional>

#include <juce_core/juce_core.h>

namespace inlay::internal {
    class UnlockerTests;

    struct AccessToken {
        juce::String productId;
        juce::String deviceId;
        juce::String userEmail;
        juce::int64 issuedAt = 0;
        juce::int64 expiresAt = 0;

        juce::int64 getAgeMs() const { return juce::Time::currentTimeMillis() - issuedAt; }

        bool olderThanDay() const { return getAgeMs() > 24 * 3600 * 1000; }
    };

    class TokenValidator {
    public:
        TokenValidator(const juce::String &productIdToUse,
                       const juce::String &publicKeyToUse,
                       const juce::String &deviceIdToUse);

        std::optional<AccessToken> validateAccessToken(const juce::String &token) const;

    private:
        friend class UnlockerTests;

        std::optional<AccessToken> makeAccessTokenFromJSON(const juce::String &claimsJson) const;

        bool validateAccessTokenClaims(const AccessToken &token) const;

        const juce::String _productId;
        const juce::String _publicKey;
        const juce::String _deviceId;
    };
} // namespace inlay::internal
