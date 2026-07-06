#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include "../Unlocker.h"
#include "Api.h"
#include "TokenValidator.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace inlay::internal {
    class AuthCallbackListener;
    class UnlockerTests;

    class UnlockerImpl : private juce::Thread,
                         private juce::AsyncUpdater,
                         private juce::Timer {
    public:
        UnlockerImpl(juce::ChangeBroadcaster &changeBroadcasterToUse,
                     const juce::String &productIdToUse,
                     const juce::String &publicKeyToUse);
        UnlockerImpl(juce::ChangeBroadcaster &changeBroadcasterToUse, Unlocker::Config config);

        ~UnlockerImpl() override;

        Unlocker::Status getStatus() const;

        bool isLocked() const;

        juce::String getError() const;

        juce::String getCurrentUser() const;

        std::optional<Unlocker::AppUpdate> getAppUpdate() const;

        void startup();

        void startActivation();

        void retryUnlocking();

        void logout();

        void skipCurrentAppUpdateVersion();

        void openWebsite(const juce::String &url) const;

    private:
        friend class UnlockerTests;

        UnlockerImpl(juce::ChangeBroadcaster &changeBroadcasterToUse,
                     const juce::String &productIdToUse,
                     const juce::String &publicKeyToUse,
                     const juce::File &inlayDirToUse);
        UnlockerImpl(juce::ChangeBroadcaster &changeBroadcasterToUse,
                     const juce::String &productIdToUse,
                     const juce::String &publicKeyToUse,
                     const juce::File &inlayDirToUse,
                     const juce::String &apiURLToUse);

        enum class WorkerTask {
            none,
            startup,
            startupWithDelay,
            startActivation,
            refreshAccessWithDelay,
        };

        struct State {
            Unlocker::Status status = Unlocker::Status::undefined;
            juce::String errorMessage;
            std::optional<AccessToken> validatedAccessToken;
            std::optional<Unlocker::AppUpdate> appUpdate;
            juce::String skippedAppUpdateVersion;

            WorkerTask pendingTask = WorkerTask::none;
            WorkerTask currentTask = WorkerTask::none;
            std::atomic_bool breakCurrentTask { false };
        };

        struct TaskResult {
            std::optional<Unlocker::Status> newStatus;
            juce::String newErrorMessage;
            WorkerTask newPendingTask = WorkerTask::none;
        };

        struct ActivationEvent {
            juce::int64 time = 0;
            juce::String instanceID;
        };

        static TaskResult makeTaskResult();
        static TaskResult makeTaskResult(Unlocker::Status newStatus,
                                         juce::String newErrorMessage = {},
                                         WorkerTask newPendingTask = WorkerTask::none);
        static TaskResult makeTaskError(juce::String newErrorMessage);
        static TaskResult makePendingTaskResult(WorkerTask newPendingTask);
        static std::optional<ActivationEvent> parseActivationEventFromText(const juce::String &text);

        Api::MetaData getMeta() const;

        void run() override;
        TaskResult runTask(WorkerTask task);
        void handleTaskResult(const TaskResult &res);
        void setStatus(Unlocker::Status status);

        TaskResult runStartupTask();

        TaskResult runStartupWithDelayTask();

        TaskResult runStartActivationTask();

        TaskResult runRefreshAccessWithDelayTask();

        void handleAsyncUpdate() override;
        void timerCallback() override;

        juce::Result ensureInlayDirExists() const;

        bool prepareStartActivation();

        bool prepareRetryUnlocking();

        bool prepareStartupFromActivationEvent();

        bool shouldReactToActivationEvent(const ActivationEvent &event, juce::int64 now) const;

        void applyLogoutState();

        void startWorkerIfNeeded();

        void updateActivationEventWatcher();

        void triggerActivationEvent();

        void checkActivationEvent();

        bool taskShouldExit() const;

        bool acquireInstanceLock();

        void saveAppUpdateFromResponse(const std::optional<Api::AppUpdate> &appUpdate);

        juce::ChangeBroadcaster &_changeBroadcaster;

        const juce::String _instanceID;
        const juce::String _productId;
        const juce::String _publicKey;
        const juce::String _deviceId;

        juce::File _inlayDir;
        juce::File _idTokenFile;
        juce::File _accessTokenFile;
        juce::File _lockFile;
        juce::File _activationEventFile;
        juce::File _appUpdateFile;
        juce::File _skippedAppUpdateFile;

        std::unique_ptr<Api> _api;
        std::unique_ptr<AuthCallbackListener> _authCallbackListener;
        TokenValidator _tokenValidator;

        State _state;
        mutable juce::CriticalSection _stateCriticalSection;
        std::atomic_bool _isLocked { true };

        juce::String _idToken;

        juce::WaitableEvent _wakeEvent;
    };
} // namespace inlay::internal
