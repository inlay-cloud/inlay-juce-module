#include "DefaultUI.h"

namespace inlay {
    namespace internal {
        juce::MessageBoxOptions createAppUpdateDialogOptions(const Unlocker::AppUpdate &appUpdate,
                                                             juce::Component *associatedComponent) {
            return juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("Update Available")
                    .withMessage("Version " + appUpdate.version + " is available. Update the current app now?")
                    .withButton("Later")
                    .withButton("Skip")
                    .withButton("Go to Update")
                    .withAssociatedComponent(associatedComponent);
        }

        AppUpdateDialogAction getAppUpdateDialogActionForNativeMessageBoxResult(const int result) noexcept {
            switch (result) {
                case 1:
                    return AppUpdateDialogAction::skip;
                case 2:
                    return AppUpdateDialogAction::openUpdate;
                default:
                    return AppUpdateDialogAction::later;
            }
        }
    } // namespace internal

    DefaultUI::DefaultUI(Unlocker &unlockerToUse)
        : _unlocker(unlockerToUse) {
        setOpaque(true);

        _messageLabel.setJustificationType(juce::Justification::centredTop);
        _messageLabel.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId));
        _messageLabel.setInterceptsMouseClicks(false, false);

        addAndMakeVisible(_messageLabel);
        addAndMakeVisible(_primaryButton);
        addAndMakeVisible(_secondaryButton);

        _unlocker.addChangeListener(this);
        updateContent();
    }

    DefaultUI::~DefaultUI() {
        _unlocker.removeChangeListener(this);
    }

    void DefaultUI::paint(juce::Graphics &g) {
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
    }

    void DefaultUI::resized() {
        auto bounds = getLocalBounds();
        auto buttonArea = bounds.removeFromBottom(32);
        _messageLabel.setBounds(getLocalBounds());

        if (_primaryButton.isVisible() && _secondaryButton.isVisible()) {
            auto buttonsArea = buttonArea.withSizeKeepingCentre(248, 32);
            _primaryButton.setBounds(buttonsArea.removeFromLeft(120));
            buttonsArea.removeFromLeft(8);
            _secondaryButton.setBounds(buttonsArea.removeFromLeft(120));
        } else if (_primaryButton.isVisible()) {
            _primaryButton.setBounds(buttonArea.withSizeKeepingCentre(120, 32));
        } else if (_secondaryButton.isVisible()) {
            _secondaryButton.setBounds(buttonArea.withSizeKeepingCentre(120, 32));
        }
    }

    void DefaultUI::changeListenerCallback(juce::ChangeBroadcaster *source) {
        if (source == &_unlocker)
            updateContent();
    }

    void DefaultUI::updateContent() {
        juce::Logger::writeToLog("DefaultUI::updateContent()");
        using Status = Unlocker::Status;

        const auto error = _unlocker.getError();
        const auto appUpdate = _unlocker.getAppUpdate();

        switch (_unlocker.getStatus()) {
            case Status::activationRequired:
                setVisible(true);
                _shownAppUpdateVersion = {};
                _messageLabel.setText(error.isEmpty()
                                          ? "This app needs activation. Click Activate and follow the browser instructions."
                                          : "This app needs activation. Click Activate and follow the browser instructions.\n\nActivation error: " + error,
                                      juce::dontSendNotification);
                updateButtons("Activate", [this] { _unlocker.startActivation(); });
                break;

            case Status::unlocking:
                setVisible(true);
                _shownAppUpdateVersion = {};
                if (error.isEmpty()) {
                    _messageLabel.setText("Unlocking...", juce::dontSendNotification);
                    updateButtons({}, {});
                } else {
                    _messageLabel.setText("Unlocking error: " + error, juce::dontSendNotification);
                    updateButtons("Retry",
                                  [this] { _unlocker.retryUnlocking(); },
                                  "Logout",
                                  [this] { _unlocker.logout(); });
                }
                break;

            case Status::unlocked:
                setVisible(false);
                updateButtons({}, {});
                if (appUpdate.has_value()) {
                    notifyAppUpdate();
                } else {
                    _shownAppUpdateVersion = {};
                }
                break;
        }

        resized();
        repaint();
    }

    void DefaultUI::updateButtons(const juce::String &primaryText,
                                     std::function<void()> primaryAction,
                                     const juce::String &secondaryText,
                                     std::function<void()> secondaryAction) {
        _primaryButton.setButtonText(primaryText);
        _primaryButton.onClick = std::move(primaryAction);
        _primaryButton.setVisible(primaryText.isNotEmpty());

        _secondaryButton.setButtonText(secondaryText);
        _secondaryButton.onClick = std::move(secondaryAction);
        _secondaryButton.setVisible(secondaryText.isNotEmpty());
    }

    void DefaultUI::notifyAppUpdate() {
        juce::Logger::writeToLog("DefaultUI::notifyAppUpdate");
        const auto appUpdate = _unlocker.getAppUpdate();
        if (!appUpdate.has_value() || appUpdate->version == _shownAppUpdateVersion)
            return;

        _shownAppUpdateVersion = appUpdate->version;

        auto options = internal::createAppUpdateDialogOptions(*appUpdate, this);

        juce::NativeMessageBox::showAsync(options,
                                          juce::ModalCallbackFunction::create(
                                              [safeThis = juce::Component::SafePointer<DefaultUI>(this)](int result) {
                                                  if (safeThis == nullptr)
                                                      return;

                                                  switch (internal::getAppUpdateDialogActionForNativeMessageBoxResult(result)) {
                                                      case internal::AppUpdateDialogAction::openUpdate:
                                                          if (const auto update = safeThis->_unlocker.getAppUpdate())
                                                              safeThis->_unlocker.openWebsite(update->url);
                                                          break;
                                                      case internal::AppUpdateDialogAction::skip:
                                                          safeThis->_unlocker.skipCurrentAppUpdateVersion();
                                                          break;
                                                      case internal::AppUpdateDialogAction::later:
                                                          break;
                                                  }
                                              }));
    }
} // namespace inlay
