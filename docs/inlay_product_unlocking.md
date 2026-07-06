# inlay_product_unlocking

`inlay_product_unlocking` is a JUCE module that adds Inlay product activation and local license validation to audio plugins.

The module is intentionally small from the plugin's point of view:

- `inlay::Unlocker` owns license state, activation, persistence, refresh, logout, and update checks.
- `inlay::DefaultUI` is an optional ready-to-use activation overlay for locked products.
- Plugin code can use `unlocker.isLocked()` as a simple realtime-safe guard for protected behaviour.

The main public files are:

- `inlay_product_unlocking.h`
- `Unlocker.h`
- `DefaultUI.h`

Internal API, token validation, network access, callback listener implementation, and module tests live under `internal/`.

## Core Concept

Inlay protects a product by requiring a valid local access token before protected functionality is available. Activation starts in the plugin, continues in the user's browser, and returns to the plugin through a local callback listener. After activation, the module stores local identity and access state under the user's application data directory for the configured Inlay product ID.

At runtime, the unlocker has four states:

| Status | Meaning |
| --- | --- |
| `inlay::Unlocker::Status::undefined` | Initial state before `startup()` has been called. |
| `inlay::Unlocker::Status::activationRequired` | No usable persisted activation is available. The user must activate in the browser. |
| `inlay::Unlocker::Status::unlocking` | Activation, unlock, retry, or refresh work is in progress, or unlocking failed and can be retried. |
| `inlay::Unlocker::Status::unlocked` | Local access is valid and protected functionality can be enabled. |

`Unlocker` inherits from `juce::ChangeBroadcaster`. UI and other message-thread code can register a `juce::ChangeListener` and read `getStatus()`, `getError()`, `getCurrentUser()`, and `getAppUpdate()` when state changes.

For realtime code, use `isLocked()`. It is the only public state method intended for audio-thread use.

## Module Installation

The module requires C++17 or newer and declares these JUCE dependencies:

- `juce_core`
- `juce_cryptography`
- `juce_events`
- `juce_gui_basics`
- `juce_product_unlocking`

### Installing with CMake

Make sure JUCE is available before adding this repository. Inlay reuses the
existing `juce::juce_core` target and does not fetch or vendor JUCE when it is
added to another project.

#### CMake option 1: FetchContent

Add the repository before your `juce_add_plugin` or `juce_add_gui_app` call:

```cmake
include(FetchContent)

FetchContent_Declare(
    inlay_juce_module
    GIT_REPOSITORY https://github.com/inlay-cloud/inlay-juce-module.git
    GIT_TAG        main
)

FetchContent_MakeAvailable(inlay_juce_module)
```

#### CMake option 2: git submodule

Add this repository as a submodule somewhere in your project, for example:

```sh
git submodule add -b main https://github.com/inlay-cloud/inlay-juce-module.git third_party/inlay-juce-module
```

Then add it from your root `CMakeLists.txt` before your plugin or app target:

```cmake
add_subdirectory(third_party/inlay-juce-module EXCLUDE_FROM_ALL)
```

To update the submodule later:

```sh
git submodule update --remote --merge third_party/inlay-juce-module
```

After your `juce_add_plugin` or `juce_add_gui_app` call, link the module target
to your plugin or app target:

```cmake
target_link_libraries(MyPluginTarget
    PRIVATE
        inlay::inlay_product_unlocking)
```

### Installing with Projucer

Download this repository or add it as a git submodule:

```sh
git submodule add -b main https://github.com/inlay-cloud/inlay-juce-module.git modules/inlay-juce-module
```

In Projucer, use "Add a module from a specified folder" and select:

```text
modules/inlay-juce-module/modules/inlay_product_unlocking
```

If you keep the repository somewhere else, select the equivalent
`modules/inlay_product_unlocking` folder from that checkout. After adding the
module, resave the `.jucer` project and rebuild.

## Basic Integration

Include the module from plugin code:

```cpp
#include <inlay_product_unlocking/inlay_product_unlocking.h>
```

