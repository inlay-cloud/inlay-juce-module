#pragma once

#include <memory>
#include <optional>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace inlay::internal {
    class UnlockerImpl;
}

namespace inlay {
    /** Controls Inlay licensing for a product.

        Unlocker owns the activation and unlock state for one product. Construct it
        with the product credentials, register any juce::ChangeListener instances
        that need to react to state changes, then call startup() to begin loading
        and validating the persisted license state.
    */
    class Unlocker : public juce::ChangeBroadcaster {
    public:
        /** Current licensing state.

            Use getStatus() or listen for ChangeBroadcaster updates to decide
            whether to show activation UI, wait for an unlock operation, or enable
            protected product functionality.
        */
        enum class Status {
            /** startup() has not been called yet. */
            undefined,

            /** The product has no valid persisted activation and must be activated. */
            activationRequired,

            /** An unlock, activation, or refresh operation is currently in progress. */
            unlocking,

            /** The product is unlocked and protected functionality can be enabled. */
            unlocked
        };

        /** Configuration used to construct an Unlocker.

            Leave apiURL empty to use the default Inlay API endpoint.
        */
        struct Config {
            /** Inlay product identifier. */
            juce::String productId;

            /** Public key used to validate tokens for the product. */
            juce::String publicKey;

            /** Optional API base URL override. */
            juce::String apiURL;
        };

        /** Information about an available application update.

            Returned by getAppUpdate() when the server reports a newer version
            that has not been skipped by the user.
        */
        struct AppUpdate {
            /** Version string reported by Inlay. */
            juce::String version;

            /** Website URL where the update can be opened. */
            juce::String url;
        };

        /** Creates an unlocker for a product using the default Inlay API endpoint. */
        Unlocker(const juce::String &productIdToUse, const juce::String &publicKeyToUse);

        /** Creates an unlocker using the supplied configuration. */
        explicit Unlocker(Config config);

        /** Stops background work and releases resources owned by the unlocker. */
        ~Unlocker() override;

        /** Returns the current licensing status.

            This method is intended for UI and other non-realtime code. It is not realtime-safe.
            For realtime code, use isLocked() or cache your own realtime-safe status from
            ChangeBroadcaster updates.
        */
        Status getStatus() const;

        /** Returns true unless the current status is Status::unlocked.

            This method is realtime-safe and may be called from audio processing threads.
        */
        bool isLocked() const;

        /** Returns the last user-facing error message, or an empty string if none is available. */
        juce::String getError() const;

        /** Returns the email address associated with the validated license, if available. */
        juce::String getCurrentUser() const;

        /** Returns the currently visible app update, if one is available and not skipped. */
        std::optional<AppUpdate> getAppUpdate() const;

        /** Starts loading and validating persisted license state.

            This method is intended to be called once after construction. It moves
            the unlocker from Status::undefined to Status::unlocking and performs
            the initial work asynchronously.
        */
        void startup();

        /** Starts the browser-based activation flow.

            This is normally called when getStatus() returns Status::activationRequired.
        */
        void startActivation();

        /** Retries unlocking after an unlock operation has reported an error. */
        void retryUnlocking();

        /** Clears the current activation state and returns the unlocker to activation required. */
        void logout();

        /** Hides the current app update version and persists that decision. */
        void skipCurrentAppUpdateVersion();

        /** Opens the given URL in the user's default browser. */
        void openWebsite(const juce::String &url) const;

    private:
        std::unique_ptr<internal::UnlockerImpl> _impl;
    };
} // namespace inlay
