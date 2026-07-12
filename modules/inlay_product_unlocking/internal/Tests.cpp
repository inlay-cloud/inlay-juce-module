#include "../Unlocker.h"
#include "../DefaultUI.h"
#include "UnlockerImpl.h"
#include "TokenValidator.h"

#include <array>
#include <chrono>
#include <future>
#include <utility>

namespace inlay::internal {
    namespace {
        constexpr std::array<juce::uint8, 19> testSha256DigestInfoPrefix{
            0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
            0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
        };

        juce::MemoryBlock toTestLittleEndian(const juce::MemoryBlock &bigEndian) {
            juce::MemoryBlock littleEndian(bigEndian.getSize());
            const auto *source = static_cast<const juce::uint8 *>(bigEndian.getData());
            auto *dest = static_cast<juce::uint8 *>(littleEndian.getData());

            for (size_t i = 0; i < bigEndian.getSize(); ++i)
                dest[i] = source[bigEndian.getSize() - 1 - i];

            return littleEndian;
        }

        juce::MemoryBlock toTestBigEndianPadded(const juce::BigInteger &value, size_t sizeInBytes) {
            const auto littleEndian = value.toMemoryBlock();

            if (littleEndian.getSize() > sizeInBytes)
                return {};

            juce::MemoryBlock bigEndian(sizeInBytes, true);
            const auto *source = static_cast<const juce::uint8 *>(littleEndian.getData());
            auto *dest = static_cast<juce::uint8 *>(bigEndian.getData());
            const auto padding = sizeInBytes - littleEndian.getSize();

            for (size_t i = 0; i < littleEndian.getSize(); ++i)
                dest[padding + i] = source[littleEndian.getSize() - 1 - i];

            return bigEndian;
        }

        size_t getTestRSAKeySizeInBytes(const juce::String &publicKey) {
            juce::BigInteger modulus;
            modulus.parseString(publicKey.fromFirstOccurrenceOf(",", false, false), 16);

            return static_cast<size_t>((modulus.getHighestBit() + 8) >> 3);
        }

        juce::MemoryBlock makeRSASHA256Signature(const juce::String &payload,
                                                 const juce::RSAKey &privateKey,
                                                 size_t keySizeInBytes) {
            const auto hash = juce::SHA256(payload.toRawUTF8(), payload.getNumBytesAsUTF8()).getRawData();

            juce::MemoryBlock digestInfo;
            digestInfo.append(testSha256DigestInfoPrefix.data(), testSha256DigestInfoPrefix.size());
            digestInfo.append(hash.getData(), hash.getSize());

            juce::MemoryBlock encodedMessage(keySizeInBytes, true);
            auto *bytes = static_cast<juce::uint8 *>(encodedMessage.getData());
            const auto paddingSize = keySizeInBytes - digestInfo.getSize() - 3;

            bytes[0] = 0x00;
            bytes[1] = 0x01;
            for (size_t i = 0; i < paddingSize; ++i)
                bytes[2 + i] = 0xff;

            bytes[2 + paddingSize] = 0x00;
            encodedMessage.copyFrom(digestInfo.getData(), static_cast<int>(3 + paddingSize), digestInfo.getSize());

            juce::BigInteger signatureValue;
            signatureValue.loadFromMemoryBlock(toTestLittleEndian(encodedMessage));
            privateKey.applyToValue(signatureValue);

            return toTestBigEndianPadded(signatureValue, keySizeInBytes);
        }

        juce::String makeSignedAccessToken(const juce::String &payload,
                                           const juce::RSAKey &privateKey,
                                           const juce::String &publicKey) {
            const auto signature = makeRSASHA256Signature(payload, privateKey, getTestRSAKeySizeInBytes(publicKey));
            return juce::Base64::toBase64(payload.toRawUTF8(), payload.getNumBytesAsUTF8())
                   + "." + juce::Base64::toBase64(signature.getData(), signature.getSize());
        }

        Unlocker::AppUpdate makeTestAppUpdate(juce::String version, juce::String url) {
            Unlocker::AppUpdate appUpdate;
            appUpdate.version = std::move(version);
            appUpdate.url = std::move(url);

            return appUpdate;
        }

        juce::String makeActivationEventJson(juce::int64 time, juce::String instanceID) {
            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty("time", time);
            obj->setProperty("instanceID", std::move(instanceID));
            return juce::JSON::toString(juce::var(obj.get()));
        }
    } // namespace

    class UnlockerTests final : public juce::UnitTest {
    public:
        UnlockerTests()
            : juce::UnitTest("Unlocker", "inlay_product_unlocking") {
        }

        void runTest() override {
            runParseAndVerifyAccessTokenPayloadTests();
            runValidateAccessTokenTests();
            runValidateAccessTokenClaimsTests();
            runStatusModelTests();
            runStartupStateTests();
            runActivationRetryLogoutStateTests();
            runActivationEventTests();
            runAppUpdateTests();
            runDefaultUITests();
        }

