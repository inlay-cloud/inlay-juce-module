#include "Api.h"

#include <type_traits>

namespace inlay::internal {

namespace
{
juce::String makeApiHeaders (const juce::String& productId,
                             const juce::String& moduleVersion,
                             const juce::String& instanceId)
{
    return "Content-Type: application/json\r\n"
           "X-Inlay-Product-Id: " + productId + "\r\n"
           "X-Inlay-Module-Version: " + moduleVersion + "\r\n"
           "X-Inlay-Instance-Id: " + instanceId + "\r\n";
}

juce::String makeApiURL (const juce::String& baseURL, const juce::String& path)
{
    return baseURL.trimEnd().trimCharactersAtEnd ("/") + "/" + path;
}

juce::var makeMetaDataVar (const Api::MetaData& metaData)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("moduleVersion", metaData.moduleVersion);
    object->setProperty ("deviceId", metaData.deviceId);
    object->setProperty ("os", metaData.os);
    object->setProperty ("systemStats", metaData.systemStats);
    object->setProperty ("productId", metaData.productId);
    object->setProperty ("productVersion", metaData.productVersion);
    object->setProperty ("productName", metaData.productName);
    object->setProperty ("isPlugin", metaData.isPlugin);
    object->setProperty ("inlayDir", metaData.inlayDir);
    object->setProperty ("instanceID", metaData.instanceId);
    return juce::var (object.get());
}

juce::var makeStartAuthRequestVar (const Api::StartAuthRequest& request)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("productId", request.productId);
    object->setProperty ("deviceId", request.deviceId);
    return juce::var (object.get());
}

juce::var makeCompleteAuthRequestVar (const Api::CompleteAuthRequest& request)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("activationToken", request.activationToken);

    auto metaData = makeMetaDataVar (request.metaData);
    if (auto* metaDataObject = metaData.getDynamicObject(); metaDataObject != nullptr)
    {
        for (const auto& property : metaDataObject->getProperties())
            object->setProperty (property.name, property.value);
    }

    return juce::var (object.get());
}

juce::var makeAccessRequestVar (const Api::AccessRequest& request)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("idToken", request.idToken);

    auto metaData = makeMetaDataVar (request.metaData);
    if (auto* metaDataObject = metaData.getDynamicObject(); metaDataObject != nullptr)
    {
        for (const auto& property : metaDataObject->getProperties())
            object->setProperty (property.name, property.value);
    }

    return juce::var (object.get());
}

juce::var makeSendLogsRequestVar (const Api::SendLogsRequest& request)
{
    juce::Array<juce::var> logs;

    for (const auto& logLine : request.logs)
        logs.add (logLine);

    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("logs", juce::var (logs));
    return juce::var (object.get());
}

std::optional<Api::StartAuthResponse> parseStartAuthResponse (const juce::var& responseBody)
{
    auto* object = responseBody.getDynamicObject();
    if (object == nullptr)
        return std::nullopt;

    auto activationToken = object->getProperty ("activationToken").toString();
    if (activationToken.isEmpty())
        return std::nullopt;

    return Api::StartAuthResponse { activationToken };
}

