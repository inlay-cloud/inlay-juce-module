#include "EventWriter.h"

#include <mutex>

namespace inlay::emulator
{
namespace
{
std::mutex eventWriteMutex;

juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

bool isFinalEventFilename (const juce::String& name)
{
    if (name.length() != 17 || ! name.endsWith (".json"))
        return false;

    for (int index = 0; index < 12; ++index)
        if (! juce::CharacterFunctions::isDigit (name[index]))
            return false;

    return true;
}

juce::int64 largestEventId (const juce::File& eventsDir)
{
    juce::Array<juce::File> files;
    eventsDir.findChildFiles (files, juce::File::findFiles, false, "*");

    juce::int64 largest = 0;
    for (const auto& file : files)
        if (const auto name = file.getFileName(); isFinalEventFilename (name))
            largest = juce::jmax (largest, name.substring (0, 12).getLargeIntValue());

    return largest;
}

juce::Result fail (const juce::String& message)
{
    return juce::Result::fail ("Emulator storage: " + message);
}

juce::var serializeOutcome (const InvocationResult& outcome)
{
    auto value = makeObject();
    auto* object = value.getDynamicObject();
    object->setProperty ("outcome", outcome.outcome);
    object->setProperty ("output", outcome.output);

    if (outcome.error.has_value())
    {
        auto error = makeObject();
        error.getDynamicObject()->setProperty ("code", outcome.error->code);
        error.getDynamicObject()->setProperty ("message", outcome.error->message);
        object->setProperty ("error", error);
    }

    return value;
}

std::optional<InvocationResult> deserializeOutcome (const juce::var& value)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || ! object->getProperty ("outcome").isString() || ! object->hasProperty ("output"))
        return std::nullopt;

    InvocationResult result { object->getProperty ("outcome").toString(), object->getProperty ("output"), std::nullopt };
    const auto error = object->getProperty ("error");
    if (! error.isVoid())
    {
        const auto* errorObject = error.getDynamicObject();
        if (errorObject == nullptr || ! errorObject->getProperty ("code").isString()
            || ! errorObject->getProperty ("message").isString())
            return std::nullopt;
        result.error = SafeError { errorObject->getProperty ("code").toString(), errorObject->getProperty ("message").toString() };
    }

    return result;
}
} // namespace

juce::Result writeJsonAtomically (const juce::File& target, const juce::var& value)
{
    if (! target.getParentDirectory().createDirectory())
        return fail ("cannot create " + target.getParentDirectory().getFullPathName());

    juce::TemporaryFile temporary (target, juce::TemporaryFile::useHiddenFile);
    {
        juce::FileOutputStream stream (temporary.getFile());
        if (stream.failedToOpen())
            return stream.getStatus();

        stream.writeText (juce::JSON::toString (value), false, false, "\n");
        stream.flush();
        if (stream.getStatus().failed())
            return stream.getStatus();
    }

    if (! temporary.overwriteTargetFileWithTemporary())
        return fail ("cannot replace " + target.getFullPathName());

    return juce::Result::ok();
}

EventWriter::EventWriter (juce::File directory, juce::String id)
    : eventsDir (std::move (directory)), runId (std::move (id))
{
    nextId = largestEventId (eventsDir) + 1;
}

juce::Result EventWriter::emit (const juce::String& type, const juce::var& payload,
                                const juce::String& commandId)
{
    std::lock_guard<std::mutex> lock (eventWriteMutex);
    if (! eventsDir.createDirectory())
        return fail ("cannot create " + eventsDir.getFullPathName());

    const auto id = juce::jmax (nextId, largestEventId (eventsDir) + 1);
    auto event = makeObject();
    auto* object = event.getDynamicObject();
    object->setProperty ("protocolVersion", protocolVersion);
    object->setProperty ("runId", runId);
    object->setProperty ("eventId", id);
    object->setProperty ("type", type);
    object->setProperty ("occurredAt", makeOccurredAt());
    object->setProperty ("payload", payload);
    if (commandId.isNotEmpty())
        object->setProperty ("commandId", commandId);

    if (const auto result = writeJsonAtomically (eventsDir.getChildFile (makeSequenceFilename (id)), event); result.failed())
        return result;

    nextId = id + 1;
    return juce::Result::ok();
}