    private:
        void runActivationEventTests() {
            beginTest("Unlocker Activation Event: parses valid event");
            {
                const auto event = UnlockerImpl::parseActivationEventFromText(
                        makeActivationEventJson(1234, "other-instance"));

                expect(event.has_value());
                if (event.has_value()) {
                    expectEquals(event->time, static_cast<juce::int64>(1234));
                    expectEquals(event->instanceID, juce::String("other-instance"));
                }
            }

            beginTest("Unlocker Activation Event: rejects missing fields");
            {
                expect(!UnlockerImpl::parseActivationEventFromText(R"({"time":1234})").has_value());
                expect(!UnlockerImpl::parseActivationEventFromText(R"({"instanceID":"other-instance"})").has_value());
                expect(!UnlockerImpl::parseActivationEventFromText({}).has_value());
            }

            beginTest("Unlocker Activation Event: watcher starts only while activation is required");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});

                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.status = Unlocker::Status::activationRequired;
                }
                unlocker.updateActivationEventWatcher();
                expect(unlocker.isTimerRunning());

                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.status = Unlocker::Status::unlocking;
                }
                unlocker.updateActivationEventWatcher();
                expect(!unlocker.isTimerRunning());
            }

            beginTest("Unlocker Activation Event: writes current instance event");
            {
                const auto inlayDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("inlay-activation-event-write-test")
                        .getChildFile(juce::Uuid().toString());

                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {}, inlayDir);
                expect(inlayDir.createDirectory());

                unlocker.triggerActivationEvent();

                const auto event = UnlockerImpl::parseActivationEventFromText(
                        unlocker._activationEventFile.loadFileAsString());
                expect(event.has_value());
                if (event.has_value()) {
                    expectEquals(event->instanceID, unlocker._instanceID);
                    expect(juce::Time::currentTimeMillis() - event->time < 5000);
                }

                inlayDir.deleteRecursively();
            }

            beginTest("Unlocker Activation Event: ignores self event");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});

                UnlockerImpl::ActivationEvent event;
                event.time = juce::Time::currentTimeMillis();
                event.instanceID = unlocker._instanceID;
                expect(!unlocker.shouldReactToActivationEvent(event, event.time));
            }

            beginTest("Unlocker Activation Event: ignores stale event");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});

                const auto now = juce::Time::currentTimeMillis();
                UnlockerImpl::ActivationEvent event;
                event.time = now - 6000;
                event.instanceID = "other-instance";
                expect(!unlocker.shouldReactToActivationEvent(event, now));
            }

            beginTest("Unlocker Activation Event: queues startup for fresh event from another instance");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});

                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.status = Unlocker::Status::activationRequired;
                    unlocker._state.errorMessage = "Waiting for activation";
                }

                expect(unlocker.prepareStartupFromActivationEvent());

                expect(unlocker._state.status == Unlocker::Status::unlocking);
                expect(unlocker._state.errorMessage.isEmpty());
                expect(unlocker._state.pendingTask == UnlockerImpl::WorkerTask::startup);
            }
        }

        void runDefaultUITests() {
            beginTest("DefaultUI: is opaque");
            {
                Unlocker unlocker("test-product-id", {});
                DefaultUI defaultUI(unlocker);

                expect(defaultUI.isOpaque());
            }

            beginTest("DefaultUI: maps update dialog buttons to the intended actions");
            {
                expect(internal::getAppUpdateDialogActionForNativeMessageBoxResult(0)
                       == internal::AppUpdateDialogAction::later);
                expect(internal::getAppUpdateDialogActionForNativeMessageBoxResult(1)
                       == internal::AppUpdateDialogAction::skip);
                expect(internal::getAppUpdateDialogActionForNativeMessageBoxResult(2)
                       == internal::AppUpdateDialogAction::openUpdate);
            }

            beginTest("DefaultUI: update dialog provides a close-only Later action");
            {
                const auto options = internal::createAppUpdateDialogOptions(
                    makeTestAppUpdate("1.2.3", "https://example.com/app.zip"),
                    nullptr);

                expectEquals(options.getNumButtons(), 3);
                expectEquals(options.getButtonText(0), juce::String("Later"));
                expectEquals(options.getButtonText(1), juce::String("Skip"));
                expectEquals(options.getButtonText(2), juce::String("Go to Update"));
            }
        }

        void runActivationRetryLogoutStateTests() {
            beginTest("Unlocker Activation: queues start activation from activation required");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});
                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.status = Unlocker::Status::activationRequired;
                    unlocker._state.errorMessage = "Previous error";
                    unlocker._state.currentTask = UnlockerImpl::WorkerTask::startup;
                }

                expect(unlocker.prepareStartActivation());
                expect(unlocker._state.status == Unlocker::Status::activationRequired);
                expect(unlocker._state.errorMessage.isEmpty());
                expect(unlocker._state.pendingTask == UnlockerImpl::WorkerTask::startActivation);
                expect(unlocker._state.breakCurrentTask);
            }

            beginTest("Unlocker Retry: ignores states without unlocking error");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});

                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.status = Unlocker::Status::activationRequired;
                    unlocker._state.errorMessage = "Activation error";
                    unlocker._state.pendingTask = UnlockerImpl::WorkerTask::none;
                }
                expect(!unlocker.prepareRetryUnlocking());
                expect(unlocker._state.pendingTask == UnlockerImpl::WorkerTask::none);
                expectEquals(unlocker._state.errorMessage, juce::String("Activation error"));

                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.status = Unlocker::Status::unlocking;
                    unlocker._state.errorMessage.clear();
                    unlocker._state.pendingTask = UnlockerImpl::WorkerTask::none;
                }
                expect(!unlocker.prepareRetryUnlocking());
                expect(unlocker._state.pendingTask == UnlockerImpl::WorkerTask::none);
                expect(unlocker._state.errorMessage.isEmpty());
            }

            beginTest("Unlocker Retry: clears error and queues startup while unlocking");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});
                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.status = Unlocker::Status::unlocking;
                    unlocker._state.errorMessage = "Unlock failed";
                    unlocker._state.currentTask = UnlockerImpl::WorkerTask::refreshAccessWithDelay;
                }

                expect(unlocker.prepareRetryUnlocking());
                expect(unlocker._state.status == Unlocker::Status::unlocking);
                expect(unlocker._state.errorMessage.isEmpty());
                expect(unlocker._state.pendingTask == UnlockerImpl::WorkerTask::startup);
                expect(unlocker._state.breakCurrentTask);
            }

            beginTest("Unlocker Logout: clears identity and returns to activation required");
            {
                const auto inlayDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("inlay-unlocker-logout-test")
                        .getChildFile(juce::Uuid().toString());

                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {}, inlayDir);
                expect(inlayDir.createDirectory());
                unlocker._idTokenFile.replaceWithText("id-token");
                unlocker._accessTokenFile.replaceWithText("access-token");
                unlocker._appUpdateFile.replaceWithText(R"({"version":"1.2.3","url":"https://example.com/app.zip"})");

                AccessToken token;
                token.userEmail = "user@example.com";

                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._idToken = "id-token";
                    unlocker._state.validatedAccessToken = token;
                    unlocker._state.status = Unlocker::Status::unlocked;
                    unlocker._state.errorMessage = "Old error";
                    unlocker._state.currentTask = UnlockerImpl::WorkerTask::refreshAccessWithDelay;
                    unlocker._state.pendingTask = UnlockerImpl::WorkerTask::startup;
                }

                unlocker.applyLogoutState();

                expect(unlocker._idToken.isEmpty());
                expect(!unlocker._state.validatedAccessToken.has_value());
                expect(unlocker._idTokenFile.loadFileAsString().isEmpty());
                expect(unlocker._accessTokenFile.loadFileAsString().isEmpty());
                expect(unlocker._appUpdateFile.existsAsFile());
                expect(unlocker._state.status == Unlocker::Status::activationRequired);
                expect(unlocker._state.errorMessage.isEmpty());
                expect(unlocker._state.pendingTask == UnlockerImpl::WorkerTask::none);
                expect(unlocker._state.breakCurrentTask);

                inlayDir.deleteRecursively();
            }
        }

        void runAppUpdateTests() {
            beginTest("Api AuthResponse: parses HISE app update");
            {
                const auto response = Api::parseAuthResponse(juce::JSON::parse(
                        R"({"accessToken":"access-token","appUpdate":{"version":"1.2.3","url":"https://example.com/app.zip"}})"));

                expect(response.has_value());
                expect(response->appUpdate.has_value());
                expectEquals(response->appUpdate->version, juce::String("1.2.3"));
                expectEquals(response->appUpdate->url, juce::String("https://example.com/app.zip"));
            }

            beginTest("Api AuthResponse: ignores app update without version");
            {
                const auto response = Api::parseAuthResponse(juce::JSON::parse(
                        R"({"accessToken":"access-token","appUpdate":{"url":"https://example.com/app.zip"}})"));

                expect(response.has_value());
                expect(!response->appUpdate.has_value());
            }

            beginTest("Api AuthResponse: ignores app update without URL");
            {
                const auto response = Api::parseAuthResponse(juce::JSON::parse(
                        R"({"accessToken":"access-token","appUpdate":{"version":"1.2.3"}})"));

                expect(response.has_value());
                expect(!response->appUpdate.has_value());
            }

            beginTest("Unlocker AppUpdate: hides skipped same version");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});
                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.appUpdate = makeTestAppUpdate("1.2.3", "https://example.com/app.zip");
                    unlocker._state.skippedAppUpdateVersion = "1.2.3";
                }

                expect(!unlocker.getAppUpdate().has_value());
            }

            beginTest("Unlocker AppUpdate: shows update when skipped version differs");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});
                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.appUpdate = makeTestAppUpdate("1.2.3", "https://example.com/app.zip");
                    unlocker._state.skippedAppUpdateVersion = "1.2.2";
                }

                const auto appUpdate = unlocker.getAppUpdate();
                expect(appUpdate.has_value());
                expectEquals(appUpdate->version, juce::String("1.2.3"));
                expectEquals(appUpdate->url, juce::String("https://example.com/app.zip"));
            }
        }

        void runStatusModelTests() {
            static constexpr int publicStatusCount = 4;

            beginTest("Unlocker Status: public statuses are stable");
            {
                expect(static_cast<int>(Unlocker::Status::undefined) >= 0);
                expect(static_cast<int>(Unlocker::Status::activationRequired) >= 0);
                expect(static_cast<int>(Unlocker::Status::unlocking) >= 0);
                expect(static_cast<int>(Unlocker::Status::unlocked) >= 0);
                expectEquals(publicStatusCount, 4);
            }

            beginTest("Unlocker Status: isLocked does not wait for the state lock");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});

                unlocker.handleTaskResult(UnlockerImpl::makeTaskResult(Unlocker::Status::unlocked));

                std::future<bool> result;
                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    result = std::async(std::launch::async, [&unlocker] {
                        return unlocker.isLocked();
                    });

                    expect(result.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready);
                }

                expect(!result.get());

                unlocker.handleTaskResult(UnlockerImpl::makeTaskResult(Unlocker::Status::activationRequired));
                expect(unlocker.isLocked());
            }
        }

        void runStartupStateTests() {
            beginTest("Unlocker Startup: default state is inert until startup");
            {
                UnlockerImpl::State state;

                expect(state.status == Unlocker::Status::undefined);
                expect(state.pendingTask == UnlockerImpl::WorkerTask::none);
                expect(state.currentTask == UnlockerImpl::WorkerTask::none);
            }

            beginTest("Unlocker Startup: constructor does not start worker thread");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});

                expect(unlocker.getStatus() == Unlocker::Status::undefined);
                expect(!unlocker.isThreadRunning());
            }

            beginTest("Unlocker Startup: constructor does not create storage directory");
            {
                const auto inlayDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("inlay-unlocker-constructor-test")
                        .getChildFile(juce::Uuid().toString());

                expect(!inlayDir.exists());

                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {}, inlayDir);

                expect(!inlayDir.exists());
                expect(unlocker._inlayDir == inlayDir);
            }
        }

        void runParseAndVerifyAccessTokenPayloadTests() {
            struct TestCase {
                const char *name;
                const char *publicKey;
                const char *token;
                const char *expectedPayload;
            };

            static constexpr TestCase testCases[]{
                {
                    "fixture mutation",
                    "10001,dac1806e0ff8a533b6f9d8176c7c8f764b5b617152e6ddbd0772be130bc246bdf2521e475553edb0e206a4a958b1f003e8b7eee606257e52bb5c285626cb6461f015d4dacb84286c2824698a47e0fec6fed187288590140b9ded672b5aae8e31d96aba99e9282b744914735eb59c7119aa45849cb09dd7b42a7fd44c6867c90eaea637c30de5e6042d89f5c91b3eed77e5f47ce43cc1c9d8c612247e020390b3c6234051c4c320b69430141c714c9312e1f22d7dcc0dd1ddf348a2ca34551de0818d319f65908a47259cd0a842adcbf92861f1f76fc3fa255159cd3d729b0b46e24cfe7e212630535e14d9e6db5a921629793d5da03a9fcfb0fde51615cb5a29",
                    "eyJwIjoicHJvZF8xMjMiLCJkIjoiZGV2XzQ1NiIsImUiOjE3MTIzNDU2NzgsImkiOjE3MTIzNDEyMzR9.UGkqC+qbMOHMUEf8twAKERwugMee8RuRqEdWHkZTxH0hEhjE+AHn/czL9DTUVnNmBVACTVBD8q6GswdMhmgy8EgWXVaXSwRi21TEGu/j7iVt7aIhODlHtptX0oFtDuP+sgk5eCEJ1KnRxorz7Dvu3f+q1Lb01K072bENv0nqgKz37qLe2smxE7F6AeW+BYVZ/Yn43rYBggoOS5awlOK1IKll9v1E7l0hmbdKQKmbSZMMueNE8yxDRyHvvdn0fQ1MTPBHydHLLgE9hMl8ZZ+G6pF9DbNzZVihPuB4TnPoEu8qhpmYuxLArm1CTN/ao+tWL9UwMyW3+J97r4STDT/tZg==",
                    "{\"p\":\"prod_123\",\"d\":\"dev_456\",\"e\":1712345678,\"i\":1712341234}"
                },
                {
                    "zero mutation",
                    "10001,bfbebb6ec558760640be1ee9f63d7d0b272e210dc238ebc76299f70bf64054eaaad3c6d74e88b6a2f0ce0efe0d86e19e1d23a7a93c6eab3db8e7f3f773d0da26278a2aaa3aa4b3694f2627589709e490302015639eb7ddea49473e4bbc0b8ffa6fbca40a9899b6bad4d376ec055527c7fa7e70930a171f3c108dcaf38ae55162904ccab6ac2dee425f839919353b73682cad51fad1ff96d06e1776a681e420b6898125b3c3740d0c60bbacae51281f8738316c39ad7ae61c6949fcdac56d83233ffdce3e8633e479352d7f06f9248801faac87d5925fa946d51c707c36e7031b306685201bd5a863c49e349aefba1174609c5e4410f8a998b73834687ef92a09",
                    "eyJwIjoiTUREMEZFMkYyMSIsImQiOiJKU0tESktTSiIsImUiOjE3MTI0NDAwMDAsImkiOjE3MTI0NDExMTF9.Azfu2bcgmXkVXOplXZOnFGYI+NW6Y+jHcY7H7YtY7teIYTPy+6mkXTjEnn36d1cTNiUudXLB98H/grCGT3+rqoC2mTvXWnlff8D8SXuV/I0i6pMQMqAJiU4znSzMqL6OQtl/9jxPIYINu5KFiWhzfmjMoNILyY4b6wIdOlfjbfx/yKBzWltlveBQlug8vroUYHeAXWcTbUOthoAVvOVzXl4oeVzTDGbMzhxm1gGOY0cIN3/AtvKfGYUXjnxHtPGPdhQGZSDlyBnrh88UYkoftWiDkmcozLKZ0hFghNDzpROsyDWwziv8FHESJlJcuECK34Z52hPcmP8L6GG5SYLlbA==",
                    "{\"p\":\"MDD0FE2F21\",\"d\":\"JSKDJKSJ\",\"e\":1712440000,\"i\":1712441111}"
                },
                {
                    "slash mutation",
                    "10001,9a8ba4dfe6857336d8e9c79acb2d17cda4f7442cd8eb5d670c30cdd6c01587a4e8190fe8713c829782cffd20fb12408149fac64e3b3d177ae9876674dbbb1b5f5d51cbf700690075d05a919b15fdb0735aa517e26cf9b994347b58cd0cd5634089c1e0eafc67a40b7bf064b4b49becb67f5749908ee303d0f16c17637d5c13da9fee82de1c73e2226536e5bb99ba7f39d42d58862b22bbe471ad472c6bbd946258d35d859a539da7ec7bf6fee56c59af56ec6f0c66716f502d1db49c2ccf85c9cd8333abb5de08c866e98d20a244252db832f666cbe8040a5a6cdec30f49b5da9b3200f59089b7c0e39a1d6d8f724aca74bab2b4d750c830694c585124976919",
                    "eyJwIjoiTUREMEFBQUYyMSIsImQiOiJKU0tESktTSiIsImUiOjE3MTI0NDAwMDAsImkiOjE3MTI0NDExMTF9.DTEbmxKMMSHybTBC718/VuzZ9NzsK4D0J77KmgN8iXdbEBNvxGd5JyTKRxImbq86v+oIEWlPFvjxc2vWSxxvPRwmrp3alqavJPiyZorclvZxPF/C72JHYI30oVJ4VUXwD1ElABDLV28Hb6y3tpB0blzWcLmsBED6RNq7+DkvAUeHCRIDti+lXeHeOklhAPsZVIKF6tdkU57EDr9qOz3YfoeAAWg/K4aIYNeZjsPWgXUv+VYhrKlCR4vBXEo4qJ6Xb2zeswSGt/xsZxnhNfG3XSuBIcHAJPXw9KFh82UYz7gX70HpkUgluLmFFrDY7JkNZOLWzS3av5280/XkAiBbxg==",
                    "{\"p\":\"MDD0AAAF21\",\"d\":\"JSKDJKSJ\",\"e\":1712440000,\"i\":1712441111}"
                },
                {
                    "incrementing mutation",
                    "10001,d00939d757247c6b9eda3b3c6f917bee54d59f0851947c9ddfa7391d7811fb64d85dbb70fa8380f5c50a6cb7b9d5ac1c7da6ea4701ef74d2ca5faefc08eacf82914179961d668cd18020f2f5c7bbba369a725afd29dd42b527b771d97fdfcdf39851a4d0df267aae2d6a08ac39309b8ebab8579b93a859dd857e034333b3491c18b49a4c73e77d793d29ba85e99041e3730949084483cc8fe0658a0d9c527e6020614fa62274680a4e69515e2cc0a30e7e8a1c6f25881c01ee76fb1b4f9e74cd80e2dcda874b06dcc6f9573af30e10a7765b9a5865486a5865743fdeca606e5c30a5a7269637e11944c53053b36396bf377deabe19dfa762a8900a1d5bc5afe9",
                    "eyJwIjoiTUREMEFBQUYyMSIsImQiOiJkZXZpY2UtMiIsImUiOjE3MTI0NDAwMDAsImkiOjE3MTI0NDExMTJ9.cubmamiAFCnAV3VUyIa5hZM4jX+YyqSlY33Tihfxlzzl5Q7d4YnXUHZLZGOK8CB+vX9Ax0Wxj2WZhR97nUeyC7CuLiM6xm8IZc8YKgW6WgoYYB0ZB4sEtV+ATy+KaafsNY11bLY9A3rTVImGS1mtURaeUYNPzh3Mma6BLf69eQS7Y0TnKF9k4+X02ZostQuvsXRyIL2C5wIJv4hh5SExQAV0sLyTCI7ABuBKo4Wvn7xmDnJ/tAIQLoLWS2XXkGZpWwX3/LzYQjoaRMwFO16BVSswr+BqPuRgu8swu0yXOiPN9CdYbpmWzZADqYqPCryi2ZGiz/LdDgZC0FbnGXn23Q==",
                    "{\"p\":\"MDD0AAAF21\",\"d\":\"device-2\",\"e\":1712440000,\"i\":1712441112}"
                },
                {
                    "totally valid key",
                    "10001,d00939d757247c6b9eda3b3c6f917bee54d59f0851947c9ddfa7391d7811fb64d85dbb70fa8380f5c50a6cb7b9d5ac1c7da6ea4701ef74d2ca5faefc08eacf82914179961d668cd18020f2f5c7bbba369a725afd29dd42b527b771d97fdfcdf39851a4d0df267aae2d6a08ac39309b8ebab8579b93a859dd857e034333b3491c18b49a4c73e77d793d29ba85e99041e3730949084483cc8fe0658a0d9c527e6020614fa62274680a4e69515e2cc0a30e7e8a1c6f25881c01ee76fb1b4f9e74cd80e2dcda874b06dcc6f9573af30e10a7765b9a5865486a5865743fdeca606e5c30a5a7269637e11944c53053b36396bf377deabe19dfa762a8900a1d5bc5afe9",
                    "eyJwIjoiTUREMEFBQUYyMSIsImQiOiJkZXZpY2UtMiIsImUiOjE4NzUzMTYyMDcsImkiOjE3NzUzMTYyMDd9.Gh50f7rLh6Y2mygMO9aguCLNOcoVuJOU4ux0EipGVzVtnqoJa+AlxYA43zEXBSygT4KuG3OWsXCtyMc+HCqHicRyHw9UqqXRH+qV/8oDe8r+OMONndZk/4uWKcEzrwYvsU7mbPCOKKijP4amMj70ieEcjVfvyyuRSJ3n7eSV7ubXPFrEmrq1QF9zMuQxLewO+7ZmF0O2BXuRfS2RK/6EnOhkN8+i63zz7WN1HaDsikBGiZgZR9L9WXr4fGJdvRhlKwVze5OL5lm/J+6NJHSGY7eam6WWQJhvR+AU3a+SA7IUuW6oS5VMKlE6Vt9215FX4MAGsReca21WyMwEJAqkcA==",
                    "{\"p\":\"MDD0AAAF21\",\"d\":\"device-2\",\"e\":1875316207,\"i\":1775316207}"
                }
            };

            for (const auto &testCase: testCases) {
                beginTest(juce::String("parseAndVerifyAccessTokenPayload: ") + testCase.name);
                {
                    const auto payload = parseAndVerifyAccessTokenPayload(testCase.token, testCase.publicKey);
                    expect(payload.has_value());
                    expectEquals(*payload, juce::String(testCase.expectedPayload));
                }

                beginTest(
                    juce::String("parseAndVerifyAccessTokenPayload: rejects tampered signature for ") + testCase.name);
                {
                    const auto token = juce::String(testCase.token);
                    const auto separatorIndex = token.indexOfChar('.');
                    auto signature = token.substring(separatorIndex + 1);

                    if (signature[0] == 'A')
                        signature = "B" + signature.substring(1);
                    else
                        signature = "A" + signature.substring(1);

                    const auto tampered = token.substring(0, separatorIndex + 1) + signature;
                    expect(!parseAndVerifyAccessTokenPayload(tampered, testCase.publicKey).has_value());
                }
            }
        }

        void runValidateAccessTokenTests() {
            juce::RSAKey publicKey;
            juce::RSAKey privateKey;
            const int randomSeeds[] { 11, 29, 37, 53 };
            juce::RSAKey::createKeyPair(publicKey, privateKey, 512, randomSeeds, juce::numElementsInArray(randomSeeds));

            const auto publicKeyText = publicKey.toString();
            const auto issuedAtSeconds = juce::Time::currentTimeMillis() / 1000 - 60;
            const auto expiresAtSeconds = issuedAtSeconds + 3600;
            const auto fixtureToken = makeSignedAccessToken(
                "{\"p\":\"my-product-id\",\"d\":\"device-2\",\"e\":" + juce::String(expiresAtSeconds)
                + ",\"i\":" + juce::String(issuedAtSeconds) + ",\"u\":\"user@example.com\"}",
                privateKey,
                publicKeyText);

            TokenValidator tokenValidator("my-product-id", publicKeyText, "device-2");

            beginTest("validateAccessToken: valid signed token");
            {
                const auto token = tokenValidator.validateAccessToken(fixtureToken);
                expect(token.has_value());
                if (token.has_value()) {
                    expectEquals(token->productId, juce::String("my-product-id"));
                    expectEquals(token->deviceId, juce::String("device-2"));
                    expectEquals(token->userEmail, juce::String("user@example.com"));
                }
            }

            beginTest("validateAccessToken: rejects product id suffix match");
            {
                TokenValidator suffixTokenValidator("other-my-product-id", publicKeyText, "device-2");

                expect(!suffixTokenValidator.validateAccessToken(fixtureToken).has_value());
            }

            beginTest("validateAccessToken: rejects payload/signature mismatch");
            {
                const juce::String tamperedToken =
                        juce::Base64::toBase64("{\"p\":\"my-product-id\",\"d\":\"device-2\",\"e\":"
                                               + juce::String(expiresAtSeconds + 1)
                                               + ",\"i\":" + juce::String(issuedAtSeconds)
                                               + ",\"u\":\"user@example.com\"}")
                        + "." + fixtureToken.fromFirstOccurrenceOf(".", false, false);

                expect(!tokenValidator.validateAccessToken(tamperedToken).has_value());
            }

            beginTest("validateAccessToken: rejects invalid signature encoding");
            {
                const auto invalidSignatureToken =
                        juce::String(fixtureToken).upToFirstOccurrenceOf(".", false, false) + ".not-base64!";
                expect(!tokenValidator.validateAccessToken(invalidSignatureToken).has_value());
            }

            beginTest("validateAccessToken: rejects malformed token format");
            {
                expect(!tokenValidator.validateAccessToken("missing-separator").has_value());
                expect(!tokenValidator.validateAccessToken("a.b.c").has_value());
            }
        }

        void runValidateAccessTokenClaimsTests() {
            auto now = juce::Time::currentTimeMillis();
            auto issuedAt = now - 60 * 1000;
            auto expiresAt = now + 60 * 1000;
            juce::String deviceId = "my-device-id";

            TokenValidator tokenValidator("my-test-product-id", {}, deviceId);

            beginTest("validateAccessTokenClaims: all valid");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.deviceId = deviceId;
                token.userEmail = "user@example.com";
                token.issuedAt = issuedAt;
                token.expiresAt = expiresAt;
                expect(tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: keeps user email when present");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.deviceId = deviceId;
                token.issuedAt = issuedAt;
                token.expiresAt = expiresAt;
                token.userEmail = "user@example.com";

                expect(tokenValidator.validateAccessTokenClaims(token));
                expectEquals(token.userEmail, juce::String("user@example.com"));
            }

            beginTest("makeAccessTokenFromJSON: parses user email claim");
            {
                const auto token = tokenValidator.makeAccessTokenFromJSON(
                        R"({"p":"my-test-product-id","d":"my-device-id","e":9999999999,"i":1,"u":"user@example.com"})");

                expect(token.has_value());
                expectEquals(token->productId, juce::String("my-test-product-id"));
                expectEquals(token->deviceId, deviceId);
                expectEquals(token->userEmail, juce::String("user@example.com"));
            }

            beginTest("makeAccessTokenFromJSON: keeps user email empty when claim is absent");
            {
                const auto token = tokenValidator.makeAccessTokenFromJSON(
                        R"({"p":"my-test-product-id","d":"my-device-id","e":9999999999,"i":1})");

                expect(token.has_value());
                expectEquals(token->productId, juce::String("my-test-product-id"));
                expectEquals(token->deviceId, deviceId);
                expect(token->userEmail.isEmpty());
            }

            beginTest("Unlocker Current User: returns validated access token email");
            {
                juce::ChangeBroadcaster changeBroadcaster;
                UnlockerImpl unlocker(changeBroadcaster, "test-product-id", {});

                expect(unlocker.getCurrentUser().isEmpty());

                AccessToken token;
                token.userEmail = "user@example.com";

                {
                    const juce::ScopedLock lock(unlocker._stateCriticalSection);
                    unlocker._state.validatedAccessToken = token;
                }

                expectEquals(unlocker.getCurrentUser(), juce::String("user@example.com"));
            }

            beginTest("makeAccessTokenFromJSON: parses refresh interval claim");
            {
                const auto token = tokenValidator.makeAccessTokenFromJSON(
                        R"({"p":"my-test-product-id","d":"my-device-id","e":9999999999,"i":1,"r":3})");
                const auto neverRefreshToken = tokenValidator.makeAccessTokenFromJSON(
                        R"({"p":"my-test-product-id","d":"my-device-id","e":9999999999,"i":1,"r":-1})");

                expect(token.has_value());
                expectEquals(token->refreshIntervalDays, juce::int64{3});
                expect(neverRefreshToken.has_value());
                expectEquals(neverRefreshToken->refreshIntervalDays, juce::int64{-1});
            }

            beginTest("makeAccessTokenFromJSON: defaults missing refresh interval to one day");
            {
                const auto token = tokenValidator.makeAccessTokenFromJSON(
                        R"({"p":"my-test-product-id","d":"my-device-id","e":9999999999,"i":1})");

                expect(token.has_value());
                expectEquals(token->refreshIntervalDays, juce::int64{1});
            }

            beginTest("makeAccessTokenFromJSON: defaults invalid refresh intervals to one day");
            {
                const juce::StringArray invalidClaims{
                        R"("r":-2)",
                        R"("r":1.5)",
                        R"("r":"2")",
                        R"("r":null)",
                        R"("r":true)"
                };

                for (const auto &invalidClaim : invalidClaims) {
                    const auto token = tokenValidator.makeAccessTokenFromJSON(
                            R"({"p":"my-test-product-id","d":"my-device-id","e":9999999999,"i":1,)"
                            + invalidClaim + "}");

                    expect(token.has_value());
                    expectEquals(token->refreshIntervalDays, juce::int64{1});
                }
            }

            beginTest("AccessToken: refreshes after configured interval");
            {
                AccessToken recentToken;
                recentToken.issuedAt = now - 47LL * 3600 * 1000;
                recentToken.refreshIntervalDays = 2;

                AccessToken oldToken;
                oldToken.issuedAt = now - 49LL * 3600 * 1000;
                oldToken.refreshIntervalDays = 2;

                expect(!recentToken.shouldRefresh());
                expect(oldToken.shouldRefresh());
            }

            beginTest("AccessToken: minus one never refreshes");
            {
                AccessToken token;
                token.issuedAt = 1;
                token.refreshIntervalDays = -1;

                expect(!token.shouldRefresh());
            }

            beginTest("AccessToken: zero refreshes a token with positive age");
            {
                AccessToken token;
                token.issuedAt = now - 1;
                token.refreshIntervalDays = 0;

                expect(token.shouldRefresh());
            }

            beginTest("validateAccessTokenClaims: invalid device id");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.deviceId = deviceId + "+extra";
                token.userEmail = "user@example.com";
                token.issuedAt = issuedAt;
                token.expiresAt = expiresAt;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: expired");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.deviceId = deviceId;
                token.userEmail = "user@example.com";
                token.issuedAt = issuedAt;
                token.expiresAt = now - 1;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: issued in future");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.deviceId = deviceId;
                token.userEmail = "user@example.com";
                token.issuedAt = now + 60 * 1000;
                token.expiresAt = expiresAt;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: product id suffix does not match");
            {
                AccessToken token;
                token.productId = "-test-product-id";
                token.deviceId = deviceId;
                token.userEmail = "user@example.com";
                token.issuedAt = issuedAt;
                token.expiresAt = expiresAt;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: missing product id");
            {
                AccessToken token;
                token.deviceId = deviceId;
                token.userEmail = "user@example.com";
                token.issuedAt = issuedAt;
                token.expiresAt = expiresAt;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: missing device id");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.userEmail = "user@example.com";
                token.issuedAt = issuedAt;
                token.expiresAt = expiresAt;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: missing user email");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.deviceId = deviceId;
                token.issuedAt = issuedAt;
                token.expiresAt = expiresAt;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: missing expiration");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.deviceId = deviceId;
                token.userEmail = "user@example.com";
                token.issuedAt = issuedAt;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }

            beginTest("validateAccessTokenClaims: missing issued at");
            {
                AccessToken token;
                token.productId = "my-test-product-id";
                token.deviceId = deviceId;
                token.userEmail = "user@example.com";
                token.expiresAt = expiresAt;
                expect(!tokenValidator.validateAccessTokenClaims(token));
            }
        }
    };

    static UnlockerTests unlockerTests;
} // namespace inlay::internal
