# Field 01 Demo

Field 01 is a small JUCE synthesiser plug-in that demonstrates how to integrate
the `inlay_product_unlocking` module and protect a product with Inlay activation.
It is intentionally compact: the UI exposes a few synth parameters while the
processor only produces audio after the product has been unlocked.

## Build integration

The module is connected in [CMakeLists.txt](CMakeLists.txt):

- `inlay::inlay_product_unlocking` is linked to the `Field01Demo` target.
- `juce::juce_audio_utils` provides the JUCE plug-in and UI support.
- `Field01Assets` packages the SVG artwork used by the editor.

The same target builds as a standalone plug-in and a VST3 plug-in.

## Main classes

### `Field01Processor`

[`Source/Field01Processor.h`](Source/Field01Processor.h) and
[`Source/Field01Processor.cpp`](Source/Field01Processor.cpp) implement the
audio processor and own both the plug-in parameters and the activation state.

The processor creates an `inlay::Unlocker` from `makeUnlockerConfig()`:

```cpp
inlay::Unlocker::Config config;
config.productId = productId;
config.publicKey = publicKey;
config.apiURL = "https://api-dev.inlay.cloud";
```

Replace `productId`, `publicKey`, and `apiURL` with the values for your product.
The constructor calls `unlocker.startup()` to restore or validate activation
state. During `processBlock()`, `isUnlocked()` prevents audio generation until
the product is activated.

`getUnlocker()` exposes the same unlocker instance to the editor, and
`getParameters()` exposes the `AudioProcessorValueTreeState` used by the UI
attachments.

### `Field01Editor`

[`Source/Field01Editor.h`](Source/Field01Editor.h) and
[`Source/Field01Editor.cpp`](Source/Field01Editor.cpp) implement the plug-in
editor. The editor:

- attaches JUCE sliders and buttons to the processor parameters;
- renders the custom Field 01 controls and bundled SVG assets;
- hosts `inlay::DefaultUI`, constructed from `processorToEdit.getUnlocker()`.

`DefaultUI` supplies the activation interface. It is added as a child component
and shown over the editor when activation requires user interaction.

### Plug-in entry point

[`Source/Main.cpp`](Source/Main.cpp) defines JUCE's `createPluginFilter()`
factory function. It returns a `Field01Processor` instance when the host loads
the plug-in.

## Activation flow

1. The host creates `Field01Processor` through `createPluginFilter()`.
2. The processor constructs and starts `inlay::Unlocker` with the product
   configuration.
3. The editor receives that unlocker and displays `inlay::DefaultUI` when
   activation is needed.
4. After activation succeeds, `isUnlocked()` allows the processor to generate
   audio.

For a production plug-in, keep the same ownership model: create one unlocker
in the processor, start it during processor setup, pass it to the editor, and
gate the protected product behavior on its unlocked state.