bool EventWriter::hasCommandCompletion (const juce::String& commandId) const
{
    juce::Array<juce::File> files;
    eventsDir.findChildFiles (files, juce::File::findFiles, false, "*");
    for (const auto& file : files)
    {
        if (! isFinalEventFilename (file.getFileName()))
            continue;

        juce::var event;
        if (juce::JSON::parse (file.loadFileAsString(), event).failed())
            continue;

        if (const auto* object = event.getDynamicObject(); object != nullptr
            && object->getProperty ("type").toString() == "command_completed"
            && object->getProperty ("commandId").toString() == commandId)
            return true;
    }

    return false;
}

CommandStateStore::CommandStateStore (juce::File directory) : stateDir (std::move (directory)) {}

juce::Result CommandStateStore::validateAll() const
{
    const auto commandsDirectory = stateDir.getChildFile ("commands");
    if (commandsDirectory.exists() && ! commandsDirectory.isDirectory())
        return fail ("invalid command state directory");

    juce::Array<juce::File> files;
    commandsDirectory.findChildFiles (files, juce::File::findFiles, false, "*.json");
    for (const auto& file : files)
    {
        juce::var value;
        if (juce::JSON::parse (file.loadFileAsString(), value).failed())
            return fail ("invalid command state");

        const auto* object = value.getDynamicObject();
        const auto commandId = object != nullptr ? object->getProperty ("commandId") : juce::var();
        if (! commandId.isString() || commandId.toString().isEmpty()
            || file != fileFor (commandId.toString()) || ! load (commandId.toString()).has_value())
            return fail ("invalid command state");
    }

    return juce::Result::ok();
}

juce::File CommandStateStore::fileFor (const juce::String& commandId) const
{
    const auto hash = juce::SHA256 (commandId.toRawUTF8(), commandId.getNumBytesAsUTF8()).toHexString();
    return stateDir.getChildFile ("commands").getChildFile (hash + ".json");
}

juce::Result CommandStateStore::save (const StoredCommand& command) const
{
    auto value = makeObject();
    auto* object = value.getDynamicObject();
    object->setProperty ("commandId", command.commandId);
    object->setProperty ("sequence", command.sequence);
    object->setProperty ("phase", command.phase == CommandPhase::processing ? "processing" : "completed");
    object->setProperty ("completionEmitted", command.completionEmitted);
    if (command.outcome.has_value())
        object->setProperty ("outcome", serializeOutcome (*command.outcome));
    return writeJsonAtomically (fileFor (command.commandId), value);
}

juce::Result CommandStateStore::markProcessing (const juce::String& commandId, juce::int64 sequence)
{
    if (commandId.isEmpty() || sequence < 0)
        return fail ("command ID and sequence are required");
    return save ({ commandId, sequence, CommandPhase::processing, std::nullopt, false });
}

juce::Result CommandStateStore::markCompleted (const juce::String& commandId, const InvocationResult& outcome)
{
    const auto existing = load (commandId);
    if (! existing.has_value())
        return fail ("missing processing state for " + commandId);

    auto completed = *existing;
    completed.phase = CommandPhase::completed;
    completed.outcome = outcome;
    return save (completed);
}

std::optional<StoredCommand> CommandStateStore::load (const juce::String& commandId) const
{
    const auto file = fileFor (commandId);
    if (! file.existsAsFile())
        return std::nullopt;

    juce::var value;
    if (juce::JSON::parse (file.loadFileAsString(), value).failed())
        return std::nullopt;

    const auto* object = value.getDynamicObject();
    if (object == nullptr || object->getProperty ("commandId").toString() != commandId)
        return std::nullopt;

    const auto sequence = object->getProperty ("sequence");
    const auto phase = object->getProperty ("phase").toString();
    const auto emitted = object->getProperty ("completionEmitted");
    if ((! sequence.isInt() && ! sequence.isInt64()) || ! emitted.isBool()
        || (phase != "processing" && phase != "completed"))
        return std::nullopt;

    StoredCommand command { commandId, static_cast<juce::int64> (sequence),
                             phase == "processing" ? CommandPhase::processing : CommandPhase::completed,
                             std::nullopt, static_cast<bool> (emitted) };
    if (command.phase == CommandPhase::completed)
    {
        command.outcome = deserializeOutcome (object->getProperty ("outcome"));
        if (! command.outcome.has_value())
            return std::nullopt;
    }
    return command;
}

bool CommandStateStore::exists (const juce::String& commandId) const
{
    return fileFor (commandId).existsAsFile();
}

juce::Result CommandStateStore::setCompletionEmitted (const juce::String& commandId)
{
    const auto existing = load (commandId);
    if (! existing.has_value())
        return fail ("missing command state for " + commandId);

    auto updated = *existing;
    updated.completionEmitted = true;
    return save (updated);
}
} // namespace inlay::emulator
