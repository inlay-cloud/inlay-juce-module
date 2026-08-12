#include "CommandProcessor.h"

#include <stdexcept>

namespace inlay::emulator
{
namespace
{
juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

InvocationResult rejected (const juce::String& code, const juce::String& message)
{
    return { "rejected", makeObject(), SafeError { code, message } };
}

InvocationResult failed (const juce::String& code, const juce::String& message)
{
    return { "failed", makeObject(), SafeError { code, message } };
}

juce::var completionPayload (const InvocationResult& result)
{
    auto value = makeObject();
    auto* object = value.getDynamicObject();
    object->setProperty ("outcome", result.outcome);
    object->setProperty ("output", result.output.getDynamicObject() != nullptr || ! result.output.isVoid() ? result.output : makeObject());
    if (result.error.has_value())
    {
        auto error = makeObject();
        error.getDynamicObject()->setProperty ("code", result.error->code);
        error.getDynamicObject()->setProperty ("message", result.error->message);
        object->setProperty ("error", error);
    }
    return value;
}

bool isSupportedMethod (const juce::String& method)
{
    static const juce::StringArray methods {
        "getStatus", "isLocked", "getError", "getCurrentUser", "getAppUpdate", "startup",
        "startActivation", "retryUnlocking", "logout", "skipCurrentAppUpdateVersion", "openWebsite"
    };
    return methods.contains (method);
}

std::optional<InvocationResult> validate (const juce::var& value, const juce::String& runId,
                                          juce::int64 filenameSequence)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        return rejected ("invalid_command", "command envelope must be an object");

    const auto commandId = object->getProperty ("commandId");
    if (! commandId.isString() || commandId.toString().trim().isEmpty())
        return rejected ("invalid_command_id", "command ID is required");
    const auto version = object->getProperty ("protocolVersion");
    if ((! version.isInt() && ! version.isInt64()) || static_cast<juce::int64> (version) != protocolVersion)
        return rejected ("invalid_protocol_version", "unsupported protocol version");
    if (! object->getProperty ("runId").isString() || object->getProperty ("runId").toString() != runId)
        return rejected ("invalid_run_id", "run ID does not match");
    const auto sequence = object->getProperty ("sequence");
    if ((! sequence.isInt() && ! sequence.isInt64()) || static_cast<juce::int64> (sequence) != filenameSequence)
        return rejected ("invalid_sequence", "sequence does not match filename");
    if (! object->getProperty ("target").isString() || object->getProperty ("target").toString() != "unlocker")
        return rejected ("invalid_target", "target must be unlocker");
    if (! object->getProperty ("method").isString() || ! isSupportedMethod (object->getProperty ("method").toString()))
        return rejected ("unsupported_method", "method is not supported");
    const auto arguments = object->getProperty ("arguments");
    if (arguments.getDynamicObject() == nullptr)
        return rejected ("invalid_arguments", "arguments must be an object");
    const auto method = object->getProperty ("method").toString();
    if (method == "openWebsite")
    {
        const auto url = arguments.getDynamicObject()->getProperty ("url");
        if (arguments.getDynamicObject()->getProperties().size() != 1
            || ! url.isString() || url.toString().trim().isEmpty())
            return rejected ("invalid_arguments", "openWebsite requires a non-empty url");
    }
    else if (arguments.getDynamicObject()->getProperties().size() != 0)
        return rejected ("invalid_arguments", "method does not accept arguments");
    return std::nullopt;
}

juce::String commandIdFor (const juce::var& value, juce::int64 sequence)
{
    if (const auto* object = value.getDynamicObject(); object != nullptr)
    {
        const auto commandId = object->getProperty ("commandId");
        if (commandId.isString() && commandId.toString().trim().isNotEmpty())
            return commandId.toString();
    }
    return "invalid-command-" + makeSequenceFilename (sequence).dropLastCharacters (5);
}
} // namespace

CommandProcessor::CommandProcessor (juce::File directory, juce::String id, EventWriter& writer,
                                    CommandStateStore& store, CommandTarget& commandTarget,
                                    std::function<void (juce::String)> fatalHandler)
    : runDirectory (std::move (directory)), runId (std::move (id)), events (writer), states (store),
      target (commandTarget), fatal (std::move (fatalHandler)) {}

CommandProcessor::~CommandProcessor()
{
    stop();
}

juce::Result CommandProcessor::preflight()
{
    if (stopped)
        return juce::Result::fail ("command processor is stopped");
    if (cursorLoaded)
        return juce::Result::ok();

    if (const auto result = loadCursor(); result.failed())
        return result;
    if (const auto result = states.validateAll(); result.failed())
        return result;

    cursorLoaded = true;
    return juce::Result::ok();
}

void CommandProcessor::start()
{
    if (stopped)
        return;
    processPendingNow();
    if (! stopped)
        startTimer (50);
}

void CommandProcessor::stop()
{
    stopped = true;
    stopTimer();
}

void CommandProcessor::timerCallback()
{
    processPendingNow();
}

