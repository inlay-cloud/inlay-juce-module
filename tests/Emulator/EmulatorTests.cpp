#include "Emulator.h"
#include "../../modules/inlay_product_unlocking/internal/ModuleVersion.h"

#include <thread>
#include <vector>

namespace inlay::emulator
{
namespace
{
juce::File createRunDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("inlay-real-emulator-" + juce::Uuid().toString());
    directory.createDirectory();
    return directory;
}

juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

void writeCommand (const juce::File& directory, juce::int64 sequence, const juce::String& commandId,
                   const juce::String& method, const juce::var& arguments)
{
    auto command = object();
    auto* envelope = command.getDynamicObject();
    envelope->setProperty ("protocolVersion", protocolVersion);
    envelope->setProperty ("runId", "run-001");
    envelope->setProperty ("commandId", commandId);
    envelope->setProperty ("sequence", sequence);
    envelope->setProperty ("target", "unlocker");
    envelope->setProperty ("method", method);
    envelope->setProperty ("arguments", arguments);
    const auto commands = directory.getChildFile ("commands");
    commands.createDirectory();
    commands.getChildFile (makeSequenceFilename (sequence)).replaceWithText (juce::JSON::toString (command));
}

juce::Array<juce::var> readEvents (const juce::File& directory)
{
    juce::Array<juce::File> files;
    directory.getChildFile ("events").findChildFiles (files, juce::File::findFiles, false, "*.json");
    std::sort (files.begin(), files.end(), [] (const auto& left, const auto& right) {
        return left.getFileName() < right.getFileName();
    });

    juce::Array<juce::var> result;
    for (const auto& file : files)
    {
        juce::var event;
        if (juce::JSON::parse (file.loadFileAsString(), event).wasOk())
            result.add (event);
    }
    return result;
}

const juce::DynamicObject* findEvent (const juce::Array<juce::var>& events, const juce::String& type,
                                      const juce::String& commandId = {})
{
    for (const auto& event : events)
        if (const auto* object = event.getDynamicObject(); object != nullptr
            && object->getProperty ("type").toString() == type
            && (commandId.isEmpty() || object->getProperty ("commandId").toString() == commandId))
            return object;
    return nullptr;
}

int countEvents (const juce::Array<juce::var>& events, const juce::String& type)
{
    int count = 0;
    for (const auto& event : events)
        if (const auto* object = event.getDynamicObject(); object != nullptr
            && object->getProperty ("type").toString() == type)
            ++count;
    return count;
}

bool waitUntil (std::function<bool()> predicate, int timeoutMs = 3000)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
    return predicate();
}

EmulatorConfig configFor (const juce::File& directory)
{
    const auto storage = directory.getChildFile ("unlocker-storage");
    storage.createDirectory();
    return { "run-001", "http://127.0.0.1:1", "product-001", "public-key", storage, {} };
}

} // namespace

class EmulatorTests final : public juce::UnitTest
{
public:
    EmulatorTests() : juce::UnitTest ("Real Unlocker Emulator", "inlay_product_unlocking") {}

