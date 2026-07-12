#include "UnlockerImpl.h"
#include "AuthCallbackListener.h"

#include <utility>

namespace inlay::internal {
    namespace {
        constexpr const char *defaultApiBaseURL = "https://api.inlay.cloud";
        constexpr const char *moduleVersion = "JUCE-1.0.1";
        constexpr const char *workerThreadName = "Inlay Unlocker Worker";
        const int instanceLockMs = 5000;
        const int activationEventPollMs = 2000;
        const int activationEventFreshnessMs = 5000;

        juce::String statusToString(Unlocker::Status status) {
            switch (status) {
                case Unlocker::Status::activationRequired:
                    return "activation_required";
                case Unlocker::Status::unlocking:
                    return "unlocking";
                case Unlocker::Status::unlocked:
                    return "unlocked";
            }

            return "unknown";
        }

        juce::File getInlayDirPath(const juce::String &productID) {
            return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("InlayCloud").getChildFile(productID);
        }

        std::optional<juce::String> readStringProperty(const juce::DynamicObject &object,
                                                       const juce::Identifier &propertyName) {
            if (!object.hasProperty(propertyName))
                return std::nullopt;

            const auto propertyValue = object.getProperty(propertyName).toString();
            if (propertyValue.isEmpty())
                return std::nullopt;

            return propertyValue;
        }

        std::optional<juce::int64> readInt64Property(const juce::DynamicObject &object,
                                                     const juce::Identifier &propertyName) {
            if (!object.hasProperty(propertyName))
                return std::nullopt;

            const auto propertyValue = object.getProperty(propertyName).toString();
            if (propertyValue.isEmpty())
                return std::nullopt;

            return propertyValue.getLargeIntValue();
        }

        Unlocker::AppUpdate makeUnlockerAppUpdate(juce::String version, juce::String url) {
            Unlocker::AppUpdate appUpdate;
            appUpdate.version = std::move(version);
            appUpdate.url = std::move(url);

            return appUpdate;
        }

        std::optional<Unlocker::AppUpdate> parseAppUpdate(const juce::DynamicObject &obj) {
            const auto version = readStringProperty(obj, "version");
            const auto url = readStringProperty(obj, "url");
            if (!version.has_value() || !url.has_value())
                return std::nullopt;

            return makeUnlockerAppUpdate(*version, *url);
        }

        std::optional<Unlocker::AppUpdate> parseAppUpdateFromText(const juce::String &text) {
            const auto json = juce::JSON::parse(text);
            const auto *obj = json.getDynamicObject();
            if (obj == nullptr)
                return std::nullopt;
            return parseAppUpdate(*obj);
        }

        std::optional<Unlocker::AppUpdate> readAppUpdateFile(const juce::File &file) {
            const auto text = file.loadFileAsString();
            if (text.trim().isEmpty())
                return std::nullopt;

            return parseAppUpdateFromText(text);
        }

        bool writeAppUpdateFile(const juce::File &file, const Unlocker::AppUpdate &appUpdate) {
            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty("version", appUpdate.version);
            obj->setProperty("url", appUpdate.url);

            return file.replaceWithText(juce::JSON::toString(juce::var(obj.get())));
        }

        juce::String readSkippedAppUpdateVersionFile(const juce::File &file) {
            const auto text = file.loadFileAsString().trim();
            if (text.isEmpty())
                return {};

            const auto json = juce::JSON::parse(text);
            if (auto *obj = json.getDynamicObject(); obj != nullptr)
                return readStringProperty(*obj, "version").value();

            return {};
        }

        bool writeSkippedAppUpdateVersionFile(const juce::File &file, const juce::String &version) {
            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty("version", version);
            return file.replaceWithText(juce::JSON::toString(juce::var(obj.get())));
        }

        juce::String getProductVersionString() {
            if (auto *app = juce::JUCEApplicationBase::getInstance())
                return app->getApplicationVersion();

#if defined(JucePlugin_VersionString)
            return JucePlugin_VersionString;
#else
            return {};
#endif
        }

        std::optional<Unlocker::AppUpdate> getVisibleAppUpdate(
            const std::optional<Unlocker::AppUpdate> &appUpdate,
            const std::optional<juce::String> &skippedVersion) {
            if (!appUpdate.has_value())
                return std::nullopt;
            if (appUpdate->version.isEmpty() || appUpdate->url.isEmpty())
                return std::nullopt;
            if (skippedVersion.has_value() && *skippedVersion == appUpdate->version)
                return std::nullopt;

            if (appUpdate->version == getProductVersionString())
                return std::nullopt;

            return appUpdate;
        }