Create one unlocker for the protected product. In a plugin, the processor is usually the right owner because it lives independently of editor windows:

```cpp
class MyProcessor : public juce::AudioProcessor
{
public:
    MyProcessor();

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isLocked() const { return _unlocker.isLocked(); }
    bool isUnlocked() const { return ! _unlocker.isLocked(); }

    inlay::Unlocker& getUnlocker() { return _unlocker; }
    const inlay::Unlocker& getUnlocker() const { return _unlocker; }

private:
    inlay::Unlocker _unlocker;
};
```

Configure the unlocker with the Inlay product ID and public key from the Inlay Console project setup:

```cpp
namespace
{
    inlay::Unlocker::Config makeUnlockerConfig()
    {
        inlay::Unlocker::Config config;
        config.productId = "YOUR_INLAY_PRODUCT_ID";
        config.publicKey = "YOUR_COMPANY_PUBLIC_KEY";

        // Leave empty for production. Set only for development or staging.
        // config.apiURL = "https://api-dev.inlay.cloud";

        return config;
    }
}

MyProcessor::MyProcessor()
    : _unlocker (makeUnlockerConfig())
{
    _unlocker.startup();
}
```

The default UI blocks editor interaction, but it does not protect audio or MIDI processing by itself. Guard protected processing with the realtime-safe lock flag:

```cpp
void MyProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                juce::MidiBuffer& midi)
{
    if (_unlocker.isLocked())
    {
        buffer.clear();
        midi.clear();
        return;
    }

    // Protected processing goes here.
}
```

Do not call `getStatus()`, `getError()`, `getCurrentUser()`, or other non-realtime methods from the audio thread.

## Default UI

`inlay::DefaultUI` is a `juce::Component` that listens to an `Unlocker` and shows:

- an Activate button when browser activation is required,
- Unlocking status while work is in progress,
- Retry and Logout buttons when unlocking reports an error,
- an update prompt when Inlay reports a newer app version.

Add it to an editor and keep the referenced unlocker alive for the full lifetime of the component:

```cpp
class MyEditor : public juce::AudioProcessorEditor
{
public:
    explicit MyEditor (MyProcessor& processorToEdit)
        : juce::AudioProcessorEditor (processorToEdit),
          _processor (processorToEdit),
          _defaultUI (processorToEdit.getUnlocker())
    {
        addAndMakeVisible (_mainContent);
        addAndMakeVisible (_defaultUI);

        setSize (600, 400);
    }

    void resized() override
    {
        const auto bounds = getLocalBounds();

        _mainContent.setBounds (bounds);
        _defaultUI.setBounds (bounds);
        _defaultUI.toFront (false);
    }

private:
    MyProcessor& _processor;
    juce::Component _mainContent;
    inlay::DefaultUI _defaultUI;
};
```

The default UI hides itself when the unlocker reaches `Status::unlocked`.

Keep project-specific layout and styling outside this module. Treat `DefaultUI` as the stock implementation, wrap it from the host plugin when needed, and build a custom UI instead of editing module files for product-specific behaviour.

## Custom UI

Build custom UI by listening to the unlocker:

```cpp
class ActivationPanel : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit ActivationPanel (inlay::Unlocker& unlockerToUse)
        : _unlocker (unlockerToUse)
    {
        _unlocker.addChangeListener (this);
        updateContent();
    }

    ~ActivationPanel() override
    {
        _unlocker.removeChangeListener (this);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override
    {
        if (source == &_unlocker)
            updateContent();
    }

    void updateContent()
    {
        switch (_unlocker.getStatus())
        {
            case inlay::Unlocker::Status::activationRequired:
                // Show Activate. Call _unlocker.startActivation().
                break;

            case inlay::Unlocker::Status::unlocking:
                // Show progress, or Retry/Logout when getError() is not empty.
                break;

            case inlay::Unlocker::Status::unlocked:
                // Hide lock UI and enable protected controls.
                break;

            case inlay::Unlocker::Status::undefined:
                break;
        }
    }

    inlay::Unlocker& _unlocker;
};
```