void CommandProcessor::reportFatal (const juce::String& message)
{
    if (stopped)
        return;
    stopped = true;
    stopTimer();
    if (fatal)
        fatal (message);
}

juce::Result CommandProcessor::loadCursor()
{
    const auto cursor = runDirectory.getChildFile ("state").getChildFile ("cursor.json");
    if (cursor.exists() && ! cursor.existsAsFile())
        return juce::Result::fail ("invalid command cursor");
    if (! cursor.existsAsFile())
        return juce::Result::ok();

    juce::var value;
    if (const auto result = juce::JSON::parse (cursor.loadFileAsString(), value); result.failed())
        return juce::Result::fail ("invalid command cursor");
    const auto* object = value.getDynamicObject();
    const auto sequence = object != nullptr ? object->getProperty ("nextSequence") : juce::var();
    if ((! sequence.isInt() && ! sequence.isInt64()) || static_cast<juce::int64> (sequence) < 1)
        return juce::Result::fail ("invalid command cursor");
    nextSequence = static_cast<juce::int64> (sequence);
    return juce::Result::ok();
}

juce::Result CommandProcessor::saveCursor (juce::int64 sequence) const
{
    auto value = makeObject();
    value.getDynamicObject()->setProperty ("nextSequence", sequence);
    return writeJsonAtomically (runDirectory.getChildFile ("state").getChildFile ("cursor.json"), value);
}

void CommandProcessor::processPendingNow()
{
    if (stopped)
        return;
    if (! cursorLoaded)
    {
        if (const auto result = preflight(); result.failed())
        {
            reportFatal (result.getErrorMessage());
            return;
        }
    }

    while (! stopped && runDirectory.getChildFile ("commands").getChildFile (makeSequenceFilename (nextSequence)).existsAsFile())
        if (! processSequence (nextSequence))
            return;
}

bool CommandProcessor::processSequence (juce::int64 sequence)
{
    const auto file = runDirectory.getChildFile ("commands").getChildFile (makeSequenceFilename (sequence));
    juce::var envelope;
    const auto parsed = juce::JSON::parse (file.loadFileAsString(), envelope);
    const auto commandId = commandIdFor (envelope, sequence);
    InvocationResult result;
    const auto validation = parsed.failed()
        ? std::optional<InvocationResult> (rejected ("malformed_command", "command file is not valid JSON"))
        : validate (envelope, runId, sequence);

    const auto stored = states.load (commandId);
    if (! stored.has_value() && states.exists (commandId))
    {
        reportFatal ("invalid command state");
        return false;
    }
    if (stored.has_value() && stored->sequence != sequence)
    {
        reportFatal ("command state sequence mismatch");
        return false;
    }
    const auto completionEmitted = stored.has_value() && stored->completionEmitted;

    if (stored.has_value() && stored->phase == CommandPhase::processing)
    {
        result = redactInvocationResult (failed ("interrupted_before_completion", "command was interrupted before completion"));
        if (const auto write = states.markCompleted (commandId, result); write.failed())
        {
            reportFatal (write.getErrorMessage());
            return false;
        }
    }
    else if (stored.has_value() && stored->phase == CommandPhase::completed)
    {
        result = redactInvocationResult (*stored->outcome);
        if (! completionEmitted)
            if (const auto write = states.markCompleted (commandId, result); write.failed())
            {
                reportFatal (write.getErrorMessage());
                return false;
            }
    }
    else
    {
        if (const auto write = states.markProcessing (commandId, sequence); write.failed())
        {
            reportFatal (write.getErrorMessage());
            return false;
        }
        if (validation.has_value())
            result = *validation;
        else
        {
            try
            {
                const auto* object = envelope.getDynamicObject();
                result = target.invoke (commandId, object->getProperty ("method").toString(),
                                        object->getProperty ("arguments"));
                if (result.outcome.isEmpty())
                    result.outcome = "completed";
                if (result.output.isVoid())
                    result.output = makeObject();
            }
            catch (const std::exception& error)
            {
                juce::ignoreUnused (error);
                result = failed ("invocation_failed", "command invocation failed");
            }
            catch (...)
            {
                result = failed ("invocation_failed", "command target threw an unknown exception");
            }
        }
        result = redactInvocationResult (std::move (result));
        if (const auto write = states.markCompleted (commandId, result); write.failed())
        {
            reportFatal (write.getErrorMessage());
            return false;
        }
    }

    if (! completionEmitted && ! events.hasCommandCompletion (commandId))
        if (const auto write = events.emit ("command_completed", completionPayload (result), commandId); write.failed())
        {
            reportFatal (write.getErrorMessage());
            return false;
        }
    if (const auto write = states.setCompletionEmitted (commandId); write.failed())
    {
        reportFatal (write.getErrorMessage());
        return false;
    }
    if (const auto write = saveCursor (sequence + 1); write.failed())
    {
        reportFatal (write.getErrorMessage());
        return false;
    }
    nextSequence = sequence + 1;
    return true;
}
} // namespace inlay::emulator
