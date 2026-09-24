#include "DefaultUI.h"

#include <cmath>

namespace inlay {
    namespace {
        constexpr auto backdropScale = 0.25f;
        constexpr auto backdropBlurRadius = 2.6f;
        constexpr auto backdropBlurKernelSize = 7;
        constexpr auto backdropRefreshIntervalMs = 100;
        constexpr auto buttonHeight = 28;
        constexpr auto buttonWidth = 96;
        constexpr auto buttonGap = 8;
        constexpr auto textGap = 4;
        constexpr auto controlsGap = 10;
        constexpr auto horizontalButtonGap = 8;
        constexpr auto minimumHorizontalButtonLayoutWidth = 232;

        const auto overlayColour = juce::Colour::fromRGBA(7, 9, 13, 122);
        const auto labelColour = juce::Colour::fromRGB(245, 247, 250);
        const auto errorColour = juce::Colour::fromRGB(255, 190, 184);
        const auto primaryButtonColour = juce::Colour::fromRGB(58, 68, 78);
        const auto primaryButtonTextColour = juce::Colour::fromRGB(248, 249, 251);
        const auto secondaryButtonColour = juce::Colour::fromRGB(37, 44, 51);
        const auto secondaryButtonTextColour = juce::Colour::fromRGB(248, 249, 251);

        int getButtonAreaHeight(const int width, const bool hasTwoButtons) {
            return hasTwoButtons && width < minimumHorizontalButtonLayoutWidth
                       ? buttonHeight * 2 + buttonGap
                       : buttonHeight;
        }

        int getPreferredLabelHeight(const juce::Label &label, const int width) {
            if (label.getText().isEmpty() || width <= 0)
                return 0;

            juce::AttributedString attributedText;
            attributedText.setJustification(label.getJustificationType());
            attributedText.append(label.getText(),
                                  label.getFont(),
                                  label.findColour(juce::Label::textColourId));

            juce::TextLayout layout;
            layout.createLayout(attributedText, static_cast<float>(width));
            return juce::roundToInt(std::ceil(layout.getHeight()));
        }
    }

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
        setOpaque(false);
        setAlwaysOnTop(true);

