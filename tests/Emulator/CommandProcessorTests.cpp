#include "CommandProcessor.h"

namespace inlay::emulator
{
namespace
{
juce::File createDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("inlay-emulator-commands-" + juce::Uuid().toString());
    directory.createDirectory();
    return directory;
}

juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

juce::var objectWith (const juce::Identifier& name, const juce::var& value)
{
    auto valueObject = object();
    valueObject.getDynamicObject()->setProperty (name, value);
    return valueObject;
}

juce::var command (const juce::String& runId, juce::int64 sequence, const juce::String& id,
                   const juce::String& method = "getStatus", const juce::var& arguments = object())
{
    auto value = object();
    auto* envelope = value.getDynamicObject();
    envelope->setProperty ("protocolVersion", protocolVersion);
    envelope->setProperty ("runId", runId);
    envelope->setProperty ("commandId", id);
    envelope->setProperty ("sequence", sequence);
    envelope->setProperty ("target", "unlocker");
    envelope->setProperty ("method", method);
    envelope->setProperty ("arguments", arguments);
    return value;
}

void writeCommand (const juce::File& directory, juce::int64 sequence, const juce::var& value)
{
    const auto commands = directory.getChildFile ("commands");
    commands.createDirectory();
    commands.getChildFile (makeSequenceFilename (sequence)).replaceWithText (juce::JSON::toString (value));
}

juce::var readEventPayload (const juce::File& events, juce::int64 id)
{
    juce::var event;
    juce::JSON::parse (events.getChildFile (makeSequenceFilename (id)).loadFileAsString(), event);
    return event.getDynamicObject()->getProperty ("payload");
}

class FakeTarget final : public CommandTarget
{
public:
    InvocationResult invoke (const juce::String&, const juce::String& method, const juce::var& arguments) override
    {
        calls.add (method);
        lastArguments = arguments;
        if (throwOnInvoke)
            throw std::runtime_error ("target failed");
        return { "completed", object(), std::nullopt };
    }

    juce::StringArray calls;
    juce::var lastArguments;
    bool throwOnInvoke = false;
};

class CommandProcessorTests final : public juce::UnitTest
{
public:
    CommandProcessorTests() : juce::UnitTest ("Emulator Command Processor", "inlay_product_unlocking") {}

    void runTest() override
    {
        beginTest ("processes commands in filename order and writes one completion with an object output");
        {
            const auto directory = createDirectory();
            writeCommand (directory, 2, command ("run-001", 2, "second", "isLocked"));
            writeCommand (directory, 1, command ("run-001", 1, "first", "getStatus"));
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            FakeTarget target;
            CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});

            processor.processPendingNow();

            expectEquals (target.calls.size(), 2);
            expectEquals (target.calls[0], juce::String ("getStatus"));
            expectEquals (target.calls[1], juce::String ("isLocked"));
            expect (events.hasCommandCompletion ("first"));
            expect (events.hasCommandCompletion ("second"));
            const auto firstPayload = readEventPayload (directory.getChildFile ("events"), 1);
            expect (firstPayload.getDynamicObject()->getProperty ("output").getDynamicObject() != nullptr);
            processor.processPendingNow();
            expectEquals (target.calls.size(), 2);
            directory.deleteRecursively();
        }

