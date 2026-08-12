#include "Protocol.h"

namespace inlay::emulator
{
namespace
{
juce::var makeConfigJson (const juce::File& storageDir)
{
    auto unlocker = juce::DynamicObject::Ptr (new juce::DynamicObject());
    unlocker->setProperty ("apiUrl", "https://api.example.test");
    unlocker->setProperty ("productId", "product-001");
    unlocker->setProperty ("publicKey", "public-key-value");
    unlocker->setProperty ("storageDir", storageDir.getFullPathName());
    unlocker->setProperty ("deviceIDToUse", "test-device-id");

    auto config = juce::DynamicObject::Ptr (new juce::DynamicObject());
    config->setProperty ("protocolVersion", protocolVersion);
    config->setProperty ("runId", "run-001");
    config->setProperty ("module", "juce");
    config->setProperty ("unlocker", juce::var (unlocker.get()));
    return juce::var (config.get());
}

juce::File createRunDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("inlay-emulator-protocol-" + juce::Uuid().toString());
    directory.createDirectory();
    return directory;
}

void writeConfig (const juce::File& runDirectory, const juce::var& config)
{
    runDirectory.getChildFile ("config.json").replaceWithText (juce::JSON::toString (config));
}

juce::DynamicObject* objectFor (const juce::var& value)
{
    return value.getDynamicObject();
}
} // namespace

class ProtocolTests final : public juce::UnitTest
{
public:
    ProtocolTests() : juce::UnitTest ("Emulator Protocol", "inlay_product_unlocking") {}