        _messageLabel.setJustificationType(juce::Justification::centredTop);
        _messageLabel.setColour(juce::Label::textColourId, labelColour);
        _messageLabel.setComponentID("inlay-primary-message");
        _messageLabel.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(),
                                                          14.0f,
                                                          juce::Font::bold)));
        _messageLabel.setMinimumHorizontalScale(1.0f);
        _messageLabel.setInterceptsMouseClicks(false, false);

        _errorLabel.setJustificationType(juce::Justification::centredTop);
        _errorLabel.setColour(juce::Label::textColourId, errorColour);
        _errorLabel.setComponentID("inlay-error-message");
        _errorLabel.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(),
                                                        12.0f,
                                                        juce::Font::plain)));
        _errorLabel.setMinimumHorizontalScale(1.0f);
        _errorLabel.setInterceptsMouseClicks(false, false);

        _messageShadow.setShadowProperties(juce::DropShadow(juce::Colours::black.withAlpha(0.72f), 3, {0, 1}));
        _errorShadow.setShadowProperties(juce::DropShadow(juce::Colours::black.withAlpha(0.72f), 3, {0, 1}));
        _messageLabel.setComponentEffect(&_messageShadow);
        _errorLabel.setComponentEffect(&_errorShadow);

        _primaryButton.setColour(juce::TextButton::buttonColourId, primaryButtonColour);
        _primaryButton.setColour(juce::TextButton::buttonOnColourId, primaryButtonColour.brighter(0.08f));
        _primaryButton.setColour(juce::TextButton::textColourOffId, primaryButtonTextColour);
        _primaryButton.setColour(juce::TextButton::textColourOnId, primaryButtonTextColour);

        _secondaryButton.setColour(juce::TextButton::buttonColourId, secondaryButtonColour);
        _secondaryButton.setColour(juce::TextButton::buttonOnColourId, secondaryButtonColour.brighter(0.12f));
        _secondaryButton.setColour(juce::TextButton::textColourOffId, secondaryButtonTextColour);
        _secondaryButton.setColour(juce::TextButton::textColourOnId, secondaryButtonTextColour);

        addAndMakeVisible(_messageLabel);
        addAndMakeVisible(_errorLabel);
        addAndMakeVisible(_primaryButton);
        addAndMakeVisible(_secondaryButton);

        _unlocker.addChangeListener(this);
        updateContent();
    }

    DefaultUI::~DefaultUI() {
        stopTimer();
        _unlocker.removeChangeListener(this);
        _messageLabel.setComponentEffect(nullptr);
        _errorLabel.setComponentEffect(nullptr);
    }

    void DefaultUI::paint(juce::Graphics &g) {
        if (_blurredBackdrop.isValid())
            g.drawImage(_blurredBackdrop, getLocalBounds().toFloat());

        g.fillAll(overlayColour);
    }

    void DefaultUI::resized() {
        auto bounds = getLocalBounds().reduced(16, 8);
        const auto hasTwoButtons = _primaryButton.isVisible() && _secondaryButton.isVisible();
        const auto hasButtons = _primaryButton.isVisible() || _secondaryButton.isVisible();
        const auto shouldStackButtons = hasTwoButtons && getWidth() < minimumHorizontalButtonLayoutWidth;
        const auto hasError = _errorLabel.getText().isNotEmpty();
        const auto preferredMessageHeight = juce::jmax(juce::roundToInt(_messageLabel.getFont().getHeight()),
                                                       getPreferredLabelHeight(_messageLabel, bounds.getWidth()));
        const auto preferredErrorHeight = hasError
                                              ? juce::jmax(juce::roundToInt(_errorLabel.getFont().getHeight()),
                                                           getPreferredLabelHeight(_errorLabel, bounds.getWidth()))
                                              : 0;
        const auto preferredButtonHeight = hasButtons ? getButtonAreaHeight(getWidth(), hasTwoButtons) : 0;
        const auto preferredGaps = (hasError ? textGap : 0) + (hasButtons ? controlsGap : 0);
        const auto preferredGroupHeight = preferredMessageHeight
                                          + preferredErrorHeight
                                          + preferredButtonHeight
                                          + preferredGaps;
        auto group = bounds.withSizeKeepingCentre(bounds.getWidth(),
                                                  juce::jmin(bounds.getHeight(), preferredGroupHeight));

        auto availableTextHeight = juce::jmax(0,
                                              group.getHeight()
                                                  - preferredButtonHeight
                                                  - (hasButtons ? controlsGap : 0)
                                                  - (hasError ? textGap : 0));
        const auto errorHeight = hasError ? juce::jmin(preferredErrorHeight, availableTextHeight) : 0;
        const auto messageHeight = juce::jmax(0, availableTextHeight - errorHeight);

        _messageLabel.setBounds(group.removeFromTop(messageHeight));
        if (hasError) {
            group.removeFromTop(textGap);
            _errorLabel.setBounds(group.removeFromTop(errorHeight));
        } else {
            _errorLabel.setBounds({});
        }

        if (hasButtons)
            group.removeFromTop(controlsGap);

        auto buttonArea = group.removeFromTop(preferredButtonHeight);
        const auto compactButtonWidth = juce::jmax(0, juce::jmin(buttonWidth, bounds.getWidth()));
        if (shouldStackButtons) {
            auto buttonsArea = buttonArea.withSizeKeepingCentre(compactButtonWidth, buttonHeight * 2 + buttonGap);
            _primaryButton.setBounds(buttonsArea.removeFromTop(buttonHeight));
            buttonsArea.removeFromTop(buttonGap);
            _secondaryButton.setBounds(buttonsArea.removeFromTop(buttonHeight));
        } else if (hasTwoButtons) {
            auto buttonsArea = buttonArea.withSizeKeepingCentre(compactButtonWidth * 2 + horizontalButtonGap, buttonHeight);
            _primaryButton.setBounds(buttonsArea.removeFromLeft(compactButtonWidth));
            buttonsArea.removeFromLeft(horizontalButtonGap);
            _secondaryButton.setBounds(buttonsArea.removeFromLeft(compactButtonWidth));
        } else if (_primaryButton.isVisible()) {
            _primaryButton.setBounds(buttonArea.withSizeKeepingCentre(compactButtonWidth, buttonHeight));
        } else if (_secondaryButton.isVisible()) {
            _secondaryButton.setBounds(buttonArea.withSizeKeepingCentre(compactButtonWidth, buttonHeight));
        }

        updateBackdrop(false);
    }

    void DefaultUI::refreshBackdrop() {
        updateBackdrop(true);
    }

    void DefaultUI::moved() {
        updateBackdrop(false);
    }

    void DefaultUI::parentHierarchyChanged() {
        if (isVisible() && getParentComponent() != nullptr) {
            toFront(false);
            updateBackdrop(true);
            startTimer(backdropRefreshIntervalMs);
        } else {
            stopTimer();
        }
    }

    void DefaultUI::visibilityChanged() {
        if (isVisible() && getParentComponent() != nullptr) {
            toFront(false);
            updateBackdrop(true);
            startTimer(backdropRefreshIntervalMs);
        } else {
            stopTimer();
        }
    }

    void DefaultUI::timerCallback() {
        refreshBackdrop();
    }

    void DefaultUI::updateBackdrop(const bool forceRefresh) {
        if (_isRefreshingBackdrop || !isVisible() || getWidth() <= 0 || getHeight() <= 0)
            return;

        auto *parent = getParentComponent();
        if (parent == nullptr) {
            _blurredBackdrop = {};
            _backdropParent = nullptr;
            _backdropBounds = {};
            return;
        }

        if (!forceRefresh
            && _blurredBackdrop.isValid()
            && parent == _backdropParent
            && getBounds() == _backdropBounds)
            return;

        const juce::ScopedValueSetter<bool> refreshingBackdrop(_isRefreshingBackdrop, true);
        const auto previousAlpha = getAlpha();
        const auto minimumDimension = static_cast<float>(juce::jmin(getWidth(), getHeight()));
        const auto captureScale = juce::jmax(backdropScale, 1.0f / minimumDimension);

        setAlpha(0.0f);
        auto backdrop = parent->createComponentSnapshot(getBounds(), true, captureScale);
        setAlpha(previousAlpha);

        if (!backdrop.isValid())
            return;

        const auto source = backdrop.createCopy();
        juce::ImageConvolutionKernel blurKernel(backdropBlurKernelSize);
        blurKernel.createGaussianBlur(backdropBlurRadius);
        blurKernel.applyToImage(backdrop, source, backdrop.getBounds());

        _blurredBackdrop = std::move(backdrop);
        _backdropParent = parent;
        _backdropBounds = getBounds();
        repaint();
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
                _messageLabel.setText("Activation required", juce::dontSendNotification);
                _errorLabel.setText(error,
                                    juce::dontSendNotification);
                updateButtons("Activate", [this] { _unlocker.startActivation(); });
                break;

            case Status::unlocking:
                setVisible(true);
                _shownAppUpdateVersion = {};
                if (error.isEmpty()) {
                    _messageLabel.setText("Unlocking...", juce::dontSendNotification);
                    _errorLabel.setText({}, juce::dontSendNotification);
                    updateButtons({}, {});
                } else {
                    _messageLabel.setText("Unable to unlock this app.", juce::dontSendNotification);
                    _errorLabel.setText(error, juce::dontSendNotification);
                    updateButtons("Retry",
                                  [this] { _unlocker.retryUnlocking(); },
                                  "Logout",
                                  [this] { _unlocker.logout(); });
                }
                break;

            case Status::unlocked:
                setVisible(false);
                _messageLabel.setText({}, juce::dontSendNotification);
                _errorLabel.setText({}, juce::dontSendNotification);
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
