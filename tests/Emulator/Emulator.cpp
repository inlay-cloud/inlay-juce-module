#include "Emulator.h"

#include "../../modules/inlay_product_unlocking/internal/ModuleVersion.h"

#include <atomic>
#include <mutex>
#include <utility>

#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <unistd.h>
#endif

namespace inlay::emulator
{
namespace
{
juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

thread_local juce::String activeCommandId;

class ScopedCommandContext
{
public:
    explicit ScopedCommandContext (const juce::String& commandId)
        : previousCommandId (std::move (activeCommandId))
    {
        activeCommandId = commandId;
    }

    ~ScopedCommandContext()
    {
        activeCommandId = std::move (previousCommandId);
    }

private:
    juce::String previousCommandId;
};

const juce::StringArray& supportedPublicMethods()
{
    static const juce::StringArray methods {
        "getStatus", "isLocked", "getError", "getCurrentUser", "getAppUpdate", "startup",
        "startActivation", "retryUnlocking", "logout", "skipCurrentAppUpdateVersion", "openWebsite"
    };
    return methods;
}

juce::var readyPayload()
{
    auto payload = object();
    auto* value = payload.getDynamicObject();
    value->setProperty ("module", "juce");
    value->setProperty ("moduleVersion", inlay::internal::moduleVersion);
    value->setProperty ("protocolVersion", protocolVersion);
   #if JUCE_WINDOWS
    value->setProperty ("pid", static_cast<juce::int64> (_getpid()));
   #else
    value->setProperty ("pid", static_cast<juce::int64> (getpid()));
   #endif

    juce::Array<juce::var> methods;
    for (const auto& method : supportedPublicMethods())
        methods.add (method);
    value->setProperty ("supportedPublicMethods", methods);
    return payload;
}

juce::var stoppedPayload (const juce::String& reason)
{
    auto payload = object();
    payload.getDynamicObject()->setProperty ("reason", reason);
    return payload;
}

InvocationResult completed (juce::var output = object())
{
    return { "completed", std::move (output), std::nullopt };
}

InvocationResult rejected (const juce::String& code, const juce::String& message)
{
    return { "rejected", object(), SafeError { code, message } };
}

void deliverFatalOnMessageThread (std::function<void (juce::String)> callback, juce::String message)
{
    if (! callback)
        return;

    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager == nullptr || manager->isThisTheMessageThread())
    {
        callback (std::move (message));
        return;
    }

    juce::MessageManager::callAsync ([callbackToRun = std::move (callback), messageToDeliver = std::move (message)]() mutable {
        callbackToRun (std::move (messageToDeliver));
    });
}
} // namespace

class Emulator::EventBrowser final : public inlay::internal::Browser
{
public:
    EventBrowser (EventWriter& writer, std::function<void (juce::String)> fatalHandler)
        : events (writer), fatal (std::move (fatalHandler)) {}

    void openURL (juce::URL url) override
    {
        const std::lock_guard<std::mutex> lock (eventMutex);
        const auto id = nextRequestId.fetch_add (1);
        auto payload = object();
        auto* value = payload.getDynamicObject();
        value->setProperty ("requestId", juce::String::formatted ("browser-%03d", id));
        value->setProperty ("url", url.toString (true));
        if (const auto result = events.emit ("browser_open_requested", payload, activeCommandId); result.failed())
            deliverFatalOnMessageThread (fatal, result.getErrorMessage());
    }

private:
    EventWriter& events;
    std::function<void (juce::String)> fatal;
    std::mutex eventMutex;
    std::atomic<int> nextRequestId { 1 };
};

Emulator::Emulator (juce::File directory, EmulatorConfig emulatorConfig,
                    std::function<void (juce::String)> fatalHandler)
    : runDirectory (std::move (directory)), config (std::move (emulatorConfig)),
      fatal (std::move (fatalHandler)), events (runDirectory.getChildFile ("events"), config.runId),
      states (runDirectory.getChildFile ("state")),
      unlocker (new inlay::internal::UnlockerImpl (
          broadcaster, config.productId, config.publicKey, config.storageDir, config.apiUrl,
          std::make_unique<EventBrowser> (events, fatal), config.deviceIDToUse)),
      commands (new CommandProcessor (
          runDirectory, config.runId, events, states, static_cast<CommandTarget&> (*this),
          [fatalCallback = fatal] (juce::String message) {
              deliverFatalOnMessageThread (fatalCallback, std::move (message));
          }))
{
}

Emulator::~Emulator()
{
    stop ("emulator_destroyed");
}