        juce::String getSystemStatsJSON() {
            juce::DynamicObject::Ptr stats = new juce::DynamicObject();

            stats->setProperty("juceVersion", juce::SystemStats::getJUCEVersion());
            stats->setProperty("operatingSystemName", juce::SystemStats::getOperatingSystemName());
            stats->setProperty("deviceDescription", juce::SystemStats::getDeviceDescription());
            stats->setProperty("uniqueDeviceID", juce::SystemStats::getUniqueDeviceID());
            stats->setProperty("logonName", juce::SystemStats::getLogonName());
            stats->setProperty("fullUserName", juce::SystemStats::getFullUserName());
            stats->setProperty("computerName", juce::SystemStats::getComputerName());
            stats->setProperty("userLanguage", juce::SystemStats::getUserLanguage());
            stats->setProperty("userRegion", juce::SystemStats::getUserRegion());
            stats->setProperty("displayLanguage", juce::SystemStats::getDisplayLanguage());
            stats->setProperty("cpuVendor", juce::SystemStats::getCpuVendor());
            stats->setProperty("memorySizeInMegabytes", juce::SystemStats::getMemorySizeInMegabytes());
            stats->setProperty("numCpus", juce::SystemStats::getNumCpus());
            stats->setProperty("isOperatingSystem64Bit", juce::SystemStats::isOperatingSystem64Bit());
            stats->setProperty("hasMMX", juce::SystemStats::hasMMX());
            stats->setProperty("hasSSE", juce::SystemStats::hasSSE());
            stats->setProperty("hasSSE2", juce::SystemStats::hasSSE2());
            stats->setProperty("hasSSE3", juce::SystemStats::hasSSE3());
            stats->setProperty("has3DNow", juce::SystemStats::has3DNow());
            stats->setProperty("hasAVX", juce::SystemStats::hasAVX());
            stats->setProperty("hasAVX2", juce::SystemStats::hasAVX2());
            stats->setProperty("hasFMA3", juce::SystemStats::hasFMA3());
            stats->setProperty("hasFMA4", juce::SystemStats::hasFMA4());

            return juce::JSON::toString(juce::var(stats.get()));
        }

        Api::AccessRequest makeAccessRequest(juce::String idToken, Api::MetaData metaData) {
            Api::AccessRequest request;
            request.idToken = std::move(idToken);
            request.metaData = std::move(metaData);

            return request;
        }

        Api::CompleteAuthRequest makeCompleteAuthRequest(juce::String activationToken, Api::MetaData metaData) {
            Api::CompleteAuthRequest request;
            request.activationToken = std::move(activationToken);
            request.metaData = std::move(metaData);

            return request;
        }

        juce::String getProductNameString() {
            if (auto *app = juce::JUCEApplicationBase::getInstance())
                return app->getApplicationName();

#if defined(JucePlugin_Name)
            return JucePlugin_Name;
#else
            return {};
#endif
        }

        bool isPluginInstance() {
            if (juce::JUCEApplicationBase::getInstance() != nullptr)
                return !juce::JUCEApplicationBase::isStandaloneApp();

#if defined(JucePlugin_Name)
            return true;
#else
            return false;
#endif
        }

        juce::String getValidAuthCallbackRedirectUrlPrefix(const juce::String &apiUrl) {
            if (apiUrl.startsWith("http://localhost")) {
                return "http://localhost";
            }
            return apiUrl.replaceFirstOccurrenceOf("https://api", "https://app", false);
        }
    } // namespace

    UnlockerImpl::UnlockerImpl(juce::ChangeBroadcaster &changeBroadcasterToUse,
                               const juce::String &productIdToUse,
                               const juce::String &publicKeyToUse)
        : UnlockerImpl(changeBroadcasterToUse, productIdToUse, publicKeyToUse, getInlayDirPath(productIdToUse), {}) {
    }

    UnlockerImpl::UnlockerImpl(juce::ChangeBroadcaster &changeBroadcasterToUse, Unlocker::Config config)
        : UnlockerImpl(changeBroadcasterToUse,
                       config.productId,
                       config.publicKey,
                       getInlayDirPath(config.productId),
                       config.apiURL.isNotEmpty() ? config.apiURL : defaultApiBaseURL) {
    }

    UnlockerImpl::UnlockerImpl(juce::ChangeBroadcaster &changeBroadcasterToUse,
                               const juce::String &productIdToUse,
                               const juce::String &publicKeyToUse,
                               const juce::File &inlayDirToUse)
        : UnlockerImpl(changeBroadcasterToUse, productIdToUse, publicKeyToUse, inlayDirToUse, defaultApiBaseURL) {
    }

