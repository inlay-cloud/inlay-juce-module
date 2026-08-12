#include "Emulator.h"

#include <JuceHeader.h>

#include <csignal>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

namespace
{
#if ! JUCE_WINDOWS
volatile std::sig_atomic_t terminationRequested = 0;

void requestTermination (int)
{
    terminationRequested = 1;
}
#endif

juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

std::optional<juce::String> readEnvelopeRunId (const juce::File& runDirectory)
{
    juce::var config;
    if (juce::JSON::parse (runDirectory.getChildFile ("config.json").loadFileAsString(), config).failed())
        return std::nullopt;

    const auto* root = config.getDynamicObject();
    if (root == nullptr)
        return std::nullopt;

    const auto version = root->getProperty ("protocolVersion");
    const auto runId = root->getProperty ("runId");
    if ((! version.isInt() && ! version.isInt64())
        || static_cast<juce::int64> (version) != inlay::emulator::protocolVersion
        || ! runId.isString() || runId.toString().trim().isEmpty())
        return std::nullopt;

    return runId.toString().trim();
}

class InlayJuceEmulatorApplication final : public juce::JUCEApplication,
                                           private juce::Timer
{
public:
    const juce::String getApplicationName() override
    {
        return ProjectInfo::projectName;
    }

    const juce::String getApplicationVersion() override
    {
        return ProjectInfo::versionString;
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void initialise (const juce::String&) override
    {
       #if ! JUCE_WINDOWS
        terminationRequested = 0;
        std::signal (SIGINT, requestTermination);
        std::signal (SIGTERM, requestTermination);
       #endif

        const auto arguments = getCommandLineParameterArray();
        if (arguments.size() != 2 || arguments[0] != "--run-dir"
            || ! juce::File::isAbsolutePath (arguments[1]))
        {
            std::cerr << "Usage: inlay-juce-emulator --run-dir <absolute-path>" << std::endl;
            setApplicationReturnValue (2);
            quit();
            return;
        }

        runDirectory = juce::File (arguments[1]);
        protocolRunId = readEnvelopeRunId (runDirectory);

        inlay::emulator::EmulatorConfig config;
        if (const auto result = inlay::emulator::loadConfig (runDirectory, config); result.failed())
        {
            handleFatal ("invalid_configuration", "emulator configuration is invalid");
            return;
        }

        protocolRunId = config.runId;
        try
        {
            emulator = std::make_unique<inlay::emulator::Emulator> (
                runDirectory, std::move (config), [this] (juce::String message) {
                    juce::ignoreUnused (message);
                    handleFatal ("runtime_failure", "emulator runtime failure");
                });
        }
        catch (...)
        {
            handleFatal ("startup_failed", "emulator startup failed");
            return;
        }

        if (const auto result = emulator->start(); result.failed())
            handleFatal ("startup_failed", "emulator startup failed");
       #if ! JUCE_WINDOWS
        else
            startTimer (20);
       #endif
    }

    void shutdown() override
    {
        stopTimer();
        if (emulator != nullptr)
        {
            emulator->stop ("process_finished");
            emulator.reset();
        }

       #if ! JUCE_WINDOWS
        std::signal (SIGINT, SIG_DFL);
        std::signal (SIGTERM, SIG_DFL);
        terminationRequested = 0;
       #endif
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
    }

private:
    void timerCallback() override
    {
       #if ! JUCE_WINDOWS
        if (terminationRequested != 0)
        {
            terminationRequested = 0;
            stopTimer();
            quit();
        }
       #endif
    }

    void handleFatal (const juce::String& code, const juce::String& safeMessage)
    {
        if (fatalHandled)
            return;

        fatalHandled = true;
        bool emitted = false;
        if (protocolRunId.has_value())
        {
            auto payload = object();
            auto* value = payload.getDynamicObject();
            value->setProperty ("code", code);
            value->setProperty ("message", safeMessage);
            inlay::emulator::EventWriter writer (runDirectory.getChildFile ("events"), *protocolRunId);
            emitted = writer.emit ("fatal", payload).wasOk();
        }

        std::cerr << "Inlay JUCE Emulator: " << safeMessage;
        if (protocolRunId.has_value() && ! emitted)
            std::cerr << " (fatal event could not be written)";
        std::cerr << std::endl;

        setApplicationReturnValue (1);
        quit();
    }

    juce::File runDirectory;
    std::optional<juce::String> protocolRunId;
    std::unique_ptr<inlay::emulator::Emulator> emulator;
    bool fatalHandled = false;
};
} // namespace

START_JUCE_APPLICATION (InlayJuceEmulatorApplication)
