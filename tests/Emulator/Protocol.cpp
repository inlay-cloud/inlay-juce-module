#include "Protocol.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <regex>

namespace inlay::emulator
{
namespace
{
juce::Result failure (const juce::String& message)
{
    return juce::Result::fail ("Invalid emulator config: " + message);
}

std::optional<juce::String> requiredString (const juce::DynamicObject& object, const juce::Identifier& name)
{
    const auto value = object.getProperty (name);
    if (! value.isString())
        return std::nullopt;

    const auto string = value.toString().trim();
    return string.isNotEmpty() ? std::optional<juce::String> (string) : std::nullopt;
}

juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

std::optional<juce::File> resolvedPath (const juce::File& file)
{
    std::error_code error;
    const auto path = std::filesystem::weakly_canonical (
        std::filesystem::u8path (file.getFullPathName().toRawUTF8()), error);
    if (error)
        return std::nullopt;

    const auto text = path.u8string();
    return juce::File (juce::String::fromUTF8 (text.data(), static_cast<int> (text.size())));
}
} // namespace

juce::Result validateWritableRunLocalDirectory (const juce::File& runDirectory,
                                                 const juce::File& directory,
                                                 const juce::String& name)
{
    const auto resolvedRunDirectory = resolvedPath (runDirectory);
    const auto resolvedDirectory = resolvedPath (directory);
    if (! resolvedRunDirectory.has_value() || ! resolvedDirectory.has_value()
        || ! resolvedDirectory->isAChildOf (*resolvedRunDirectory))
        return juce::Result::fail (name + " must resolve inside the run directory");

    if (directory.exists() && ! directory.isDirectory())
        return juce::Result::fail (name + " must be a directory");
    if (! directory.createDirectory())
        return juce::Result::fail (name + " cannot be created");

    const auto createdDirectory = resolvedPath (directory);
    if (! createdDirectory.has_value() || ! createdDirectory->isAChildOf (*resolvedRunDirectory))
        return juce::Result::fail (name + " must resolve inside the run directory");

    const auto probe = directory.getChildFile (".inlay-emulator-write-probe-" + juce::Uuid().toString());
    if (! probe.create())
        return juce::Result::fail (name + " is not writable");
    if (! probe.deleteFile())
        return juce::Result::fail (name + " write probe cannot be removed");

    return juce::Result::ok();
}

juce::Result loadConfig (const juce::File& runDirectory, EmulatorConfig& config)
{
    const auto configFile = runDirectory.getChildFile ("config.json");
    if (! configFile.existsAsFile())
        return failure ("config.json is missing");

    juce::var parsed;
    if (const auto parseResult = juce::JSON::parse (configFile.loadFileAsString(), parsed); parseResult.failed())
        return failure ("config.json is malformed");

    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return failure ("config.json must be an object");

    const auto version = root->getProperty ("protocolVersion");
    if ((! version.isInt() && ! version.isInt64())
        || static_cast<juce::int64> (version) != static_cast<juce::int64> (protocolVersion))
        return failure ("unsupported protocol version");

    const auto runId = requiredString (*root, "runId");
    if (! runId.has_value())
        return failure ("runId is required");

    const auto module = requiredString (*root, "module");
    if (! module.has_value() || *module != "juce")
        return failure ("module must be juce");

    const auto* unlocker = root->getProperty ("unlocker").getDynamicObject();
    if (unlocker == nullptr)
        return failure ("unlocker must be an object");

    const auto apiUrl = requiredString (*unlocker, "apiUrl");
    const auto productId = requiredString (*unlocker, "productId");
    const auto publicKey = requiredString (*unlocker, "publicKey");
    const auto storagePath = requiredString (*unlocker, "storageDir");
    if (! apiUrl.has_value() || ! productId.has_value() || ! publicKey.has_value() || ! storagePath.has_value())
        return failure ("unlocker settings are required");

    const auto deviceIDToUseValue = unlocker->getProperty ("deviceIDToUse");
    if (! deviceIDToUseValue.isVoid() && ! deviceIDToUseValue.isString())
        return failure ("deviceIDToUse must be a string");

    const auto deviceIDToUse = deviceIDToUseValue.toString();

    if (! juce::File::isAbsolutePath (*storagePath))
        return failure ("storageDir must be absolute");

    const juce::File storageDir (*storagePath);
    if (const auto result = validateWritableRunLocalDirectory (runDirectory, storageDir, "storageDir");
        result.failed())
        return failure (result.getErrorMessage());

    config = { *runId, *apiUrl, *productId, *publicKey, storageDir, deviceIDToUse };
    return juce::Result::ok();
}

juce::var serializeStatus (inlay::Unlocker::Status status)
{
    switch (status)
    {
        case inlay::Unlocker::Status::undefined:          return "undefined";
        case inlay::Unlocker::Status::activationRequired: return "activation_required";
        case inlay::Unlocker::Status::unlocking:          return "unlocking";
        case inlay::Unlocker::Status::unlocked:           return "unlocked";
    }

    jassertfalse;
    return {};
}

juce::var serializeAppUpdate (const std::optional<inlay::Unlocker::AppUpdate>& update)
{
    if (! update.has_value())
        return {};

    auto result = makeObject();
    auto* object = result.getDynamicObject();
    object->setProperty ("version", update->version);
    object->setProperty ("url", update->url);
    return result;
}

juce::var makeUnlockerErrorPayload (const juce::String& message)
{
    auto result = makeObject();
    auto* object = result.getDynamicObject();
    object->setProperty ("scope", "unlocker");
    object->setProperty ("code", "unlocker_error");
    object->setProperty ("message", message);
    return result;
}

juce::String redactProtocolText (const juce::String& text)
{
    static const std::regex credentials (
        R"(((?:access[_-]?token|id[_-]?token|authorization|private[_ -]?key)["']?\s*[=:]\s*)(?:["'][^"']*["']|(?:bearer\s+)?[^\s,}\]]+))",
        std::regex::icase);
    return juce::String (std::regex_replace (text.toStdString(), credentials, "$1[REDACTED]"));
}

juce::var redactProtocolValue (juce::var value)
{
    if (value.isString())
        return redactProtocolText (value.toString());

    if (auto* array = value.getArray(); array != nullptr)
        for (int index = 0; index < array->size(); ++index)
            array->set (index, redactProtocolValue (array->getReference (index)));

    if (auto* object = value.getDynamicObject(); object != nullptr)
    {
        const auto properties = object->getProperties();
        for (const auto& property : properties)
            object->setProperty (property.name, redactProtocolValue (property.value));
    }

    return value;
}

InvocationResult redactInvocationResult (InvocationResult result)
{
    result.output = redactProtocolValue (std::move (result.output));
    if (result.error.has_value())
        result.error->message = redactProtocolText (result.error->message);
    return result;
}

juce::String makeOccurredAt()
{
    const auto now = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
    std::tm utc {};
   #if JUCE_WINDOWS
    gmtime_s (&utc, &now);
   #else
    gmtime_r (&now, &utc);
   #endif
    char buffer[32] {};
    std::strftime (buffer, sizeof (buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

juce::String makeSequenceFilename (juce::int64 sequence)
{
    return juce::String::formatted ("%012lld.json", static_cast<long long> (sequence));
}
} // namespace inlay::emulator
