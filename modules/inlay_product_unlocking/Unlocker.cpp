#include "Unlocker.h"

#include "internal/UnlockerImpl.h"

#include <utility>

namespace inlay {
    Unlocker::Unlocker(const juce::String &productIdToUse, const juce::String &publicKeyToUse)
        : _impl(std::make_unique<internal::UnlockerImpl>(*this, productIdToUse, publicKeyToUse)) {
    }

    Unlocker::Unlocker(Config config)
        : _impl(std::make_unique<internal::UnlockerImpl>(*this, std::move(config))) {
    }

    Unlocker::~Unlocker() = default;

    Unlocker::Status Unlocker::getStatus() const {
        return _impl->getStatus();
    }

    bool Unlocker::isLocked() const {
        return _impl->isLocked();
    }

    juce::String Unlocker::getError() const {
        return _impl->getError();
    }

    juce::String Unlocker::getCurrentUser() const {
        return _impl->getCurrentUser();
    }

    std::optional<Unlocker::AppUpdate> Unlocker::getAppUpdate() const {
        return _impl->getAppUpdate();
    }

    void Unlocker::startup() {
        _impl->startup();
    }

    void Unlocker::startActivation() {
        _impl->startActivation();
    }

    void Unlocker::retryUnlocking() {
        _impl->retryUnlocking();
    }

    void Unlocker::logout() {
        _impl->logout();
    }

    void Unlocker::skipCurrentAppUpdateVersion() {
        _impl->skipCurrentAppUpdateVersion();
    }

    void Unlocker::openWebsite(const juce::String &url) const {
        _impl->openWebsite(url);
    }
} // namespace inlay
