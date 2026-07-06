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
                  private juce::ChangeListener
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

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void updateContent();
    void updateButtons (const juce::String& primaryText,
                        std::function<void()> primaryAction,
                        const juce::String& secondaryText = {},
                        std::function<void()> secondaryAction = {});
    void notifyAppUpdate();

    Unlocker& _unlocker;
    juce::Label _messageLabel;
    juce::TextButton _primaryButton;
    juce::TextButton _secondaryButton;
    juce::String _shownAppUpdateVersion;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DefaultUI)
};

} // namespace inlay
