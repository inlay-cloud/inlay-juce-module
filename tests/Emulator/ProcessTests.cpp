#include "EventWriter.h"

#include <algorithm>
#include <csignal>
#include <functional>
#include <string>
#include <thread>

#if ! JUCE_WINDOWS
 #include <sys/types.h>
 #include <unistd.h>
#endif

namespace inlay::emulator
{
namespace
{
constexpr auto processRunId = "process-run-001";
constexpr auto processPublicKey = "process-public-key-secret";
constexpr auto processAccessTokenSecret = "access-token-secret-fixture";
constexpr auto processIdTokenSecret = "id-token-secret-fixture";
constexpr auto processAuthorizationSecret = "authorization-secret-fixture";

juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

juce::File createRunDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("inlay-emulator-process-" + juce::Uuid().toString());
    directory.createDirectory();
    return directory;
}

bool writeConfig (const juce::File& runDirectory)
{
    const auto storage = runDirectory.getChildFile ("module-state");
    auto unlocker = object();
    auto* unlockerObject = unlocker.getDynamicObject();
    unlockerObject->setProperty ("apiUrl", "http://127.0.0.1:1");
    unlockerObject->setProperty ("productId", "process-product");
    unlockerObject->setProperty ("publicKey", processPublicKey);
    unlockerObject->setProperty ("storageDir", storage.getFullPathName());
    unlockerObject->setProperty ("accessTokenFixture", processAccessTokenSecret);
    unlockerObject->setProperty ("idTokenFixture", processIdTokenSecret);
    unlockerObject->setProperty ("authorizationFixture", processAuthorizationSecret);

    auto config = object();
    auto* configObject = config.getDynamicObject();
    configObject->setProperty ("protocolVersion", protocolVersion);
    configObject->setProperty ("runId", processRunId);
    configObject->setProperty ("module", "juce");
    configObject->setProperty ("unlocker", unlocker);
    return writeJsonAtomically (runDirectory.getChildFile ("config.json"), config).wasOk();
}

bool invalidateConfigModule (const juce::File& runDirectory)
{
    juce::var config;
    const auto file = runDirectory.getChildFile ("config.json");
    if (juce::JSON::parse (file.loadFileAsString(), config).failed())
        return false;
    auto* root = config.getDynamicObject();
    if (root == nullptr)
        return false;
    root->setProperty ("module", "not-juce");
    return writeJsonAtomically (file, config).wasOk();
}

juce::Array<juce::File> eventFiles (const juce::File& runDirectory)
{
    juce::Array<juce::File> files;
    runDirectory.getChildFile ("events").findChildFiles (files, juce::File::findFiles, false, "*.json");
    std::sort (files.begin(), files.end(), [] (const auto& left, const auto& right) {
        return left.getFileName() < right.getFileName();
    });
    return files;
}

juce::Array<juce::var> readEvents (const juce::File& runDirectory)
{
    juce::Array<juce::var> events;
    for (const auto& file : eventFiles (runDirectory))
    {
        juce::var event;
        if (juce::JSON::parse (file.loadFileAsString(), event).wasOk())
            events.add (event);
    }
    return events;
}

const juce::DynamicObject* findEvent (const juce::Array<juce::var>& events,
                                      const juce::String& type,
                                      const juce::String& commandId = {})
{
    for (const auto& event : events)
        if (const auto* envelope = event.getDynamicObject(); envelope != nullptr
            && envelope->getProperty ("type").toString() == type
            && (commandId.isEmpty() || envelope->getProperty ("commandId").toString() == commandId))
            return envelope;
    return nullptr;
}

bool waitUntil (std::function<bool()> predicate, int timeoutMs = 5000)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (predicate())
            return true;
        juce::Thread::sleep (20);
    }
    return predicate();
}

bool writeCommand (const juce::File& runDirectory, juce::int64 sequence,
                   const juce::String& commandId, const juce::String& method,
                   const juce::var& arguments)
{
    auto command = object();
    auto* envelope = command.getDynamicObject();
    envelope->setProperty ("protocolVersion", protocolVersion);
    envelope->setProperty ("runId", processRunId);
    envelope->setProperty ("commandId", commandId);
    envelope->setProperty ("sequence", sequence);
    envelope->setProperty ("target", "unlocker");
    envelope->setProperty ("method", method);
    envelope->setProperty ("arguments", arguments);
    envelope->setProperty ("issuedAt", "2026-08-07T00:00:00Z");
    return writeJsonAtomically (
               runDirectory.getChildFile ("commands").getChildFile (makeSequenceFilename (sequence)), command)
        .wasOk();
}