    UnlockerImpl::UnlockerImpl(juce::ChangeBroadcaster &changeBroadcasterToUse,
                               const juce::String &productIdToUse,
                               const juce::String &publicKeyToUse,
                               const juce::File &inlayDirToUse,
                               const juce::String &apiURLToUse)
        : juce::Thread(workerThreadName),
          _changeBroadcaster(changeBroadcasterToUse),
          _instanceID(juce::Uuid().toString()),
          _productId(productIdToUse),
          _publicKey(publicKeyToUse),
          _deviceId(juce::SystemStats::getUniqueDeviceID()),
          _inlayDir(inlayDirToUse),
          _idTokenFile(_inlayDir.getChildFile("id-token")),
          _accessTokenFile(_inlayDir.getChildFile("access-token")),
          _lockFile(_inlayDir.getChildFile("lock.json")),
          _activationEventFile(_inlayDir.getChildFile("activation-event.json")),
          _appUpdateFile(_inlayDir.getChildFile("update.json")),
          _skippedAppUpdateFile(_inlayDir.getChildFile("skipped-update.json")),
          _api(std::make_unique<Api>(apiURLToUse, productIdToUse, moduleVersion, _instanceID)),
          _authCallbackListener(
              std::make_unique<AuthCallbackListener>(getValidAuthCallbackRedirectUrlPrefix(apiURLToUse))),
          _tokenValidator(_productId, _publicKey, _deviceId) {
        juce::Logger::writeToLog("UnlockerImpl::UnlockerImpl()");
        juce::Logger::writeToLog("instanceID: " + _instanceID);
        juce::Logger::writeToLog("productIdToUse: " + productIdToUse);
        juce::Logger::writeToLog("publicKeyToUse: " + publicKeyToUse);
        juce::Logger::writeToLog("deviceID: " + _deviceId);
        juce::Logger::writeToLog("inlayDir: " + _inlayDir.getFullPathName());
        juce::Logger::writeToLog("productVersion: " + getProductVersionString());
    }

    UnlockerImpl::~UnlockerImpl() {
        juce::Logger::writeToLog("UnlockerImpl::~UnlockerImpl()");
        {
            const juce::ScopedLock lock(_stateCriticalSection);
            _state.pendingTask = WorkerTask::none;
            _state.breakCurrentTask.store(true);
        }

        _wakeEvent.signal();
        signalThreadShouldExit();
        stopThread(-1);
        stopTimer();
        cancelPendingUpdate();
    }

    Unlocker::Status UnlockerImpl::getStatus() const {
        const juce::ScopedLock lock(_stateCriticalSection);
        return _state.status;
    }

    bool UnlockerImpl::isLocked() const {
        return _isLocked.load(std::memory_order_relaxed);
    }

    juce::String UnlockerImpl::getError() const {
        const juce::ScopedLock lock(_stateCriticalSection);
        return _state.errorMessage;
    }

    juce::String UnlockerImpl::getCurrentUser() const {
        const juce::ScopedLock lock(_stateCriticalSection);
        if (!_state.validatedAccessToken.has_value())
            return {};

        return _state.validatedAccessToken->userEmail;
    }

    std::optional<Unlocker::AppUpdate> UnlockerImpl::getAppUpdate() const {
        const juce::ScopedLock lock(_stateCriticalSection);
        return getVisibleAppUpdate(_state.appUpdate, _state.skippedAppUpdateVersion);
    }

    UnlockerImpl::TaskResult UnlockerImpl::makeTaskResult() {
        TaskResult result;
        return result;
    }

    UnlockerImpl::TaskResult UnlockerImpl::makeTaskResult(Unlocker::Status newStatus,
                                                          juce::String newErrorMessage,
                                                          WorkerTask newPendingTask) {
        TaskResult result;
        result.newStatus = newStatus;
        result.newErrorMessage = std::move(newErrorMessage);
        result.newPendingTask = newPendingTask;

        return result;
    }

    UnlockerImpl::TaskResult UnlockerImpl::makeTaskError(juce::String newErrorMessage) {
        TaskResult result;
        result.newErrorMessage = std::move(newErrorMessage);

        return result;
    }

    UnlockerImpl::TaskResult UnlockerImpl::makePendingTaskResult(WorkerTask newPendingTask) {
        TaskResult result;
        result.newPendingTask = newPendingTask;

        return result;
    }

    std::optional<UnlockerImpl::ActivationEvent> UnlockerImpl::parseActivationEventFromText(const juce::String &text) {
        if (text.trim().isEmpty())
            return std::nullopt;

        const auto json = juce::JSON::parse(text);
        const auto *obj = json.getDynamicObject();
        if (obj == nullptr)
            return std::nullopt;

        const auto time = readInt64Property(*obj, "time");
        const auto instanceID = readStringProperty(*obj, "instanceID");
        if (!time.has_value() || !instanceID.has_value())
            return std::nullopt;

        ActivationEvent event;
        event.time = *time;
        event.instanceID = *instanceID;
        return event;
    }

