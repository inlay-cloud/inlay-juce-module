#include "AuthCallbackListener.h"

namespace inlay::internal {
    namespace {
        constexpr int pollMs = 250;
        constexpr int minPort = 49152;
        constexpr int maxPort = 65535;
        constexpr const char *callbackPath = "/auth/complete";

        juce::String decodeQueryValue(juce::String value) {
            value = value.replace("+", " ");
            return juce::URL::removeEscapeChars(value);
        }

        juce::String getRequestTarget(const juce::String &request) {
            const auto firstLine = request.upToFirstOccurrenceOf("\r\n", false, false)
                                   .upToFirstOccurrenceOf("\n", false, false);

            if (!firstLine.startsWith("GET "))
                return {};

            const auto targetStart = 4;
            const auto targetEnd = firstLine.indexOfChar(targetStart, ' ');
            if (targetEnd <= targetStart)
                return {};

            return firstLine.substring(targetStart, targetEnd);
        }

        juce::String getQueryParam(const juce::String &requestTarget, const juce::String &paramName) {
            const auto queryIndex = requestTarget.indexOfChar('?');
            if (queryIndex < 0 || queryIndex + 1 >= requestTarget.length())
                return {};

            const auto query = requestTarget.substring(queryIndex + 1);
            juce::StringArray pairs;
            pairs.addTokens(query, "&", {});

            for (const auto &pair : pairs) {
                const auto separatorIndex = pair.indexOfChar('=');
                const auto key = decodeQueryValue(separatorIndex >= 0 ? pair.substring(0, separatorIndex) : pair);

                if (key != paramName)
                    continue;

                const auto value = separatorIndex >= 0 ? pair.substring(separatorIndex + 1) : juce::String{};
                return decodeQueryValue(value);
            }

            return {};
        }
    }

    void AuthCallbackListener::close() {

    }

    AuthCallbackListener::AuthCallbackListener(const juce::String &allowedRedirectUrlPrefix):
        _allowedRedirectUrlPrefix(allowedRedirectUrlPrefix) {
    }

    juce::Result AuthCallbackListener::createListenerOnRandomPort() {
        _listenerPort = createListenerPort();
        if (_listenerPort == 0)
            return juce::Result::fail("Failed to start local auth listener on localhost.");

        return juce::Result::ok();
    }

    juce::String AuthCallbackListener::getEndpointURL() const {
        return "http://127.0.0.1:" + juce::String(_listenerPort) + callbackPath;
    }

    bool AuthCallbackListener::waitForRequest(const std::atomic_bool &shouldStop) {
        juce::Logger::writeToLog("Listening for auth completion on http://localhost:"
                                 + juce::String(_listenerPort)
                                 + callbackPath + ".");

        while (!shouldStop.load()) {
            const auto ready = _socket.waitUntilReady(true, pollMs);
            if (ready <= 0)
                continue;

            juce::Logger::writeToLog("socker is ready");

            std::unique_ptr<juce::StreamingSocket> client(_socket.waitForNextConnection());
            if (client == nullptr)
                continue;

            juce::Logger::writeToLog("new connection");

            const auto request = readHttpRequest(*client);
            if (request.startsWith("GET " + juce::String(callbackPath))) {
                juce::Logger::writeToLog("Received auth completion request on localhost.");

                const auto requestTarget = getRequestTarget(request);
                const auto redirectURL = getQueryParam(requestTarget, "redirectURL");
                const auto isOk = getQueryParam(requestTarget, "ok").equalsIgnoreCase("true");

                if (redirectURL.isNotEmpty()) {
                    if (!redirectURL.startsWith(_allowedRedirectUrlPrefix)) {
                        writeHttpResponse(*client,
                                      "HTTP/1.1 400 Bad Request",
                                      "Invalid redirect URL");
                        return false;
                    }

                    const auto body = juce::String("Redirecting...");
                    const auto response = "HTTP/1.1 302 Found\r\n"
                                          "Location: " + redirectURL + "\r\n"
                                          "Content-Type: text/plain; charset=utf-8\r\n"
                                          "Connection: close\r\n"
                                          "Content-Length: " + juce::String(body.getNumBytesAsUTF8()) + "\r\n\r\n"
                                          + body;

                    juce::ignoreUnused(client->write(response.toRawUTF8(),
                                                     static_cast<int>(response.getNumBytesAsUTF8())));
                } else {
                    writeHttpResponse(*client,
                                      "HTTP/1.1 200 OK",
                                      "Authentication complete. You can close this window.");
                }

                return isOk;
            }

            writeHttpResponse(*client, "HTTP/1.1 404 Not Found", "Not found.");
        }

        return false;
    }

    juce::String AuthCallbackListener::readHttpRequest(juce::StreamingSocket &socket) const {
        juce::MemoryOutputStream request;
        char buffer[1024];

        for (;;) {
            const auto ready = socket.waitUntilReady(true, 1000);
            if (ready <= 0)
                break;

            const auto bytesRead = socket.read(buffer, static_cast<int>(sizeof (buffer)), false);
            if (bytesRead <= 0)
                break;

            request.write(buffer, static_cast<size_t>(bytesRead));

            const auto requestText = request.toString();
            if (requestText.contains("\r\n\r\n") || requestText.contains("\n\n"))
                break;
        }

        return request.toString();
    }

    void AuthCallbackListener::writeHttpResponse(juce::StreamingSocket &socket,
                                                 const juce::String &statusLine,
                                                 const juce::String &body) const {
        const auto contentLength = body.getNumBytesAsUTF8();
        const auto response = statusLine + "\r\n"
                              "Content-Type: text/plain; charset=utf-8\r\n"
                              "Connection: close\r\n"
                              "Content-Length: " + juce::String(contentLength) + "\r\n\r\n"
                              + body;

        juce::ignoreUnused(socket.write(response.toRawUTF8(), static_cast<int>(response.getNumBytesAsUTF8())));
    }

    int AuthCallbackListener::createListenerPort() {
        auto &random = juce::Random::getSystemRandom();

        for (int attempt = 0; attempt < 128; ++attempt) {
            const auto port = random.nextInt({minPort, maxPort + 1});
            if (_socket.createListener(port, "127.0.0.1"))
                return port;
        }

        return 0;
    }
} // namespace inlay::internal
