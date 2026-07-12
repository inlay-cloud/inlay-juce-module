#include "TokenValidator.h"

#include "SignatureVerification.h"

namespace inlay::internal {
    namespace {
        std::optional<juce::MemoryBlock> decodeBase64(const juce::String &text) {
            if (text.isEmpty())
                return std::nullopt;

            juce::MemoryOutputStream output;

            if (!juce::Base64::convertFromBase64(output, text.trim()))
                return std::nullopt;

            return output.getMemoryBlock();
        }

        std::optional<juce::String> parseAndVerifyAccessTokenPayload(const juce::String &token,
                                                                     const juce::String &publicKey) {
            const auto separatorIndex = token.indexOfChar('.');
            if (separatorIndex <= 0 || separatorIndex != token.lastIndexOfChar('.'))
                return std::nullopt;

            const auto payloadBase64 = token.substring(0, separatorIndex).trim();
            const auto signatureBase64 = token.substring(separatorIndex + 1).trim();

            const auto payload = decodeBase64(payloadBase64);
            const auto signature = decodeBase64(signatureBase64);
            if (!payload.has_value() || !signature.has_value())
                return std::nullopt;

            if (!verifyRSASHA256Signature(*payload, *signature, publicKey))
                return std::nullopt;

            const auto *utf8 = static_cast<const char *>(payload->getData());
            if (!juce::CharPointer_UTF8::isValidString(utf8, static_cast<int>(payload->getSize())))
                return std::nullopt;

            return juce::String::fromUTF8(utf8, static_cast<int>(payload->getSize()));
        }

        std::optional<juce::String> readTokenStringProperty(const juce::DynamicObject &object,
                                                            const juce::Identifier &propertyName) {
            if (!object.hasProperty(propertyName))
                return std::nullopt;

            const auto propertyValue = object.getProperty(propertyName).toString();
            if (propertyValue.isEmpty())
                return std::nullopt;

            return propertyValue;
        }

        std::optional<juce::int64> readTokenInt64Property(const juce::DynamicObject &object,
                                                          const juce::Identifier &propertyName) {
            if (!object.hasProperty(propertyName))
                return std::nullopt;

            const auto propertyValue = object.getProperty(propertyName).toString();
            if (propertyValue.isEmpty())
                return std::nullopt;

            return propertyValue.getLargeIntValue();
        }

        std::optional<juce::int64> readRefreshIntervalDays(const juce::DynamicObject &object) {
            static const juce::Identifier propertyName{"r"};
            if (!object.hasProperty(propertyName))
                return std::nullopt;

            const auto value = object.getProperty(propertyName);
            if (!value.isInt() && !value.isInt64())
                return std::nullopt;

            const auto days = static_cast<juce::int64>(value);
            if (days < -1)
                return std::nullopt;

            return days;
        }
    } // namespace

    TokenValidator::TokenValidator(const juce::String &productIdToUse,
                                   const juce::String &publicKeyToUse,
                                   const juce::String &deviceIdToUse)
        : _productId(productIdToUse),
          _publicKey(publicKeyToUse),
          _deviceId(deviceIdToUse) {
    }

    std::optional<AccessToken> TokenValidator::validateAccessToken(const juce::String &token) const {
        const auto claimsJson = parseAndVerifyAccessTokenPayload(token, _publicKey);
        if (!claimsJson.has_value())
            return std::nullopt;

        const auto accessTokenCandidate = makeAccessTokenFromJSON(*claimsJson);
        if (!accessTokenCandidate.has_value())
            return std::nullopt;

        if (!validateAccessTokenClaims(*accessTokenCandidate))
            return std::nullopt;

        return accessTokenCandidate;
    }

    std::optional<AccessToken> TokenValidator::makeAccessTokenFromJSON(const juce::String &claimsJson) const {
        const auto claimsVar = juce::JSON::parse(claimsJson);
        const auto *claimsObj = claimsVar.getDynamicObject();
        if (claimsObj == nullptr)
            return std::nullopt;

        const auto productIdSuff = readTokenStringProperty(*claimsObj, "p");
        const auto deviceId = readTokenStringProperty(*claimsObj, "d");
        const auto issuedAtSeconds = readTokenInt64Property(*claimsObj, "i");
        const auto expiresAtSeconds = readTokenInt64Property(*claimsObj, "e");
        const auto userEmail = readTokenStringProperty(*claimsObj, "u");
        const auto refreshIntervalDays = readRefreshIntervalDays(*claimsObj);

        if (!productIdSuff.has_value() || !deviceId.has_value()
            || !issuedAtSeconds.has_value() || !expiresAtSeconds.has_value())
            return std::nullopt;

        AccessToken token;
        token.productId = *productIdSuff;
        token.deviceId = *deviceId;
        token.userEmail = userEmail.value_or(juce::String{});
        token.issuedAt = *issuedAtSeconds * 1000;
        token.expiresAt = *expiresAtSeconds * 1000;
        token.refreshIntervalDays = refreshIntervalDays.value_or(1);
        return token;
    }

    bool TokenValidator::validateAccessTokenClaims(const AccessToken &token) const {
        const auto now = juce::Time::currentTimeMillis();

        const auto productCheck = token.productId == _productId;
        if (!productCheck) return false;

        const auto deviceCheck = token.deviceId == _deviceId;
        if (!deviceCheck) return false;

        const auto expirationCheck = token.expiresAt > now;
        if (!expirationCheck) return false;

        const auto issuedCheck = token.issuedAt > 0 && token.issuedAt < now;
        if (!issuedCheck) return false;

        const auto userEmailCheck = token.userEmail != "";
        if (!userEmailCheck) return false;

        return true;
    }
} // namespace inlay::internal

#if JUCE_UNIT_TESTS
#include "Tests.cpp"
#endif