    Api::MetaData UnlockerImpl::getMeta() const {
        Api::MetaData metaData;
        metaData.moduleVersion = moduleVersion;
        metaData.deviceId = _deviceId;
        metaData.os = juce::SystemStats::getOperatingSystemName();
        metaData.systemStats = getSystemStatsJSON();
        metaData.productId = _productId;
        metaData.productVersion = getProductVersionString();
        metaData.productName = getProductNameString();
        metaData.isPlugin = isPluginInstance();
        metaData.inlayDir = _inlayDir.getFullPathName();
        metaData.instanceId = _instanceID;

        return metaData;
    }

    void UnlockerImpl::startup() {
        juce::Logger::writeToLog("startup");
        {
            const juce::ScopedLock stateLock(_stateCriticalSection);

            if (_state.status != Unlocker::Status::undefined) return;

            setStatus(Unlocker::Status::unlocking);
            _state.errorMessage.clear();
            _state.pendingTask = WorkerTask::startup;
            _state.breakCurrentTask.store(false);
        }

        if (!isThreadRunning())
            startThread();

        triggerAsyncUpdate();
        _wakeEvent.signal();
    }

    void UnlockerImpl::startActivation() {
        juce::Logger::writeToLog("UnlockerImpl::startActivation()");

        if (!prepareStartActivation())
            return;

        startWorkerIfNeeded();
        triggerAsyncUpdate();
        _wakeEvent.signal();
    }

    void UnlockerImpl::retryUnlocking() {
        juce::Logger::writeToLog("UnlockerImpl::retryUnlocking()");

        if (!prepareRetryUnlocking())
            return;

        startWorkerIfNeeded();
        triggerAsyncUpdate();
        _wakeEvent.signal();
    }

    void UnlockerImpl::logout() {
        juce::Logger::writeToLog("UnlockerImpl::logout()");

        applyLogoutState();
        triggerAsyncUpdate();
        _wakeEvent.signal();
    }

    void UnlockerImpl::skipCurrentAppUpdateVersion() {
        const auto appUpdate = getAppUpdate();
        if (!appUpdate.has_value())
            return;

        {
            const juce::ScopedLock lock(_stateCriticalSection);
            _state.skippedAppUpdateVersion = appUpdate->version;
        }

        if (ensureInlayDirExists().wasOk())
            writeSkippedAppUpdateVersionFile(_skippedAppUpdateFile, appUpdate->version);

        triggerAsyncUpdate();
    }

    void UnlockerImpl::openWebsite(const juce::String &url) const {
        juce::URL(url).launchInDefaultBrowser();
    }

    void UnlockerImpl::run() {
        juce::Logger::writeToLog("UnlockerImpl::run()");
        const auto exitGuard = juce::ScopeGuard{
            [] {
                juce::Logger::writeToLog("UnlockerImpl::run() - exited");
            }
        };

        while (!threadShouldExit()) {
            _wakeEvent.wait();
            juce::Logger::writeToLog("UnlockerImpl::run() - woke up");

            if (threadShouldExit()) break;

            WorkerTask taskToRun = WorkerTask::none;
            {
                const juce::ScopedLock lock(_stateCriticalSection);
                taskToRun = _state.pendingTask;
                _state.pendingTask = WorkerTask::none;
                _state.breakCurrentTask.store(false);
                _state.currentTask = taskToRun;
            }

            if (taskToRun != WorkerTask::none) {
                const auto res = runTask(taskToRun);
                handleTaskResult(res);
            }
        }
    }

    UnlockerImpl::TaskResult UnlockerImpl::runTask(WorkerTask task) {
        switch (task) {
            case WorkerTask::startup:
                return runStartupTask();
            case WorkerTask::startupWithDelay:
                return runStartupWithDelayTask();
            case WorkerTask::startActivation:
                return runStartActivationTask();
            case WorkerTask::refreshAccessWithDelay:
                return runRefreshAccessWithDelayTask();
            case WorkerTask::none:
                return makeTaskResult();
            default:
                return makeTaskResult();
        }
    }