    void runTest() override
    {
        runConfigTests();
        runSerializationTests();
    }

private:
    void runConfigTests()
    {
        beginTest ("loads a valid version-one JUCE config and creates storage");
        {
            const auto runDirectory = createRunDirectory();
            const auto storageDir = runDirectory.getChildFile ("state");
            writeConfig (runDirectory, makeConfigJson (storageDir));

            EmulatorConfig config;
            expect (loadConfig (runDirectory, config).wasOk());
            expectEquals (config.runId, juce::String ("run-001"));
            expectEquals (config.apiUrl, juce::String ("https://api.example.test"));
            expectEquals (config.productId, juce::String ("product-001"));
            expectEquals (config.publicKey, juce::String ("public-key-value"));
            expect (config.storageDir == storageDir);
            expectEquals (config.deviceIDToUse, juce::String ("test-device-id"));
            expect (storageDir.isDirectory());
            runDirectory.deleteRecursively();
        }

        beginTest ("treats missing and empty device ID overrides as unset");
        {
            const auto runDirectory = createRunDirectory();
            EmulatorConfig loaded;
            auto config = makeConfigJson (runDirectory.getChildFile ("state"));
            objectFor (objectFor (config)->getProperty ("unlocker"))->removeProperty ("deviceIDToUse");
            writeConfig (runDirectory, config);
            expect (loadConfig (runDirectory, loaded).wasOk());
            expect (loaded.deviceIDToUse.isEmpty());

            objectFor (objectFor (config)->getProperty ("unlocker"))->setProperty ("deviceIDToUse", "");
            writeConfig (runDirectory, config);
            expect (loadConfig (runDirectory, loaded).wasOk());
            expect (loaded.deviceIDToUse.isEmpty());
            runDirectory.deleteRecursively();
        }

        beginTest ("rejects a non-string device ID override");
        {
            const auto runDirectory = createRunDirectory();
            auto config = makeConfigJson (runDirectory.getChildFile ("state"));
            objectFor (objectFor (config)->getProperty ("unlocker"))->setProperty ("deviceIDToUse", 42);
            writeConfig (runDirectory, config);
            EmulatorConfig loaded;
            expect (loadConfig (runDirectory, loaded).failed());
            runDirectory.deleteRecursively();
        }

        beginTest ("rejects missing and malformed config fields without exposing the key");
        {
            const auto runDirectory = createRunDirectory();
            auto config = makeConfigJson (runDirectory.getChildFile ("state"));
            objectFor (config)->setProperty ("runId", "");
            writeConfig (runDirectory, config);
            EmulatorConfig loaded;
            const auto missingResult = loadConfig (runDirectory, loaded);
            expect (missingResult.failed());

            writeConfig (runDirectory, juce::var ("not an object"));
            const auto malformedResult = loadConfig (runDirectory, loaded);
            expect (malformedResult.failed());
            expect (! malformedResult.getErrorMessage().contains ("public-key-value"));
            runDirectory.deleteRecursively();
        }

        beginTest ("rejects an absent config file and invalid JSON");
        {
            const auto runDirectory = createRunDirectory();
            EmulatorConfig loaded;
            expect (loadConfig (runDirectory, loaded).failed());

            runDirectory.getChildFile ("config.json").replaceWithText ("{ not valid JSON");
            expect (loadConfig (runDirectory, loaded).failed());
            runDirectory.deleteRecursively();
        }

        beginTest ("rejects non-string and missing unlocker settings");
        {
            const auto runDirectory = createRunDirectory();
            EmulatorConfig loaded;

            auto config = makeConfigJson (runDirectory.getChildFile ("state"));
            objectFor (objectFor (config)->getProperty ("unlocker"))->setProperty ("apiUrl", 42);
            writeConfig (runDirectory, config);
            expect (loadConfig (runDirectory, loaded).failed());

            for (const auto& name : juce::StringArray { "apiUrl", "productId", "publicKey", "storageDir" })
            {
                config = makeConfigJson (runDirectory.getChildFile ("state"));
                objectFor (objectFor (config)->getProperty ("unlocker"))->removeProperty (name);
                writeConfig (runDirectory, config);
                expect (loadConfig (runDirectory, loaded).failed(), "missing " + name);
            }

            runDirectory.deleteRecursively();
        }

        beginTest ("rejects an unsupported version or module");
        {
            const auto runDirectory = createRunDirectory();
            auto config = makeConfigJson (runDirectory.getChildFile ("state"));
            objectFor (config)->setProperty ("protocolVersion", protocolVersion + 1);
            writeConfig (runDirectory, config);
            EmulatorConfig loaded;
            expect (loadConfig (runDirectory, loaded).failed());

            config = makeConfigJson (runDirectory.getChildFile ("state"));
            objectFor (config)->setProperty ("protocolVersion", static_cast<juce::int64> (4294967297LL));
            writeConfig (runDirectory, config);
            expect (loadConfig (runDirectory, loaded).failed());

            config = makeConfigJson (runDirectory.getChildFile ("state"));
            objectFor (config)->setProperty ("module", "hise");
            writeConfig (runDirectory, config);
            expect (loadConfig (runDirectory, loaded).failed());
            runDirectory.deleteRecursively();
        }

        beginTest ("rejects relative storage and a storage path that is a file");
        {
            const auto runDirectory = createRunDirectory();
            auto config = makeConfigJson (runDirectory.getChildFile ("state"));
            objectFor (objectFor (config)->getProperty ("unlocker"))->setProperty ("storageDir", "relative-state");
            writeConfig (runDirectory, config);
            EmulatorConfig loaded;
            expect (loadConfig (runDirectory, loaded).failed());

            const auto storageFile = runDirectory.getChildFile ("not-a-directory");
            storageFile.replaceWithText ("file");
            config = makeConfigJson (storageFile);
            writeConfig (runDirectory, config);
            expect (loadConfig (runDirectory, loaded).failed());
            runDirectory.deleteRecursively();
        }

        beginTest ("rejects storage outside the run directory without creating it");
        {
            const auto runDirectory = createRunDirectory();
            const auto outsideStorage = runDirectory.getSiblingFile (
                runDirectory.getFileName() + "-outside-storage");
            expect (! outsideStorage.exists());
            writeConfig (runDirectory, makeConfigJson (outsideStorage));

            EmulatorConfig loaded;
            expect (loadConfig (runDirectory, loaded).failed());
            expect (! outsideStorage.exists());

            outsideStorage.deleteRecursively();
            runDirectory.deleteRecursively();
        }

       #if ! JUCE_WINDOWS
        beginTest ("rejects run-local storage symlinks that resolve outside the run directory");
        {
            const auto runDirectory = createRunDirectory();
            const auto outsideStorage = runDirectory.getSiblingFile (
                runDirectory.getFileName() + "-symlink-storage");
            const auto storageLink = runDirectory.getChildFile ("linked-storage");
            expect (outsideStorage.createDirectory());
            expect (outsideStorage.createSymbolicLink (storageLink, true));
            writeConfig (runDirectory, makeConfigJson (storageLink));

            EmulatorConfig loaded;
            expect (loadConfig (runDirectory, loaded).failed());

            storageLink.deleteFile();
            outsideStorage.deleteRecursively();
            runDirectory.deleteRecursively();
        }
       #endif

        beginTest ("rejects a non-writable storage directory");
        {
            const auto runDirectory = createRunDirectory();
            const auto storageDir = runDirectory.getChildFile ("read-only-state");
            expect (storageDir.createDirectory());
            expect (storageDir.setReadOnly (true));
            writeConfig (runDirectory, makeConfigJson (storageDir));

            EmulatorConfig loaded;
            expect (loadConfig (runDirectory, loaded).failed());
            storageDir.setReadOnly (false);
            runDirectory.deleteRecursively();
        }
    }