Useful methods for custom UI:

| Method | Use |
| --- | --- |
| `startup()` | Starts loading and validating local license state. Call once after constructing the unlocker. |
| `startActivation()` | Starts browser-based activation. Usually called from an Activate button. |
| `retryUnlocking()` | Retries after an unlocking error. |
| `logout()` | Clears local activation state and returns to activation required. |
| `getStatus()` | Returns the current status for UI/message-thread code. |
| `getError()` | Returns the last user-facing error message. |
| `getCurrentUser()` | Returns the activated user's email address when available. |
| `getAppUpdate()` | Returns available app update information when visible. |
| `skipCurrentAppUpdateVersion()` | Hides the currently visible app update version. |
| `openWebsite(url)` | Opens a URL in the user's default browser. |
| `isLocked()` | Realtime-safe lock check for protected code paths. |

The expected custom UI flow is:

1. `startup()` moves the unlocker out of `Status::undefined`.
2. In `Status::activationRequired`, show Activate and call `startActivation()` from the user's action.
3. In `Status::unlocking`, show progress. If `getError()` is not empty, offer Retry and Logout actions.
4. In `Status::unlocked`, hide blocking UI and enable the protected experience.

## Configuration

`inlay::Unlocker::Config` accepts:

| Field | Required | Meaning |
| --- | --- | --- |
| `productId` | Yes | Inlay product ID for the plugin or application. |
| `publicKey` | Yes | Public key used to verify access tokens for this product and device. |
| `apiURL` | No | API base URL override. Leave empty to use `https://api.inlay.cloud`. |

Use a separate Inlay product ID for each separately licensed product. Do not reuse a host plugin product ID for add-ons or other separately sold content.

## Local Persistence

The module stores activation data below the user's application data directory:

```text
InlayCloud/<productId>/
```

The stored data currently includes:

| File | Purpose |
| --- | --- |
| `id-token` | Persisted identity token used to request fresh access. |
| `access-token` | Persisted access token validated locally before unlocking. |
| `lock.json` | Lightweight cross-instance lock used while refreshing access. |
| `activation-event.json` | Cross-instance activation event used to wake other plugin instances after activation. |
| `update.json` | Cached app update information returned by Inlay. |
| `skipped-update.json` | App update version skipped by the user. |

Calling `logout()` clears the saved identity/access state and moves the unlocker back to `Status::activationRequired`.

## App Updates

Inlay access responses can include application update information. The unlocker caches that information locally and exposes it through `getAppUpdate()` when:

- a version and URL are available,
- the version is not the currently running plugin/app version,
- the user has not skipped that version.

`DefaultUI` shows a native update prompt after unlock. Custom UI can call `getAppUpdate()`, `skipCurrentAppUpdateVersion()`, and `openWebsite()` directly.

## Integration Checklist

1. Add the module to the JUCE project.
2. Include `<inlay_product_unlocking/inlay_product_unlocking.h>`.
3. Create one `inlay::Unlocker` per protected product instance.
4. Provide `productId` and `publicKey` from the Inlay Console.
5. Call `startup()` once after constructing the unlocker.
6. Use `isLocked()` to guard protected realtime/audio behaviour.
7. Add `inlay::DefaultUI` to the editor, or build a custom `ChangeListener` UI.
8. Keep activation and UI methods on the message thread.
9. Add Logout and app-update handling when the plugin needs custom controls.

## Development Notes

- Keep one unlocker per protected product instance.
- Construct `DefaultUI` only after the referenced unlocker exists.
- Call `startup()` once after constructing the unlocker.
- Call `startActivation()`, `retryUnlocking()`, `logout()`, and update-related methods from UI/message-thread code.
- Use `isLocked()` from realtime/audio code.
- Use `ChangeBroadcaster` updates for editor state instead of polling.
- Functions that can fail should return `juce::Result` when adding new public API.