    void UnlockerImpl::handleTaskResult(const TaskResult &res) {
        bool shouldWake = false;
        bool shouldTriggerUpdate = false;
        bool shouldTriggerActivationEvent = false;

        {
            const juce::ScopedLock lock(_stateCriticalSection);

            _state.currentTask = WorkerTask::none;

            if (_state.breakCurrentTask.load()) {
                _state.breakCurrentTask.store(false);
                // any state updates are ignored
                return;
            }

            if (res.newStatus.has_value()) {
                const auto previousStatus = _state.status;
                setStatus(res.newStatus.value());
                shouldTriggerUpdate = true;
                shouldTriggerActivationEvent = previousStatus != Unlocker::Status::unlocked
                                               && _state.status == Unlocker::Status::unlocked;
            }

            if (!res.newErrorMessage.isEmpty()) {
                _state.errorMessage = res.newErrorMessage;
                shouldTriggerUpdate = true;
            }

            if (res.newPendingTask != WorkerTask::none) {
                _state.pendingTask = res.newPendingTask;
                shouldWake = true;
            }
        }

        if (shouldTriggerActivationEvent)
            triggerActivationEvent();

        if (shouldTriggerUpdate) {
            triggerAsyncUpdate();
        }
        if (shouldWake) {
            _wakeEvent.signal();
        }
    }

    void UnlockerImpl::setStatus(Unlocker::Status status) {
        _state.status = status;
        _isLocked.store(status != Unlocker::Status::unlocked, std::memory_order_relaxed);
    }

    void UnlockerImpl::saveAppUpdateFromResponse(const std::optional<Api::AppUpdate> &appUpdate) {
        std::optional<Unlocker::AppUpdate> appUpdateToWrite;
        bool shouldWriteUpdate = false;
        bool shouldDeleteUpdate = false;

        {
            const juce::ScopedLock lock(_stateCriticalSection);
            const auto before = getVisibleAppUpdate(_state.appUpdate, _state.skippedAppUpdateVersion);

            if (appUpdate.has_value()) {
                _state.appUpdate = makeUnlockerAppUpdate(appUpdate->version, appUpdate->url);
                appUpdateToWrite = _state.appUpdate;
                shouldWriteUpdate = true;
            } else {
                shouldDeleteUpdate = true;
                _state.appUpdate.reset();
            }
        }

        if (shouldWriteUpdate && appUpdateToWrite.has_value() && ensureInlayDirExists().wasOk())
            writeAppUpdateFile(_appUpdateFile, *appUpdateToWrite);

        if (shouldDeleteUpdate)
            _appUpdateFile.deleteFile();
    }

    UnlockerImpl::TaskResult UnlockerImpl::runStartupTask() {
        juce::Logger::writeToLog("UnlockerImpl::runStartupTask()");

        if (taskShouldExit())
            return makeTaskResult();

        if (const auto result = ensureInlayDirExists(); result.failed()) {
            return makeTaskResult(Unlocker::Status::unlocking,
                                  "Failed to prepare local storage. " + result.getErrorMessage());
        }

        const auto appUpdate = readAppUpdateFile(_appUpdateFile);
        const auto skippedVersion = readSkippedAppUpdateVersionFile(_skippedAppUpdateFile);
        {
            const juce::ScopedLock lock(_stateCriticalSection);
            _state.appUpdate = appUpdate;
            _state.skippedAppUpdateVersion = skippedVersion;
        }

        _idToken = _idTokenFile.loadFileAsString();
        if (_idToken.isEmpty()) {
            return makeTaskResult(Unlocker::Status::activationRequired);
        }

        const auto accessToken = _accessTokenFile.loadFileAsString();
        if (!accessToken.isEmpty()) {
            const auto validatedAccessToken = _tokenValidator.validateAccessToken(accessToken);
            {
                const juce::ScopedLock lock(_stateCriticalSection);
                _state.validatedAccessToken = validatedAccessToken;
            }

            if (validatedAccessToken.has_value()) {
                return makeTaskResult(Unlocker::Status::unlocked,
                                      {},
                                      _state.validatedAccessToken->shouldRefresh()
                                          ? WorkerTask::refreshAccessWithDelay
                                          : WorkerTask::none);
            }
        }

        // no token or invalid/expired

        if (taskShouldExit()) return makeTaskResult();

        if (!acquireInstanceLock()) {
            return makePendingTaskResult(WorkerTask::startupWithDelay);
        }

        if (taskShouldExit()) return makeTaskResult();

        const auto accessResp = _api->requestAccess(makeAccessRequest(_idToken, getMeta()));
        if (accessResp.statusCode == 422) {
            _idTokenFile.deleteFile();
            _accessTokenFile.deleteFile();
            {
                const juce::ScopedLock lock(_stateCriticalSection);
                _idToken.clear();
                _state.validatedAccessToken.reset();;
            }

            return makeTaskResult(Unlocker::Status::activationRequired);
        }

        if (accessResp.statusCode != 200 && accessResp.statusCode != 201) {
            return makeTaskError("Failed to unlock. " + accessResp.toErrMsg());
        }
        if (!accessResp.okPayload.has_value()) {
            return makeTaskError("Failed to unlock. Internal error");
        }

        if (taskShouldExit()) return makeTaskResult();

        if (const auto result = ensureInlayDirExists(); result.failed()) {
            return makeTaskError("Failed to prepare local storage. " + result.getErrorMessage());
        }

        _accessTokenFile.replaceWithText(accessResp.okPayload->accessToken);

        const auto validatedAccessToken = _tokenValidator.validateAccessToken(accessResp.okPayload->accessToken);
        {
            const juce::ScopedLock lock(_stateCriticalSection);
            _state.validatedAccessToken = validatedAccessToken;
        }

        if (!validatedAccessToken.has_value()) {
            return makeTaskError("Internal error. Code: VFASAZ");
        }

        saveAppUpdateFromResponse(accessResp.okPayload->appUpdate);

        return makeTaskResult(Unlocker::Status::unlocked);
    }