    void runSerializationTests()
    {
        beginTest ("serializes every unlocker status");
        expectEquals (serializeStatus (inlay::Unlocker::Status::undefined).toString(), juce::String ("undefined"));
        expectEquals (serializeStatus (inlay::Unlocker::Status::activationRequired).toString(), juce::String ("activation_required"));
        expectEquals (serializeStatus (inlay::Unlocker::Status::unlocking).toString(), juce::String ("unlocking"));
        expectEquals (serializeStatus (inlay::Unlocker::Status::unlocked).toString(), juce::String ("unlocked"));

        beginTest ("serializes absent and present app updates");
        const auto absentUpdate = serializeAppUpdate (std::nullopt);
        expect (absentUpdate.isVoid());
        expectEquals (juce::JSON::toString (absentUpdate), juce::String ("null"));
        inlay::Unlocker::AppUpdate update { "2.0.0", "https://example.test/update" };
        const auto updateValue = serializeAppUpdate (update);
        expectEquals (objectFor (updateValue)->getProperty ("version").toString(), juce::String ("2.0.0"));
        expectEquals (objectFor (updateValue)->getProperty ("url").toString(), juce::String ("https://example.test/update"));

        beginTest ("uses the exact safe unlocker error payload");
        const auto payload = makeUnlockerErrorPayload ("Could not unlock");
        expectEquals (objectFor (payload)->getProperty ("scope").toString(), juce::String ("unlocker"));
        expectEquals (objectFor (payload)->getProperty ("code").toString(), juce::String ("unlocker_error"));
        expectEquals (objectFor (payload)->getProperty ("message").toString(), juce::String ("Could not unlock"));

        beginTest ("makes a UTC ISO-8601 timestamp and a twelve-digit sequence filename");
        const auto occurredAt = makeOccurredAt();
        expectEquals (occurredAt.length(), 20);
        expectEquals (occurredAt[4], juce::juce_wchar ('-'));
        expectEquals (occurredAt[7], juce::juce_wchar ('-'));
        expectEquals (occurredAt[10], juce::juce_wchar ('T'));
        expectEquals (occurredAt[19], juce::juce_wchar ('Z'));
        expectEquals (makeSequenceFilename (4), juce::String ("000000000004.json"));
    }
};

static ProtocolTests protocolTests;
} // namespace inlay::emulator