bool hasTemporaryFiles (const juce::File& runDirectory)
{
    juce::Array<juce::File> files;
    runDirectory.findChildFiles (files, juce::File::findFiles, true, "*");
    for (const auto& file : files)
        if (file.getFileName().contains ("_temp") || file.hasFileExtension ("tmp"))
            return true;
    return false;
}

class ProcessOutputCollector final
{
public:
    explicit ProcessOutputCollector (juce::ChildProcess& child)
        : process (child), reader ([this] { readUntilClosed(); })
    {
    }

    ~ProcessOutputCollector()
    {
        wait();
    }

    void wait()
    {
        if (reader.joinable())
            reader.join();
    }

    juce::String getOutput() const
    {
        return juce::String::fromUTF8 (bytes.data(), static_cast<int> (bytes.size()));
    }

private:
    void readUntilClosed()
    {
        for (;;)
        {
            char buffer[4096];
            const auto count = process.readProcessOutput (buffer, static_cast<int> (sizeof (buffer)));
            if (count <= 0)
                return;
            bytes.append (buffer, static_cast<size_t> (count));
        }
    }

    juce::ChildProcess& process;
    std::string bytes;
    std::thread reader;
};
} // namespace

class ProcessTests final : public juce::UnitTest
{
public:
    ProcessTests() : juce::UnitTest ("Headless Emulator Process", "inlay_product_unlocking") {}