juce::Result Emulator::start()
{
    if (started)
        return juce::Result::ok();
    if (stopped || unlocker == nullptr)
        return juce::Result::fail ("Emulator cannot be restarted");

    if (const auto result = validateWritableRunLocalDirectory (
            runDirectory, runDirectory.getChildFile ("state"), "Emulator state directory");
        result.failed())
        return result;
    if (const auto result = commands->preflight(); result.failed())
        return result;

    started = true;
    broadcaster.addChangeListener (this);
    unlocker->startup();

    if (const auto result = events.emit ("ready", readyPayload()); result.failed())
    {
        broadcaster.removeChangeListener (this);
        started = false;
        return result;
    }

    commands->start();
    return juce::Result::ok();
}

void Emulator::stop (const juce::String& reason)
{
    if (stopped)
        return;

    stopped = true;
    if (commands != nullptr)
        commands->stop();
    if (started)
        broadcaster.removeChangeListener (this);

    commands.reset();
    unlocker.reset();

    if (started)
        if (const auto result = events.emit ("stopped", stoppedPayload (reason)); result.failed())
            deliverFatalOnMessageThread (fatal, result.getErrorMessage());
}

InvocationResult Emulator::invoke (const juce::String& commandId, const juce::String& method,
                                   const juce::var& arguments)
{
    const ScopedCommandContext commandContext (commandId);
    if (unlocker == nullptr)
        return rejected ("unlocker_unavailable", "unlocker is not available");

    if (method == "getStatus")
    {
        auto output = object();
        output.getDynamicObject()->setProperty ("status", serializeStatus (unlocker->getStatus()));
        return completed (redactCommandOutput (output));
    }
    if (method == "isLocked")
    {
        auto output = object();
        output.getDynamicObject()->setProperty ("locked", unlocker->isLocked());
        return completed (redactCommandOutput (output));
    }
    if (method == "getError")
    {
        auto output = object();
        output.getDynamicObject()->setProperty ("error", unlocker->getError());
        return completed (redactCommandOutput (output));
    }
    if (method == "getCurrentUser")
    {
        auto output = object();
        output.getDynamicObject()->setProperty ("currentUser", unlocker->getCurrentUser());
        return completed (redactCommandOutput (output));
    }
    if (method == "getAppUpdate")
    {
        auto output = object();
        output.getDynamicObject()->setProperty ("appUpdate", serializeAppUpdate (unlocker->getAppUpdate()));
        return completed (redactCommandOutput (output));
    }
    if (method == "startup")
        unlocker->startup();
    else if (method == "startActivation")
        unlocker->startActivation();
    else if (method == "retryUnlocking")
        unlocker->retryUnlocking();
    else if (method == "logout")
        unlocker->logout();
    else if (method == "skipCurrentAppUpdateVersion")
        unlocker->skipCurrentAppUpdateVersion();
    else if (method == "openWebsite")
    {
        const auto* argumentObject = arguments.getDynamicObject();
        const auto url = argumentObject != nullptr ? argumentObject->getProperty ("url") : juce::var();
        if (! url.isString() || url.toString().trim().isEmpty())
            return rejected ("invalid_arguments", "openWebsite requires a non-empty url");
        unlocker->openWebsite (url.toString());
    }
    else
        return rejected ("unsupported_method", "method is not supported");

    return completed();
}

juce::String Emulator::redactErrorForEvent (const juce::String& error)
{
    return redactProtocolText (error);
}

juce::var Emulator::redactCommandOutput (juce::var output)
{
    return redactProtocolValue (std::move (output));
}

void Emulator::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source != &broadcaster || stopped || unlocker == nullptr)
        return;

    const auto state = unlocker->getSnapshot();
    auto snapshot = object();
    auto* value = snapshot.getDynamicObject();
    value->setProperty ("status", serializeStatus (state.status));
    value->setProperty ("locked", state.locked);
    value->setProperty ("currentUser", state.currentUser);
    value->setProperty ("appUpdate", serializeAppUpdate (state.appUpdate));
    const auto error = redactErrorForEvent (state.error);
    value->setProperty ("error", error);

    if (const auto result = events.emit ("unlocker_changed", snapshot); result.failed())
    {
        deliverFatalOnMessageThread (fatal, result.getErrorMessage());
        return;
    }

    if (error.isNotEmpty())
        if (const auto result = events.emit ("unlocker_error", makeUnlockerErrorPayload (error)); result.failed())
            deliverFatalOnMessageThread (fatal, result.getErrorMessage());
}
} // namespace inlay::emulator