    UnlockerImpl::TaskResult UnlockerImpl::runStartupWithDelayTask() {
        juce::Logger::writeToLog("UnlockerImpl::runStartupWithDelayTask()");
        const auto delay = juce::Random::getSystemRandom().nextInt({1000, 3000});
        wait(delay);
        if (taskShouldExit())
            return makeTaskResult();

        return runStartupTask();
    }

    UnlockerImpl::TaskResult UnlockerImpl::runStartActivationTask() {
        juce::Logger::writeToLog("UnlockerImpl::runStartActivationTask()");

        if (taskShouldExit())
            return makeTaskResult();

        const Api::StartAuthRequest startAuthReq{_productId, _deviceId};

        const auto startAuthResp = _api->startAuth(startAuthReq);
        if (startAuthResp.statusCode != 200) {
            return makeTaskResult(Unlocker::Status::activationRequired,
                                  "Failed to start activation. " + startAuthResp.toErrMsg());
        }
        if (!startAuthResp.okPayload.has_value()) {
            return makeTaskResult(Unlocker::Status::activationRequired,
                                  "Failed to start activation. Internal error");
        }

        if (const auto result = _authCallbackListener->createListenerOnRandomPort(); result.failed()) {
            return makeTaskResult(Unlocker::Status::activationRequired,
                                  "Failed to start activation. Unable to start listener on local port");
        }

        const auto closeGuard = juce::ScopeGuard{
            [this] {
                _authCallbackListener->close();
            }
        };

        const auto callbackURL = _authCallbackListener->getEndpointURL();
        const auto continueURL = _api->makeContinueAuthURL(startAuthResp.okPayload->activationToken, callbackURL);

        if (!continueURL.launchInDefaultBrowser()) {
            return makeTaskResult(Unlocker::Status::activationRequired,
                                  "Failed to start activation. Unable to launch browser");
        }

        if (const auto ok = _authCallbackListener->waitForRequest(_state.breakCurrentTask); !ok) {
            return makeTaskResult();
        }

        if (taskShouldExit())
            return makeTaskResult();

        const auto accessResp = _api->completeAuth(makeCompleteAuthRequest(startAuthResp.okPayload->activationToken,
                                                                           getMeta()));
        if (accessResp.statusCode != 200) {
            return makeTaskResult(Unlocker::Status::activationRequired,
                                  "Failed to complete activation. " + accessResp.toErrMsg());
        }
        if (!accessResp.okPayload.has_value()) {
            return makeTaskResult(Unlocker::Status::activationRequired,
                                  "Failed to complete activation. Internal error");
        }

        if (taskShouldExit()) return makeTaskResult();

        _idToken = accessResp.okPayload->idToken;

        if (const auto result = ensureInlayDirExists(); result.failed()) {
            return makeTaskResult(Unlocker::Status::activationRequired,
                                  "Failed to complete activation. Unable to prepare local storage");
        }

        if (!_idTokenFile.replaceWithText(_idToken)) {
            return makeTaskError("Failed to complete activation. Unable to save a file");
        }

        const auto accessToken = accessResp.okPayload->accessToken;
        if (!_accessTokenFile.replaceWithText(accessToken)) {
            return makeTaskError("Failed to complete activation. Unable to save a file");
        }
        const auto validatedAccessToken = _tokenValidator.validateAccessToken(accessToken);
        {
            const juce::ScopedLock lock(_stateCriticalSection);
            _state.validatedAccessToken = validatedAccessToken;
        }

        if (!validatedAccessToken.has_value()) {
            return makeTaskError("Internal error. Code: VFASAN");
        }

        saveAppUpdateFromResponse(accessResp.okPayload->appUpdate);

        return makeTaskResult(Unlocker::Status::unlocked);
    }