    void runTest() override
    {
        beginTest ("runs the real unlocker through protocol v1 without browser or temporary-file side effects");

        const auto runDirectory = createRunDirectory();
        bool passed = true;
        const auto check = [&] (bool condition, const juce::String& message) {
            expect (condition, message);
            passed = passed && condition;
            return condition;
        };

        if (! check (writeConfig (runDirectory), "could not create process-test configuration"))
        {
            logMessage ("Retained failing emulator run directory: " + runDirectory.getFullPathName());
            return;
        }

        juce::ChildProcess process;
        const juce::StringArray argv { INLAY_EMULATOR_PATH, "--run-dir", runDirectory.getFullPathName() };
        if (! check (process.start (argv), "could not launch the headless emulator"))
        {
            logMessage ("Retained failing emulator run directory: " + runDirectory.getFullPathName());
            return;
        }
        ProcessOutputCollector outputCollector (process);

        const auto startupObserved = waitUntil ([&] {
            const auto events = readEvents (runDirectory);
            const auto* ready = findEvent (events, "ready");
            const auto* changed = findEvent (events, "unlocker_changed");
            if (ready == nullptr || changed == nullptr)
                return false;
            const auto* snapshot = changed->getProperty ("payload").getDynamicObject();
            return snapshot != nullptr && snapshot->getProperty ("status").toString() == "activation_required";
        });
        check (startupObserved, "ready and automatic activation-required startup were not observed");

        auto events = readEvents (runDirectory);
        const auto* ready = findEvent (events, "ready");
        juce::int64 processId = 0;
        check (ready != nullptr, "ready event is missing");
        if (ready != nullptr)
        {
            const auto* payload = ready->getProperty ("payload").getDynamicObject();
            check (payload != nullptr, "ready payload is not an object");
            if (payload != nullptr)
            {
                check (payload->getProperty ("module").toString() == "juce", "ready module is incorrect");
                check (payload->getProperty ("moduleVersion").toString() == "JUCE-1.0.3",
                       "ready module version is incorrect");
                check (static_cast<juce::int64> (payload->getProperty ("protocolVersion")) == 1,
                       "ready protocol version is incorrect");
                processId = static_cast<juce::int64> (payload->getProperty ("pid"));
                check (processId > 0, "ready process ID is invalid");

                const juce::StringArray expectedMethods {
                    "getStatus", "isLocked", "getError", "getCurrentUser", "getAppUpdate", "startup",
                    "startActivation", "retryUnlocking", "logout", "skipCurrentAppUpdateVersion", "openWebsite"
                };
                const auto* methods = payload->getProperty ("supportedPublicMethods").getArray();
                check (methods != nullptr && methods->size() == expectedMethods.size(),
                       "ready supported method count is incorrect");
                if (methods != nullptr && methods->size() == expectedMethods.size())
                    for (int index = 0; index < expectedMethods.size(); ++index)
                        check ((*methods)[index].toString() == expectedMethods[index],
                               "ready supported method differs at index " + juce::String (index));
            }
        }

        check (writeCommand (runDirectory, 1, "status-command", "getStatus", object()),
               "could not write getStatus command");
        const auto statusCompleted = waitUntil ([&] {
            return findEvent (readEvents (runDirectory), "command_completed", "status-command") != nullptr;
        });
        check (statusCompleted, "getStatus did not complete");

        events = readEvents (runDirectory);
        if (const auto* completion = findEvent (events, "command_completed", "status-command"))
        {
            const auto* payload = completion->getProperty ("payload").getDynamicObject();
            check (payload != nullptr && payload->getProperty ("outcome").toString() == "completed",
                   "getStatus outcome is not completed");
            const auto* output = payload != nullptr ? payload->getProperty ("output").getDynamicObject() : nullptr;
            check (output != nullptr && output->getProperty ("status").toString() == "activation_required",
                   "getStatus output is incorrect");
        }

        constexpr auto website = "https://example.test/exact?a=1&b=two";
        auto websiteArguments = object();
        websiteArguments.getDynamicObject()->setProperty ("url", website);
        check (writeCommand (runDirectory, 2, "website-command", "openWebsite", websiteArguments),
               "could not write openWebsite command");
        const auto websiteObserved = waitUntil ([&] {
            const auto current = readEvents (runDirectory);
            return findEvent (current, "browser_open_requested") != nullptr
                && findEvent (current, "command_completed", "website-command") != nullptr;
        });
        check (websiteObserved, "openWebsite browser request and completion were not observed");

        events = readEvents (runDirectory);
        if (const auto* browser = findEvent (events, "browser_open_requested"))
        {
            const auto* payload = browser->getProperty ("payload").getDynamicObject();
            check (payload != nullptr && payload->getProperty ("requestId").toString() == "browser-001",
                   "browser request ID is incorrect");
            check (payload != nullptr && payload->getProperty ("url").toString() == website,
                   "browser request URL is incorrect");
            check (browser->getProperty ("commandId").toString() == "website-command",
                   "openWebsite browser event is missing its command ID");
        }

        if (const auto* completion = findEvent (events, "command_completed", "website-command"))
        {
            const auto* payload = completion->getProperty ("payload").getDynamicObject();
            check (payload != nullptr && payload->getProperty ("outcome").toString() == "completed",
                   "openWebsite outcome is not completed");
            const auto* output = payload != nullptr ? payload->getProperty ("output").getDynamicObject() : nullptr;
            check (output != nullptr && output->getProperties().isEmpty(), "openWebsite output is not empty");
        }

       #if ! JUCE_WINDOWS
        const auto terminationSent = processId > 0
            && ::kill (static_cast<pid_t> (processId), SIGINT) == 0;
        check (terminationSent, "could not send SIGINT to the emulator");
        const auto stoppedObserved = terminationSent && waitUntil ([&] {
            return findEvent (readEvents (runDirectory), "stopped") != nullptr;
        });
        check (stoppedObserved, "SIGINT did not produce a stopped event");

        const auto processFinished = process.waitForProcessToFinish (5000);
        const auto processExitCode = processFinished ? process.getExitCode() : 0;
        check (processFinished, "SIGINT did not finish the emulator process");
        if (! processFinished)
            process.kill();
        check (processFinished && processExitCode == 0, "SIGINT did not produce a clean process exit");
       #else
        process.kill();
        process.waitForProcessToFinish (2000);
       #endif

        outputCollector.wait();
        const auto processOutput = outputCollector.getOutput();
        check (! processOutput.contains (processAccessTokenSecret), "access-token fixture leaked to process output");
        check (! processOutput.contains (processIdTokenSecret), "id-token fixture leaked to process output");
        check (! processOutput.contains (processAuthorizationSecret),
               "authorization fixture leaked to process output");

        events = readEvents (runDirectory);
       #if ! JUCE_WINDOWS
        if (const auto* stopped = findEvent (events, "stopped"))
        {
            const auto* payload = stopped->getProperty ("payload").getDynamicObject();
            check (payload != nullptr && payload->getProperty ("reason").toString() == "process_finished",
                   "stopped reason is incorrect");
            check (events.getLast().getDynamicObject() == stopped, "stopped is not the final event");
        }
       #endif

        const auto files = eventFiles (runDirectory);
        check (files.size() == events.size(), "one or more event files are malformed");
        for (int index = 0; index < events.size(); ++index)
        {
            const auto* envelope = events[index].getDynamicObject();
            check (envelope != nullptr, "event envelope is not an object");
            if (envelope == nullptr)
                continue;
            const auto eventId = static_cast<juce::int64> (envelope->getProperty ("eventId"));
            check (static_cast<juce::int64> (envelope->getProperty ("protocolVersion")) == 1,
                   "event protocol version is incorrect");
            check (envelope->getProperty ("runId").toString() == processRunId, "event run ID is incorrect");
            check (eventId == index + 1, "event IDs are not sequential");
            check (files[index].getFileName() == makeSequenceFilename (eventId),
                   "event ID does not match its filename");
            check (! envelope->hasProperty ("id"), "v1 event envelope contains legacy id field");
            check (envelope->getProperty ("occurredAt").toString().isNotEmpty(), "event timestamp is missing");
        }

        for (juce::int64 sequence = 1; sequence <= 2; ++sequence)
        {
            juce::var command;
            const auto file = runDirectory.getChildFile ("commands").getChildFile (makeSequenceFilename (sequence));
            const auto parsed = juce::JSON::parse (file.loadFileAsString(), command);
            const auto* envelope = command.getDynamicObject();
            check (parsed.wasOk() && envelope != nullptr, "command envelope is malformed");
            if (envelope != nullptr)
            {
                check (static_cast<juce::int64> (envelope->getProperty ("protocolVersion")) == 1,
                       "command protocol version is incorrect");
                check (envelope->getProperty ("commandId").toString().isNotEmpty(), "command ID is missing");
                check (! envelope->hasProperty ("id"), "v1 command envelope contains legacy id field");
            }
        }

        check (! hasTemporaryFiles (runDirectory), "temporary protocol files were left behind");
        juce::String serializedEvents;
        for (const auto& event : events)
            serializedEvents += juce::JSON::toString (event);
        check (! serializedEvents.contains (processPublicKey), "public key leaked into protocol events");

        if (passed)
            runDirectory.deleteRecursively();
        else
            logMessage ("Retained failing emulator run directory: " + runDirectory.getFullPathName());

        beginTest ("emits a credential-safe fatal envelope when a valid run cannot be configured");
        const auto invalidRunDirectory = createRunDirectory();
        bool invalidPassed = true;
        const auto invalidCheck = [&] (bool condition, const juce::String& message) {
            expect (condition, message);
            invalidPassed = invalidPassed && condition;
            return condition;
        };

        if (! invalidCheck (writeConfig (invalidRunDirectory) && invalidateConfigModule (invalidRunDirectory),
                            "could not create invalid process-test configuration"))
        {
            logMessage ("Retained failing emulator run directory: " + invalidRunDirectory.getFullPathName());
            return;
        }

        juce::ChildProcess invalidProcess;
        const juce::StringArray invalidArgv {
            INLAY_EMULATOR_PATH, "--run-dir", invalidRunDirectory.getFullPathName()
        };
        if (! invalidCheck (invalidProcess.start (invalidArgv), "could not launch invalid-config emulator"))
        {
            logMessage ("Retained failing emulator run directory: " + invalidRunDirectory.getFullPathName());
            return;
        }
        ProcessOutputCollector invalidOutputCollector (invalidProcess);

        const auto invalidFinished = invalidProcess.waitForProcessToFinish (5000);
        const auto invalidExitCode = invalidFinished ? invalidProcess.getExitCode() : 0;
        invalidCheck (invalidFinished, "invalid-config emulator did not exit");
        if (! invalidFinished)
            invalidProcess.kill();
        invalidCheck (invalidFinished && invalidExitCode != 0, "invalid-config emulator returned success");
        invalidOutputCollector.wait();
        const auto invalidProcessOutput = invalidOutputCollector.getOutput();
        invalidCheck (! invalidProcessOutput.contains (processAccessTokenSecret),
                      "access-token fixture leaked to fatal process output");
        invalidCheck (! invalidProcessOutput.contains (processIdTokenSecret),
                      "id-token fixture leaked to fatal process output");
        invalidCheck (! invalidProcessOutput.contains (processAuthorizationSecret),
                      "authorization fixture leaked to fatal process output");

        const auto invalidEvents = readEvents (invalidRunDirectory);
        const auto* fatal = findEvent (invalidEvents, "fatal");
        invalidCheck (fatal != nullptr, "fatal event is missing");
        if (fatal != nullptr)
        {
            const auto* payload = fatal->getProperty ("payload").getDynamicObject();
            invalidCheck (static_cast<juce::int64> (fatal->getProperty ("protocolVersion")) == 1,
                          "fatal protocol version is incorrect");
            invalidCheck (fatal->getProperty ("runId").toString() == processRunId,
                          "fatal run ID is incorrect");
            invalidCheck (static_cast<juce::int64> (fatal->getProperty ("eventId")) == 1,
                          "fatal event ID is incorrect");
            invalidCheck (payload != nullptr && payload->getProperty ("code").toString() == "invalid_configuration",
                          "fatal code is incorrect");
            invalidCheck (payload != nullptr
                              && payload->getProperty ("message").toString() == "emulator configuration is invalid",
                          "fatal message is incorrect");
        }

        juce::String invalidSerializedEvents;
        for (const auto& event : invalidEvents)
            invalidSerializedEvents += juce::JSON::toString (event);
        invalidCheck (! invalidSerializedEvents.contains (processPublicKey),
                      "public key leaked into fatal protocol events");
        invalidCheck (! hasTemporaryFiles (invalidRunDirectory),
                      "temporary fatal protocol files were left behind");

        if (invalidPassed)
            invalidRunDirectory.deleteRecursively();
        else
            logMessage ("Retained failing emulator run directory: " + invalidRunDirectory.getFullPathName());

        beginTest ("fails before ready when emulator state storage is unusable");
        const auto stateFailureDirectory = createRunDirectory();
        bool stateFailurePassed = true;
        const auto stateFailureCheck = [&] (bool condition, const juce::String& message) {
            expect (condition, message);
            stateFailurePassed = stateFailurePassed && condition;
            return condition;
        };

        if (! stateFailureCheck (writeConfig (stateFailureDirectory)
                                    && stateFailureDirectory.getChildFile ("state").replaceWithText ("not a directory"),
                                 "could not create unusable-state process fixture"))
        {
            logMessage ("Retained failing emulator run directory: " + stateFailureDirectory.getFullPathName());
            return;
        }

        juce::ChildProcess stateFailureProcess;
        const juce::StringArray stateFailureArgv {
            INLAY_EMULATOR_PATH, "--run-dir", stateFailureDirectory.getFullPathName()
        };
        if (! stateFailureCheck (stateFailureProcess.start (stateFailureArgv),
                                 "could not launch unusable-state emulator"))
        {
            logMessage ("Retained failing emulator run directory: " + stateFailureDirectory.getFullPathName());
            return;
        }
        ProcessOutputCollector stateFailureOutputCollector (stateFailureProcess);

        const auto stateFailureFinished = stateFailureProcess.waitForProcessToFinish (5000);
        const auto stateFailureExitCode = stateFailureFinished ? stateFailureProcess.getExitCode() : 0;
        stateFailureCheck (stateFailureFinished, "unusable-state emulator did not exit");
        if (! stateFailureFinished)
            stateFailureProcess.kill();
        stateFailureCheck (stateFailureFinished && stateFailureExitCode != 0,
                           "unusable-state emulator returned success");
        stateFailureOutputCollector.wait();

        const auto stateFailureEvents = readEvents (stateFailureDirectory);
        const auto* stateFatal = findEvent (stateFailureEvents, "fatal");
        stateFailureCheck (stateFatal != nullptr, "unusable state did not produce a fatal event");
        stateFailureCheck (findEvent (stateFailureEvents, "ready") == nullptr,
                           "ready was emitted before emulator state validation");
        if (stateFatal != nullptr)
        {
            const auto* payload = stateFatal->getProperty ("payload").getDynamicObject();
            stateFailureCheck (payload != nullptr
                                   && payload->getProperty ("code").toString() == "startup_failed",
                               "unusable-state fatal code is incorrect");
        }
        stateFailureCheck (! hasTemporaryFiles (stateFailureDirectory),
                           "temporary unusable-state protocol files were left behind");

        if (stateFailurePassed)
            stateFailureDirectory.deleteRecursively();
        else
            logMessage ("Retained failing emulator run directory: " + stateFailureDirectory.getFullPathName());

        beginTest ("emits fatal before ready when the durable command cursor is corrupt");
        const auto corruptCursorDirectory = createRunDirectory();
        bool corruptCursorPassed = true;
        const auto corruptCursorCheck = [&] (bool condition, const juce::String& message) {
            expect (condition, message);
            corruptCursorPassed = corruptCursorPassed && condition;
            return condition;
        };

        if (! corruptCursorCheck (
                writeConfig (corruptCursorDirectory)
                    && corruptCursorDirectory.getChildFile ("state").createDirectory()
                    && corruptCursorDirectory.getChildFile ("state").getChildFile ("cursor.json")
                           .replaceWithText ("not JSON"),
                "could not create corrupt-cursor process fixture"))
        {
            logMessage ("Retained failing emulator run directory: " + corruptCursorDirectory.getFullPathName());
            return;
        }

        juce::ChildProcess corruptCursorProcess;
        const juce::StringArray corruptCursorArgv {
            INLAY_EMULATOR_PATH, "--run-dir", corruptCursorDirectory.getFullPathName()
        };
        if (! corruptCursorCheck (corruptCursorProcess.start (corruptCursorArgv),
                                  "could not launch corrupt-cursor emulator"))
        {
            logMessage ("Retained failing emulator run directory: " + corruptCursorDirectory.getFullPathName());
            return;
        }
        ProcessOutputCollector corruptCursorOutputCollector (corruptCursorProcess);

        const auto corruptCursorFinished = corruptCursorProcess.waitForProcessToFinish (5000);
        const auto corruptCursorExitCode = corruptCursorFinished ? corruptCursorProcess.getExitCode() : 0;
        corruptCursorCheck (corruptCursorFinished, "corrupt-cursor emulator did not exit");
        if (! corruptCursorFinished)
            corruptCursorProcess.kill();
        corruptCursorCheck (corruptCursorFinished && corruptCursorExitCode != 0,
                            "corrupt-cursor emulator returned success");
        corruptCursorOutputCollector.wait();

        const auto corruptCursorEvents = readEvents (corruptCursorDirectory);
        const auto* corruptCursorFatal = findEvent (corruptCursorEvents, "fatal");
        corruptCursorCheck (corruptCursorFatal != nullptr, "corrupt cursor did not produce a fatal event");
        corruptCursorCheck (findEvent (corruptCursorEvents, "ready") == nullptr,
                            "ready was emitted before durable cursor validation");
        if (corruptCursorFatal != nullptr)
        {
            const auto* payload = corruptCursorFatal->getProperty ("payload").getDynamicObject();
            corruptCursorCheck (payload != nullptr
                                    && payload->getProperty ("code").toString() == "startup_failed",
                                "corrupt-cursor fatal code is incorrect");
        }
        corruptCursorCheck (! hasTemporaryFiles (corruptCursorDirectory),
                            "temporary corrupt-cursor protocol files were left behind");

        if (corruptCursorPassed)
            corruptCursorDirectory.deleteRecursively();
        else
            logMessage ("Retained failing emulator run directory: " + corruptCursorDirectory.getFullPathName());
    }
};

static ProcessTests processTests;
} // namespace inlay::emulator