template <typename OkPayload, typename ParseOkPayload>
Api::Result<OkPayload> postJSON (const juce::String& baseURL,
                                 const juce::String& productId,
                                 const juce::String& moduleVersion,
                                 const juce::String& instanceId,
                                 const juce::String& path,
                                 const juce::var& body,
                                 ParseOkPayload&& parseOkPayload)
{
    Api::Result<OkPayload> result;

    const auto requestBody = juce::JSON::toString (body);
    const auto requestURL = juce::URL (makeApiURL (baseURL, path)).withPOSTData (requestBody);

    const auto extraHeaders = makeApiHeaders (productId, moduleVersion, instanceId);

    juce::Logger::writeToLog("postJSON: " + path);

    if (auto stream = requestURL.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                        .withConnectionTimeoutMs (5000)
                                                        .withExtraHeaders (extraHeaders)
                                                        .withStatusCode (&result.statusCode)
                                                        .withHttpRequestCmd ("POST")))
    {
        if (result.statusCode >= 200 && result.statusCode < 300)
        {
            const auto responseText = stream->readEntireStreamAsString();
            if constexpr (std::is_same_v<OkPayload, std::monostate>)
            {
                result.okPayload = std::monostate {};
            }
            else
            {
                const auto responseBody = juce::JSON::parse (responseText);
                result.okPayload = parseOkPayload (responseBody);
            }
        }
        else
        {
            char buffer[200];
            const auto bytesRead = stream->read (buffer, static_cast<int> (sizeof (buffer)));
            result.nokPayload = juce::String::fromUTF8 (buffer, bytesRead);
        }
    }

    return result;
}
} // namespace

Api::Api (juce::String baseURLToUse,
          juce::String productIdToUse,
          juce::String moduleVersionToUse,
          juce::String instanceIdToUse)
    : _baseURL (baseURLToUse),
      _productId (productIdToUse),
      _moduleVersion (moduleVersionToUse),
      _instanceId (instanceIdToUse)
{
}

juce::URL Api::makeContinueAuthURL (const juce::String& activationToken, const juce::String& redirectURL) const
{
    return juce::URL (makeApiURL (_baseURL, "appweb/auth/continue"))
        .withParameter ("activationToken", activationToken)
        .withParameter ("redirectURL", redirectURL);
}

Api::Result<Api::StartAuthResponse> Api::startAuth (const StartAuthRequest& request) const
{
    return postJSON<StartAuthResponse> (_baseURL,
                                        _productId,
                                        _moduleVersion,
                                        _instanceId,
                                        "app/auth/start",
                                        makeStartAuthRequestVar (request),
                                        parseStartAuthResponse);
}

Api::Result<Api::AuthResponse> Api::completeAuth (const CompleteAuthRequest& request) const
{
    return postJSON<AuthResponse> (_baseURL,
                                   _productId,
                                   _moduleVersion,
                                   _instanceId,
                                   "app/auth/complete",
                                   makeCompleteAuthRequestVar (request),
                                   Api::parseAuthResponse);
}

Api::Result<Api::AuthResponse> Api::requestAccess (const AccessRequest& request) const
{
    return postJSON<AuthResponse> (_baseURL,
                                   _productId,
                                   _moduleVersion,
                                   _instanceId,
                                   "app/auth/access",
                                   makeAccessRequestVar (request),
                                   Api::parseAuthResponse);
}

Api::Result<std::monostate> Api::sendLogs (const SendLogsRequest& request) const
{
    return postJSON<std::monostate> (_baseURL,
                                     _productId,
                                     _moduleVersion,
                                     _instanceId,
                                     "logs",
                                     makeSendLogsRequestVar (request),
                                     [] (const juce::var&) -> std::optional<std::monostate>
                                     {
                                         return std::monostate {};
                                     });
}

std::optional<Api::AuthResponse> Api::parseAuthResponse (const juce::var& responseBody)
{
    auto* object = responseBody.getDynamicObject();
    if (object == nullptr)
        return std::nullopt;

    auto accessToken = object->getProperty ("accessToken").toString();
    if (accessToken.isEmpty())
        return std::nullopt;

    Api::AuthResponse response;
    response.accessToken = accessToken;
    response.idToken = object->getProperty ("idToken").toString();

    auto* appUpdateObject = object->getProperty ("appUpdate").getDynamicObject();
    if (appUpdateObject != nullptr)
    {
        const auto version = appUpdateObject->getProperty ("version").toString();
        const auto url = appUpdateObject->getProperty ("url").toString();
        if (version.isNotEmpty() && url.isNotEmpty())
            response.appUpdate = Api::AppUpdate { version, url };
    }

    return response;
}

} // namespace inlay::internal
