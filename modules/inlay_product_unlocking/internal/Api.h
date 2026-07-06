#pragma once

#include <optional>
#include <variant>

#include <juce_core/juce_core.h>

namespace inlay::internal {

class UnlockerTests;

class Api
{
public:
    template <typename OkPayload>
    struct Result
    {
        int statusCode = 0;
        std::optional<OkPayload> okPayload;
        juce::String nokPayload;

        juce::String toErrMsg() const
        {
            auto message = "Status: " + juce::String (statusCode);
            if (nokPayload.isNotEmpty())
                message += ", Body: " + nokPayload;

            return message;
        }
    };

    struct MetaData
    {
        juce::String moduleVersion;
        juce::String deviceId;
        juce::String os;
        juce::String systemStats;
        juce::String productId;
        juce::String productVersion;
        juce::String productName;
        bool isPlugin = false;
        juce::String inlayDir;
        juce::String instanceId;
    };

    struct StartAuthRequest
    {
        juce::String productId;
        juce::String deviceId;
    };

    struct StartAuthResponse
    {
        juce::String activationToken;
    };

    struct CompleteAuthRequest
    {
        juce::String activationToken;
        MetaData metaData;
    };

    struct AccessRequest
    {
        juce::String idToken;
        MetaData metaData;
    };

    struct AppUpdate
    {
        juce::String version;
        juce::String url;
    };

    struct AuthResponse
    {
        juce::String accessToken;
        juce::String idToken;
        std::optional<AppUpdate> appUpdate;
    };

    struct SendLogsRequest
    {
        juce::StringArray logs;
    };

    Api (juce::String baseURLToUse,
         juce::String productIdToUse,
         juce::String moduleVersionToUse,
         juce::String instanceIdToUse);

    juce::URL makeContinueAuthURL (const juce::String& activationToken, const juce::String& redirectURL) const;
    Result<StartAuthResponse> startAuth (const StartAuthRequest& request) const;
    Result<AuthResponse> completeAuth (const CompleteAuthRequest& request) const;
    Result<AuthResponse> requestAccess (const AccessRequest& request) const;
    Result<std::monostate> sendLogs (const SendLogsRequest& request) const;

private:
    friend class UnlockerTests;

    static std::optional<AuthResponse> parseAuthResponse (const juce::var& responseBody);

    juce::String _baseURL;
    juce::String _productId;
    juce::String _moduleVersion;
    juce::String _instanceId;
};

} // namespace inlay::internal
