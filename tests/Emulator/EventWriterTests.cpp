#include "EventWriter.h"

#include <atomic>
#include <thread>

namespace inlay::emulator
{
namespace
{
juce::File createDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("inlay-emulator-events-" + juce::Uuid().toString());
    directory.createDirectory();
    return directory;
}

juce::var objectWith (const juce::Identifier& name, const juce::var& value)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty (name, value);
    return juce::var (object.get());
}

void writeExistingEvent (const juce::File& file, juce::int64 id)
{
    file.replaceWithText (juce::JSON::toString (objectWith ("eventId", id)));
}

juce::DynamicObject* objectFor (const juce::var& value)
{
    return value.getDynamicObject();
}
} // namespace

class EventWriterTests final : public juce::UnitTest
{
public:
    EventWriterTests() : juce::UnitTest ("Emulator Event Writer", "inlay_product_unlocking") {}

    void runTest() override
    {
        runEventTests();
        runStateTests();
    }

private:
    void runEventTests()
    {
        beginTest ("recovers event IDs from final event files only");
        {
            const auto directory = createDirectory();
            const auto events = directory.getChildFile ("events");
            expect (events.createDirectory());
            writeExistingEvent (events.getChildFile ("000000000004.json"), 4);
            writeExistingEvent (events.getChildFile ("not-an-event.json"), 999);
            writeExistingEvent (events.getChildFile ("000000000005.json.tmp"), 999);

            EventWriter writer (events, "run-001");
            expect (writer.emit ("ready", objectWith ("answer", 42)).wasOk());
            expect (events.getChildFile ("000000000005.json").existsAsFile());
            expect (! events.getChildFile ("000000000006.json").existsAsFile());
            directory.deleteRecursively();
        }

        beginTest ("writes complete event envelopes atomically");
        {
            const auto directory = createDirectory();
            const auto events = directory.getChildFile ("events");
            EventWriter writer (events, "run-001");
            const auto payload = objectWith ("status", "ready");

            expect (writer.emit ("snapshot", payload, "command-7").wasOk());
            juce::var event;
            expect (juce::JSON::parse (events.getChildFile ("000000000001.json").loadFileAsString(), event).wasOk());
            const auto* envelope = objectFor (event);
            expect (envelope != nullptr);
            expectEquals (static_cast<juce::int64> (envelope->getProperty ("protocolVersion")), static_cast<juce::int64> (protocolVersion));
            expectEquals (envelope->getProperty ("runId").toString(), juce::String ("run-001"));
            expectEquals (envelope->getProperty ("type").toString(), juce::String ("snapshot"));
            expectEquals (static_cast<juce::int64> (envelope->getProperty ("eventId")), static_cast<juce::int64> (1));
            expect (! envelope->hasProperty ("id"));
            expectEquals (envelope->getProperty ("commandId").toString(), juce::String ("command-7"));
            expectEquals (objectFor (envelope->getProperty ("payload"))->getProperty ("status").toString(), juce::String ("ready"));
            expect (envelope->getProperty ("occurredAt").toString().isNotEmpty());
            expectEquals (events.findChildFiles (juce::File::findFiles, false, "*.tmp").size(), 0);
            directory.deleteRecursively();
        }

        beginTest ("concurrent writers allocate distinct final event IDs");
        {
            const auto directory = createDirectory();
            const auto events = directory.getChildFile ("events");
            EventWriter writer (events, "run-001");
            std::atomic<int> failures { 0 };
            std::thread first ([&] { if (writer.emit ("first", {}).failed()) ++failures; });
            std::thread second ([&] { if (writer.emit ("second", {}).failed()) ++failures; });
            first.join();
            second.join();

            expectEquals (failures.load(), 0);
            expect (events.getChildFile ("000000000001.json").existsAsFile());
            expect (events.getChildFile ("000000000002.json").existsAsFile());
            directory.deleteRecursively();
        }

        beginTest ("finds previously emitted command completions");
        {
            const auto directory = createDirectory();
            const auto events = directory.getChildFile ("events");
            EventWriter writer (events, "run-001");
            expect (writer.emit ("command_completed", objectWith ("outcome", "ok"), "command-7").wasOk());

            EventWriter recovered (events, "run-001");
            expect (recovered.hasCommandCompletion ("command-7"));
            expect (! recovered.hasCommandCompletion ("command-8"));
            directory.deleteRecursively();
        }
    }

    void runStateTests()
    {
        beginTest ("stores command state under a SHA-256 path and restores processing state");
        {
            const auto directory = createDirectory();
            CommandStateStore states (directory.getChildFile ("state"));
            expect (states.markProcessing ("command-7", 12).wasOk());
            const juce::String commandId ("command-7");
            const auto expectedName = juce::SHA256 (commandId.toRawUTF8(), commandId.getNumBytesAsUTF8()).toHexString() + ".json";
            expect (directory.getChildFile ("state").getChildFile ("commands").getChildFile (expectedName).existsAsFile());

            const auto stored = states.load ("command-7");
            expect (stored.has_value());
            expectEquals (stored->commandId, juce::String ("command-7"));
            expectEquals (stored->sequence, static_cast<juce::int64> (12));
            expect (stored->phase == CommandPhase::processing);
            expect (! stored->completionEmitted);
            directory.deleteRecursively();
        }

        beginTest ("restores completed outcome and completion emission state");
        {
            const auto directory = createDirectory();
            CommandStateStore states (directory.getChildFile ("state"));
            InvocationResult result { "failed", objectWith ("retryable", false), SafeError { "denied", "Access denied" } };
            expect (states.markProcessing ("command-8", 13).wasOk());
            expect (states.markCompleted ("command-8", result).wasOk());
            expect (states.setCompletionEmitted ("command-8").wasOk());

            CommandStateStore recovered (directory.getChildFile ("state"));
            const auto stored = recovered.load ("command-8");
            expect (stored.has_value());
            expect (stored->phase == CommandPhase::completed);
            expectEquals (stored->outcome->outcome, juce::String ("failed"));
            expectEquals (objectFor (stored->outcome->output)->getProperty ("retryable").toString(), juce::String ("0"));
            expect (stored->outcome->error.has_value());
            expectEquals (stored->outcome->error->code, juce::String ("denied"));
            expect (stored->completionEmitted);
            directory.deleteRecursively();
        }
    }
};

static EventWriterTests eventWriterTests;
} // namespace inlay::emulator