        beginTest ("rejects invalid envelopes with a stable filename fallback ID and advances past malformed files");
        {
            const auto directory = createDirectory();
            writeCommand (directory, 1, command ("wrong-run", 1, "bad-run"));
            directory.getChildFile ("commands").getChildFile (makeSequenceFilename (2)).replaceWithText ("not JSON");
            writeCommand (directory, 3, command ("run-001", 3, "good", "getError"));
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            FakeTarget target;
            CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});

            processor.processPendingNow();

            expectEquals (target.calls.size(), 1);
            expectEquals (target.calls[0], juce::String ("getError"));
            expect (events.hasCommandCompletion ("bad-run"));
            expect (events.hasCommandCompletion ("invalid-command-000000000002"));
            const auto rejectedPayload = readEventPayload (directory.getChildFile ("events"), 2);
            const auto rejected = rejectedPayload.getDynamicObject();
            expectEquals (rejected->getProperty ("outcome").toString(), juce::String ("rejected"));
            expect (rejected->getProperty ("output").getDynamicObject() != nullptr);
            directory.deleteRecursively();
        }

        beginTest ("waits for sequence gaps and rejects invalid target method sequence and arguments");
        {
            const auto directory = createDirectory();
            writeCommand (directory, 2, command ("run-001", 2, "later"));
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            FakeTarget target;
            CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});
            processor.processPendingNow();
            expectEquals (target.calls.size(), 0);

            auto invalid = command ("run-001", 1, "invalid", "unsupported", juce::var (42));
            invalid.getDynamicObject()->setProperty ("target", "other");
            invalid.getDynamicObject()->setProperty ("sequence", 9);
            writeCommand (directory, 1, invalid);
            processor.processPendingNow();
            expectEquals (target.calls.size(), 1);
            expectEquals (target.calls[0], juce::String ("getStatus"));
            expect (events.hasCommandCompletion ("invalid"));
            expect (events.hasCommandCompletion ("later"));
            directory.deleteRecursively();
        }

        beginTest ("requires an ID and exact method argument shapes");
        {
            const auto directory = createDirectory();
            auto missingId = command ("run-001", 1, "unused");
            missingId.getDynamicObject()->removeProperty ("commandId");
            writeCommand (directory, 1, missingId);
            writeCommand (directory, 2, command ("run-001", 2, "extra", "getStatus", objectWith ("extra", true)));
            writeCommand (directory, 3, command ("run-001", 3, "bad-url", "openWebsite"));
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            FakeTarget target;
            CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});

            processor.processPendingNow();

            expectEquals (target.calls.size(), 0);
            expect (events.hasCommandCompletion ("invalid-command-000000000001"));
            expect (events.hasCommandCompletion ("extra"));
            expect (events.hasCommandCompletion ("bad-url"));
            directory.deleteRecursively();
        }

        beginTest ("rejects each invalid envelope field without invoking the target");
        {
            const auto verifyRejected = [&] (const juce::var& invalid) {
                const auto directory = createDirectory();
                writeCommand (directory, 1, invalid);
                EventWriter events (directory.getChildFile ("events"), "run-001");
                CommandStateStore states (directory.getChildFile ("state"));
                FakeTarget target;
                CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});

                processor.processPendingNow();

                expectEquals (target.calls.size(), 0);
                const auto payload = readEventPayload (directory.getChildFile ("events"), 1);
                expectEquals (payload.getDynamicObject()->getProperty ("outcome").toString(), juce::String ("rejected"));
                directory.deleteRecursively();
            };

            auto wrongVersion = command ("run-001", 1, "wrong-version");
            wrongVersion.getDynamicObject()->setProperty ("protocolVersion", protocolVersion + 1);
            verifyRejected (wrongVersion);

            auto wrongTarget = command ("run-001", 1, "wrong-target");
            wrongTarget.getDynamicObject()->setProperty ("target", "other");
            verifyRejected (wrongTarget);

            verifyRejected (command ("run-001", 1, "wrong-method", "notSupported"));

            auto wrongSequence = command ("run-001", 1, "wrong-sequence");
            wrongSequence.getDynamicObject()->setProperty ("sequence", 2);
            verifyRejected (wrongSequence);

            verifyRejected (command ("run-001", 1, "wrong-arguments", "getStatus", juce::var (42)));
        }

        beginTest ("honors persisted completion emission even if the event file is unavailable");
        {
            const auto directory = createDirectory();
            writeCommand (directory, 1, command ("run-001", 1, "already-emitted"));
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            expect (states.markProcessing ("already-emitted", 1).wasOk());
            expect (states.markCompleted ("already-emitted", { "completed", object(), std::nullopt }).wasOk());
            expect (states.setCompletionEmitted ("already-emitted").wasOk());
            FakeTarget target;
            CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});

            processor.processPendingNow();

            expectEquals (target.calls.size(), 0);
            expect (! events.hasCommandCompletion ("already-emitted"));
            expect (directory.getChildFile ("state").getChildFile ("cursor.json").existsAsFile());
            directory.deleteRecursively();
        }

        beginTest ("redacts stored completed outcomes before their first completion event");
        {
            const auto directory = createDirectory();
            writeCommand (directory, 1, command ("run-001", 1, "stored-get-error", "getError"));
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            auto output = objectWith ("error", "accessToken=stored-access-secret");
            expect (states.markProcessing ("stored-get-error", 1).wasOk());
            expect (states.markCompleted ("stored-get-error", {
                "completed", output, SafeError { "stored_error", "Authorization: Bearer stored-auth-secret" }
            }).wasOk());
            FakeTarget target;
            CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});

            processor.processPendingNow();

            expectEquals (target.calls.size(), 0);
            const auto payload = readEventPayload (directory.getChildFile ("events"), 1);
            const auto serialized = juce::JSON::toString (payload);
            expect (! serialized.contains ("stored-access-secret"));
            expect (! serialized.contains ("stored-auth-secret"));
            expect (serialized.contains ("[REDACTED]"));
            directory.deleteRecursively();
        }

        beginTest ("uses a fixed safe error when the target throws");
        {
            const auto directory = createDirectory();
            writeCommand (directory, 1, command ("run-001", 1, "throwing"));
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            FakeTarget target;
            target.throwOnInvoke = true;
            CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});

            processor.processPendingNow();

            const auto payload = readEventPayload (directory.getChildFile ("events"), 1);
            const auto* error = payload.getDynamicObject()->getProperty ("error").getDynamicObject();
            expectEquals (error->getProperty ("code").toString(), juce::String ("invocation_failed"));
            expectEquals (error->getProperty ("message").toString(), juce::String ("command invocation failed"));
            expect (! juce::JSON::toString (payload).contains ("target failed"));
            directory.deleteRecursively();
        }

        beginTest ("replays saved outcomes without invoking and recovers interrupted processing without reinvoking");
        {
            const auto directory = createDirectory();
            writeCommand (directory, 1, command ("run-001", 1, "completed"));
            writeCommand (directory, 2, command ("run-001", 2, "interrupted"));
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            expect (states.markProcessing ("completed", 1).wasOk());
            expect (states.markCompleted ("completed", { "completed", object(), std::nullopt }).wasOk());
            expect (states.markProcessing ("interrupted", 2).wasOk());
            FakeTarget target;
            CommandProcessor processor (directory, "run-001", events, states, target, [] (juce::String) {});

            processor.processPendingNow();

            expectEquals (target.calls.size(), 0);
            expect (events.hasCommandCompletion ("completed"));
            expect (events.hasCommandCompletion ("interrupted"));
            const auto recoveredPayload = readEventPayload (directory.getChildFile ("events"), 2);
            const auto recovered = recoveredPayload.getDynamicObject();
            expectEquals (recovered->getProperty ("outcome").toString(), juce::String ("failed"));
            expectEquals (recovered->getProperty ("error").getDynamicObject()->getProperty ("code").toString(), juce::String ("interrupted_before_completion"));
            processor.processPendingNow();
            expectEquals (target.calls.size(), 0);
            directory.deleteRecursively();
        }

        beginTest ("stops on corrupt persisted command state instead of reinvoking");
        {
            const auto directory = createDirectory();
            writeCommand (directory, 1, command ("run-001", 1, "corrupt"));
            const juce::String commandId ("corrupt");
            const auto hash = juce::SHA256 (commandId.toRawUTF8(), commandId.getNumBytesAsUTF8()).toHexString();
            const auto stateFile = directory.getChildFile ("state").getChildFile ("commands").getChildFile (hash + ".json");
            stateFile.getParentDirectory().createDirectory();
            stateFile.replaceWithText ("not JSON");
            EventWriter events (directory.getChildFile ("events"), "run-001");
            CommandStateStore states (directory.getChildFile ("state"));
            FakeTarget target;
            juce::String fatal;
            CommandProcessor processor (directory, "run-001", events, states, target, [&] (juce::String message) { fatal = message; });

            processor.processPendingNow();

            expectEquals (target.calls.size(), 0);
            expect (fatal.isNotEmpty());
            expect (! events.hasCommandCompletion ("corrupt"));
            directory.deleteRecursively();
        }
    }
};

static CommandProcessorTests commandProcessorTests;
} // namespace
} // namespace inlay::emulator
