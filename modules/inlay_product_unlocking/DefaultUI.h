#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Unlocker.h"

namespace inlay
{

namespace internal
{
enum class AppUpdateDialogAction
{
    later,
    skip,
    openUpdate
};

juce::MessageBoxOptions createAppUpdateDialogOptions (const Unlocker::AppUpdate&, juce::Component*);
AppUpdateDialogAction getAppUpdateDialogActionForNativeMessageBoxResult (int result) noexcept;
} // namespace internal

/** Ready-to-use activation UI for an Unlocker.

    DefaultUI listens to an Unlocker and displays the appropriate activation,
    retry, logout, and update prompts for the current licensing state. Add it to
    a plugin editor or another parent component, size it like any other
    juce::Component, and keep the referenced Unlocker alive for the lifetime of
    this component.
*/
class DefaultUI : public juce::Component,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    /** Creates a UI bound to the supplied Unlocker. */
    explicit DefaultUI (Unlocker&);

    /** Detaches from the Unlocker and destroys the component. */
    ~DefaultUI() override;

    /** Paints the component background. */
    void paint (juce::Graphics&) override;

    /** Lays out the message label and action buttons. */
    void resized() override;

    /** Refreshes the blurred image of the plugin UI beneath this component.

        Call this after changing visible content underneath an already-visible
        overlay. Showing, moving, and resizing the overlay refresh it
        automatically.
    */
    void refreshBackdrop();

    /** Refreshes the blurred backdrop after this component moves. */
    void moved() override;

    /** Refreshes the blurred backdrop after this component is attached to a parent. */
    void parentHierarchyChanged() override;

    /** Refreshes the blurred backdrop whenever the overlay is shown. */
    void visibilityChanged() override;

private:
    void updateBackdrop (bool forceRefresh);
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateContent();
    void updateButtons (const juce::String& primaryText,
                        std::function<void()> primaryAction,
                        const juce::String& secondaryText = {},
                        std::function<void()> secondaryAction = {});
    void notifyAppUpdate();

    Unlocker& _unlocker;
    juce::DropShadowEffect _messageShadow;
    juce::DropShadowEffect _errorShadow;
    juce::Label _messageLabel;
    juce::Label _errorLabel;
    juce::TextButton _primaryButton;
    juce::TextButton _secondaryButton;
    juce::String _shownAppUpdateVersion;
    juce::Image _blurredBackdrop;
    juce::Component* _backdropParent = nullptr;
    juce::Rectangle<int> _backdropBounds;
    bool _isRefreshingBackdrop = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DefaultUI)
};

} // namespace inlay