    UnlockerImpl::TaskResult UnlockerImpl::runRefreshAccessWithDelayTask() {
        juce::Logger::writeToLog("UnlockerImpl::runRefreshAccessWithDelayTask()");

        const auto delay = juce::Random::getSystemRandom().nextInt({1000, 10000});
        wait(delay);

        if (taskShouldExit()) return makeTaskResult();

        if (const auto result = ensureInlayDirExists(); result.failed()) {
            return makeTaskError("Failed to prepare local storage. " + result.getErrorMessage());
        }

        const auto accessToken = _accessTokenFile.loadFileAsString();

        if (accessToken.isEmpty()) {
            {
                const juce::ScopedLock lock(_stateCriticalSection);
                _state.validatedAccessToken.reset();
            }
            return makeTaskResult(Unlocker::Status::unlocking, {}, WorkerTask::startup);
        }

        auto validatedAccessToken = _tokenValidator.validateAccessToken(accessToken);
        {
            const juce::ScopedLock lock(_stateCriticalSection);
            _state.validatedAccessToken = validatedAccessToken;
        }

        if (!validatedAccessToken.has_value()) {
            return makeTaskResult(Unlocker::Status::unlocking, {}, WorkerTask::startup);
        }

        if (!validatedAccessToken->shouldRefresh()) {
            // recently refreshed by another instance
            return makeTaskResult();
        }

        if (taskShouldExit()) return makeTaskResult();

        if (!acquireInstanceLock()) {
            return makePendingTaskResult(WorkerTask::refreshAccessWithDelay);
        }

        if (taskShouldExit()) return makeTaskResult();

        const auto accessResp = _api->requestAccess(makeAccessRequest(_idToken, getMeta()));

        if (accessResp.statusCode == 422) {
            _idTokenFile.deleteFile();
            _accessTokenFile.deleteFile();
            {
                const juce::ScopedLock lock(_stateCriticalSection);
                _idToken.clear();
                _state.validatedAccessToken.reset();
            }
            return makeTaskResult(Unlocker::Status::activationRequired);
        }

        if (accessResp.statusCode != 200 || !accessResp.okPayload.has_value()) {
            juce::Logger::writeToLog("Failed to refresh token because of non 200 response or missing payload");
            return makeTaskResult();
        }

        if (taskShouldExit()) return makeTaskResult();

        if (const auto result = ensureInlayDirExists(); result.failed()) {
            return makeTaskResult();
        }

        _accessTokenFile.replaceWithText(accessResp.okPayload->accessToken);
        validatedAccessToken = _tokenValidator.validateAccessToken(accessResp.okPayload->accessToken);
        {
            const juce::ScopedLock lock(_stateCriticalSection);
            _state.validatedAccessToken = validatedAccessToken;
        }


        if (!validatedAccessToken.has_value()) {
            // should not happen: server returned invalid token
            _idTokenFile.deleteFile();
            _accessTokenFile.deleteFile();
            return makeTaskResult(Unlocker::Status::activationRequired);
        }

        // just saving the update (if any). Change is not triggered, assuming the notification will be delivered on next
        // startup
        saveAppUpdateFromResponse(accessResp.okPayload->appUpdate);


        return makeTaskResult();
    }

    void UnlockerImpl::handleAsyncUpdate() {
        juce::Logger::writeToLog("UnlockerImpl::handleAsyncUpdate() - new status: " + statusToString(getStatus()));
        updateActivationEventWatcher();
        _changeBroadcaster.sendChangeMessage();
    }

    void UnlockerImpl::timerCallback() {
        checkActivationEvent();
    }

    juce::Result UnlockerImpl::ensureInlayDirExists() const {
        return _inlayDir.createDirectory();
    }

    bool UnlockerImpl::prepareStartActivation() {
        const juce::ScopedLock stateLock(_stateCriticalSection);

        if (_state.status != Unlocker::Status::activationRequired)
            return false;

        _state.errorMessage.clear();
        setStatus(Unlocker::Status::activationRequired);

        _state.breakCurrentTask.store(_state.currentTask != WorkerTask::none);

        _state.pendingTask = WorkerTask::startActivation;
        return true;
    }

    bool UnlockerImpl::prepareRetryUnlocking() {
        const juce::ScopedLock stateLock(_stateCriticalSection);

        if (_state.status != Unlocker::Status::unlocking || _state.errorMessage.isEmpty())
            return false;

        _state.errorMessage.clear();
        setStatus(Unlocker::Status::unlocking);

        _state.breakCurrentTask.store(_state.currentTask != WorkerTask::none);

        _state.pendingTask = WorkerTask::startup;
        return true;
    }