    void runTest() override
    {
        beginTest ("starts the real unlocker before ready and emits complete snapshots");
        {
            const auto directory = createRunDirectory();
            juce::String fatal;
            Emulator emulator (directory, configFor (directory), [&] (juce::String message) { fatal = message; });

            expect (emulator.start().wasOk());
            expect (waitUntil ([&] {
                const auto events = readEvents (directory);
                const auto* changed = findEvent (events, "unlocker_changed");
                return changed != nullptr
                    && changed->getProperty ("payload").getDynamicObject()->getProperty ("status").toString()
                           == "activation_required";
            }));

            const auto events = readEvents (directory);
            expect (events.size() >= 2);
            const auto* ready = events.getFirst().getDynamicObject();
            expectEquals (ready->getProperty ("type").toString(), juce::String ("ready"));
            const auto* readyPayload = ready->getProperty ("payload").getDynamicObject();
            expectEquals (readyPayload->getProperty ("module").toString(), juce::String ("juce"));
            expectEquals (readyPayload->getProperty ("moduleVersion").toString(),
                          juce::String (inlay::internal::moduleVersion));
            expectEquals (static_cast<int> (readyPayload->getProperty ("protocolVersion")), protocolVersion);
            expect (static_cast<juce::int64> (readyPayload->getProperty ("pid")) > 0);
            const juce::StringArray expectedMethods {
                "getStatus", "isLocked", "getError", "getCurrentUser", "getAppUpdate", "startup",
                "startActivation", "retryUnlocking", "logout", "skipCurrentAppUpdateVersion", "openWebsite"
            };
            const auto* methods = readyPayload->getProperty ("supportedPublicMethods").getArray();
            expect (methods != nullptr);
            expectEquals (methods != nullptr ? methods->size() : 0, expectedMethods.size());
            if (methods != nullptr)
                for (int index = 0; index < expectedMethods.size(); ++index)
                    expectEquals ((*methods)[index].toString(), expectedMethods[index]);

            const auto* changed = findEvent (events, "unlocker_changed");
            const auto* snapshot = changed->getProperty ("payload").getDynamicObject();
            expect (snapshot->hasProperty ("status"));
            expect (snapshot->hasProperty ("locked"));
            expect (snapshot->hasProperty ("currentUser"));
            expect (snapshot->hasProperty ("appUpdate"));
            expect (snapshot->hasProperty ("error"));
            expectEquals (snapshot->getProperty ("status").toString(), juce::String ("activation_required"));
            expect (static_cast<bool> (snapshot->getProperty ("locked")));
            expectEquals (snapshot->getProperty ("currentUser").toString(), juce::String());
            expect (snapshot->getProperty ("appUpdate").isVoid());
            expectEquals (snapshot->getProperty ("error").toString(), juce::String());
            expect (fatal.isEmpty());

            emulator.stop ("test_finished");
            emulator.stop ("duplicate");
            const auto stoppedEvents = readEvents (directory);
            expectEquals (countEvents (stoppedEvents, "stopped"), 1);
            const auto* stopped = findEvent (stoppedEvents, "stopped");
            expectEquals (stopped->getProperty ("payload").getDynamicObject()->getProperty ("reason").toString(),
                          juce::String ("test_finished"));
            expectEquals (stoppedEvents.getLast().getDynamicObject()->getProperty ("type").toString(),
                          juce::String ("stopped"));
            directory.deleteRecursively();
        }

        beginTest ("rejects corrupt durable command state before emitting ready");
        {
            const auto directory = createRunDirectory();
            const auto commandStates = directory.getChildFile ("state").getChildFile ("commands");
            expect (commandStates.createDirectory());
            commandStates.getChildFile (juce::String::repeatedString ("0", 64) + ".json")
                .replaceWithText ("not JSON");
            Emulator emulator (directory, configFor (directory), [] (juce::String) {});

            expect (emulator.start().failed());
            expect (findEvent (readEvents (directory), "ready") == nullptr);

            emulator.stop ("test_finished");
            directory.deleteRecursively();
        }

       #if ! JUCE_WINDOWS
        beginTest ("rejects an emulator state symlink that resolves outside the run directory");
        {
            const auto directory = createRunDirectory();
            const auto outsideState = directory.getSiblingFile (directory.getFileName() + "-linked-state");
            expect (outsideState.createDirectory());
            expect (outsideState.createSymbolicLink (directory.getChildFile ("state"), true));
            Emulator emulator (directory, configFor (directory), [] (juce::String) {});

            expect (emulator.start().failed());
            expect (findEvent (readEvents (directory), "ready") == nullptr);

            emulator.stop ("test_finished");
            directory.getChildFile ("state").deleteFile();
            outsideState.deleteRecursively();
            directory.deleteRecursively();
        }
       #endif

        beginTest ("maps all public methods and substitutes browser opening with an exact event");
        {
            const auto directory = createRunDirectory();
            auto urlArguments = object();
            urlArguments.getDynamicObject()->setProperty ("url", "https://example.com/exact?a=1&b=2");
            juce::String fatal;
            Emulator emulator (directory, configFor (directory), [&] (juce::String message) { fatal = message; });
            const auto startActivation = emulator.invoke ({}, "startActivation", object());
            const auto retry = emulator.invoke ({}, "retryUnlocking", object());
            const auto logout = emulator.invoke ({}, "logout", object());
            const auto skip = emulator.invoke ({}, "skipCurrentAppUpdateVersion", object());
            const auto website = emulator.invoke ({}, "openWebsite", urlArguments);
            const auto status = emulator.invoke ({}, "getStatus", object());
            const auto locked = emulator.invoke ({}, "isLocked", object());
            const auto error = emulator.invoke ({}, "getError", object());
            const auto currentUser = emulator.invoke ({}, "getCurrentUser", object());
            const auto appUpdate = emulator.invoke ({}, "getAppUpdate", object());
            const auto startup = emulator.invoke ({}, "startup", object());

            for (const auto* result : { &startActivation, &retry, &logout, &skip, &website, &status,
                                        &locked, &error, &currentUser, &appUpdate, &startup })
                expectEquals (result->outcome, juce::String ("completed"));

            expect (status.output.getDynamicObject()->hasProperty ("status"));
            expect (locked.output.getDynamicObject()->getProperty ("locked").isBool());
            expect (error.output.getDynamicObject()->getProperty ("error").isString());
            expect (currentUser.output.getDynamicObject()->getProperty ("currentUser").isString());
            expect (appUpdate.output.getDynamicObject()->hasProperty ("appUpdate"));
            for (const auto* result : { &startActivation, &retry, &logout, &skip, &website, &startup })
                expectEquals (result->output.getDynamicObject()->getProperties().size(), 0);

            const auto events = readEvents (directory);

            const auto* browser = findEvent (events, "browser_open_requested");
            const auto* browserPayload = browser->getProperty ("payload").getDynamicObject();
            expectEquals (browserPayload->getProperty ("requestId").toString(), juce::String ("browser-001"));
            expectEquals (browserPayload->getProperty ("url").toString(),
                          juce::String ("https://example.com/exact?a=1&b=2"));
            expect (! browser->hasProperty ("commandId"));
            expect (fatal.isEmpty());

            emulator.stop ("test_finished");
            directory.deleteRecursively();
        }

        beginTest ("serializes concurrent browser requests in request ID order");
        {
            const auto directory = createRunDirectory();
            Emulator emulator (directory, configFor (directory), [] (juce::String) {});
            constexpr int requestCount = 32;
            juce::WaitableEvent start (true);
            std::vector<std::thread> threads;
            threads.reserve (requestCount);
            for (int index = 0; index < requestCount; ++index)
                threads.emplace_back ([&, index] {
                    start.wait();
                    auto arguments = object();
                    arguments.getDynamicObject()->setProperty ("url", "https://example.com/" + juce::String (index));
                    emulator.invoke ({}, "openWebsite", arguments);
                });

            start.signal();
            for (auto& thread : threads)
                thread.join();

            const auto events = readEvents (directory);
            juce::Array<const juce::DynamicObject*> browserEvents;
            for (const auto& event : events)
                if (const auto* envelope = event.getDynamicObject(); envelope != nullptr
                    && envelope->getProperty ("type").toString() == "browser_open_requested")
                    browserEvents.add (envelope);

            expectEquals (browserEvents.size(), requestCount);
            for (int index = 0; index < browserEvents.size(); ++index)
                expectEquals (browserEvents[index]->getProperty ("payload").getDynamicObject()
                                  ->getProperty ("requestId").toString(),
                              juce::String::formatted ("browser-%03d", index + 1));
            emulator.stop ("test_finished");
            directory.deleteRecursively();
        }

        beginTest ("redacts credential-like values before event serialization");
        {
            const auto redacted = Emulator::redactErrorForEvent (
                "accessToken=access-secret idToken: id-secret Authorization: Bearer auth-secret "
                "private-key=private-secret");
            expect (! redacted.contains ("access-secret"));
            expect (! redacted.contains ("id-secret"));
            expect (! redacted.contains ("auth-secret"));
            expect (! redacted.contains ("private-secret"));
            expectEquals (redacted,
                          juce::String ("accessToken=[REDACTED] idToken: [REDACTED] Authorization: [REDACTED] "
                                        "private-key=[REDACTED]"));
        }

        beginTest ("redacts credential-like values from every command getter output");
        {
            auto output = object();
            output.getDynamicObject()->setProperty ("error", "accessToken=access-secret");
            output.getDynamicObject()->setProperty ("currentUser", "Authorization: Bearer auth-secret");
            auto appUpdate = object();
            appUpdate.getDynamicObject()->setProperty ("url", "https://example.com/?private-key=private-secret");
            output.getDynamicObject()->setProperty ("appUpdate", appUpdate);

            const auto redacted = Emulator::redactCommandOutput (output);
            const auto text = juce::JSON::toString (redacted);
            expect (! text.contains ("access-secret"));
            expect (! text.contains ("auth-secret"));
            expect (! text.contains ("private-secret"));
            expect (text.contains ("[REDACTED]"));
        }

        beginTest ("associates synchronous openWebsite browser events with their command ID");
        {
            const auto directory = createRunDirectory();
            auto arguments = object();
            arguments.getDynamicObject()->setProperty ("url", "https://example.com/from-command");
            writeCommand (directory, 1, "open-website-command", "openWebsite", arguments);
            Emulator emulator (directory, configFor (directory), [] (juce::String) {});

            expect (emulator.start().wasOk());
            expect (waitUntil ([&] {
                return findEvent (readEvents (directory), "browser_open_requested", "open-website-command") != nullptr;
            }));
            const auto events = readEvents (directory);
            const auto* browser = findEvent (events, "browser_open_requested", "open-website-command");
            expect (browser != nullptr);
            if (browser != nullptr)
                expectEquals (browser->getProperty ("payload").getDynamicObject()->getProperty ("url").toString(),
                              juce::String ("https://example.com/from-command"));
            emulator.stop ("test_finished");
            directory.deleteRecursively();
        }
    }
};

static EmulatorTests emulatorTests;
} // namespace inlay::emulator