    bool UnlockerImpl::prepareStartupFromActivationEvent() {
        const juce::ScopedLock stateLock(_stateCriticalSection);

        if (_state.status != Unlocker::Status::activationRequired)
            return false;

        _state.errorMessage.clear();
        setStatus(Unlocker::Status::unlocking);
        _state.breakCurrentTask.store(_state.currentTask != WorkerTask::none);
        _state.pendingTask = WorkerTask::startup;
        return true;
    }

    bool UnlockerImpl::shouldReactToActivationEvent(const ActivationEvent &event, juce::int64 now) const {
        if (event.instanceID == _instanceID)
            return false;

        return now - event.time <= activationEventFreshnessMs;
    }

    void UnlockerImpl::applyLogoutState() {
        {
            const juce::ScopedLock stateLock(_stateCriticalSection);

            _idToken.clear();
            _state.validatedAccessToken = std::nullopt;
            _state.errorMessage.clear();
            setStatus(Unlocker::Status::activationRequired);

            _state.breakCurrentTask.store(_state.currentTask != WorkerTask::none);

            _state.pendingTask = WorkerTask::none;
        }

        _idTokenFile.deleteFile();
        _accessTokenFile.deleteFile();
    }

    void UnlockerImpl::startWorkerIfNeeded() {
        if (!isThreadRunning())
            startThread();
    }

    void UnlockerImpl::updateActivationEventWatcher() {
        const auto shouldWatch = getStatus() == Unlocker::Status::activationRequired;

        if (shouldWatch) {
            if (!isTimerRunning())
                startTimer(activationEventPollMs);
            return;
        }

        stopTimer();
    }

    void UnlockerImpl::triggerActivationEvent() {
        if (ensureInlayDirExists().failed())
            return;

        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("time", juce::Time::currentTimeMillis());
        obj->setProperty("instanceID", _instanceID);

        _activationEventFile.replaceWithText(juce::JSON::toString(juce::var(obj.get())));
    }

    void UnlockerImpl::checkActivationEvent() {
        const auto event = parseActivationEventFromText(_activationEventFile.loadFileAsString());
        if (!event.has_value())
            return;

        if (!shouldReactToActivationEvent(*event, juce::Time::currentTimeMillis()))
            return;

        if (!prepareStartupFromActivationEvent())
            return;

        startWorkerIfNeeded();
        triggerAsyncUpdate();
        _wakeEvent.signal();
    }

    bool UnlockerImpl::taskShouldExit() const {
        if (threadShouldExit())
            return true;

        const juce::ScopedLock lock(_stateCriticalSection);
        return _state.breakCurrentTask.load();
    }

    bool UnlockerImpl::acquireInstanceLock() {
        struct LockData {
            juce::String instanceID;
            juce::int64 expiresAt = 0;
        };

        const auto makeLockData = [](juce::String instanceID, juce::int64 expiresAt) {
            LockData lockData;
            lockData.instanceID = std::move(instanceID);
            lockData.expiresAt = expiresAt;

            return lockData;
        };

        const auto parseLockData = [makeLockData](const juce::String &text) -> std::optional<LockData> {
            if (text.trim().isEmpty())
                return std::nullopt;

            const auto json = juce::JSON::parse(text);
            const auto *obj = json.getDynamicObject();
            if (obj == nullptr)
                return std::nullopt;

            const auto instanceID = readStringProperty(*obj, "instanceID");
            const auto expiresAt = readInt64Property(*obj, "expiresAt");
            if (!instanceID.has_value() || !expiresAt.has_value())
                return std::nullopt;

            return makeLockData(*instanceID, *expiresAt);
        };

        const auto now = juce::Time::currentTimeMillis();
        const auto lockText = _lockFile.loadFileAsString();
        const auto currentLock = parseLockData(lockText);

        if (currentLock.has_value() && now < currentLock->expiresAt)
            return false;

        juce::DynamicObject::Ptr newLockObj = new juce::DynamicObject();
        newLockObj->setProperty("instanceID", _instanceID);
        newLockObj->setProperty("expiresAt", now + instanceLockMs);

        const auto newLockJson = juce::JSON::toString(juce::var(newLockObj.get()));
        if (!_lockFile.replaceWithText(newLockJson))
            return false;

        const auto verifyNow = juce::Time::currentTimeMillis();
        const auto writtenLock = parseLockData(_lockFile.loadFileAsString());
        if (!writtenLock.has_value())
            return false;

        return writtenLock->instanceID == _instanceID && verifyNow < writtenLock->expiresAt;
    }
} // namespace inlay::internal
