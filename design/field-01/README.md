# FIELD / 01

Original vintage oscillator interface for the Inlay demo plugin. A warm ivory instrument panel, olive dials and phosphor-green scope, with six controls. The reference informed the tactile vintage mood only; its layout, orange accents, sculpted knobs, symbols and decorative motifs were not reproduced.

## Deliverables

- `field-01-preview.png`: final 2160 × 1400 preview (2×).
- `field-01-preview-1x.png`: final 1080 × 700 preview (native size).
- `field-01-preview.svg`: assembled, scalable artwork with the texture embedded.
- `asset-sheet.png`: visual inventory of the reusable parts and states.
- `assets/`: 18 independent SVG files and one generated PNG material.
- `layout.json`: pixel bounds, layer order, hit areas, dial pivots, state names and colours.
- `source/`: editable generator and raster exporter.

The previews are composed from the supplied assets, not a separate concept rendering. This is an artwork handoff; the plugin editor and DSP have not been changed.

## Controls

| Parameter | Treatment | Intended role |
| --- | --- | --- |
| Waveform | Three-position selector | Sine, triangle or pulse |
| Range | Two-position slide switch | Low-frequency or audio-rate oscillator |
| Frequency | Sage rotary dial | Oscillator frequency |
| Drift | Charcoal rotary dial | Small pitch variations |
| Colour | Charcoal rotary dial | Harmonic colour / saturation |
| Level | Charcoal rotary dial | Output level |

The parameter semantics are proposed for the demo. Preview numbers and pointer positions are illustrative; the eventual DSP mapping should supply both from the same normalized value.

## Asset inventory

| Files | Native dimensions | Use |
| --- | --- | --- |
| `panel.svg` | 1080 × 700 | Case and enamel base |
| `panel-lettering.svg` | 1080 × 700 | Fixed outlined labels, branding and separators |
| `enamel-texture.png` | 1536 × 1024 | Optional raster paint material, draw at 16% opacity inside the faceplate |
| `fastener.svg` | 20 × 20 | Reuse at four panel corners |
| `scope-bezel.svg` | 650 × 246 | Scope surround |
| `scope-glass-grid.svg` | 622 × 217 | Glass and static graticule |
| `scope-trace-{sine,triangle,pulse}.svg` | 622 × 217 | Example waveforms; replace with a live path in the plugin |
| `scope-readout-example.svg` | 622 × 217 | Example readout overlay; replace with live text |
| `knob-{sage,charcoal}.svg` | 160 × 160 | Fixed dial material and lighting |
| `knob-scale.svg` | 160 × 160 | Fixed 270° calibration ticks |
| `knob-index.svg` | 160 × 160 | Rotating pointer only |
| `wave-selector-{sine,triangle,pulse}.svg` | 274 × 57 | All three selection states |
| `range-{low,audio}.svg` | 90 × 40 | Both switch states |

SVGs use paths, basic shapes, gradients and opacity. No SVG filters, external fonts, scripts or external image dependencies appear in the individual SVG assets. All static lettering is outlined; no font files are bundled. The assembled preview embeds the separate texture for portability.

## JUCE handoff

Use `juce_add_binary_data` or Projucer BinaryData for the asset files. Load SVGs with `juce::Drawable::createFromImageData`, and the material PNG with `juce::ImageCache::getFromMemory`. Keep drawables cached instead of parsing them in `paint()`.

1. Use a 1080 × 700 logical coordinate system and a single uniform scale when resizing.
2. Draw the panel, texture (opacity 0.16), fixed lettering, fasteners, scope and controls in the order shown in the preview. Texture bounds: `[10, 10, 1060, 674]`.
3. For each knob, draw the scale, then the body. Rotate only the index around local `(80, 80)`. The angle is `-135 + 270 * normalizedValue` degrees, clockwise from the 12 o'clock pointer. This corresponds to 7:30 through 4:30 on the clock face.
4. Draw the waveform as a runtime `juce::Path`, clipped to the trace region. The separate SVG traces are examples and useful static fallback images. Draw readouts separately so labels and values stay synchronized with parameters.
5. Use the manifest's hit areas, with drag, wheel, keyboard, double-click reset and parameter attachments implemented in code. The wave selector is one choice parameter with three hit segments; the range switch is one boolean/choice parameter.
6. Keep hover/focus/disabled feedback procedural: subtle outline on hover, a visible focus ring, and reduced control opacity when disabled. Do not rotate the gradients, scale ticks or painted lettering.

Native SVG compatibility has been kept simple, but the assets still need verification in the final JUCE editor. The supplied previews were rendered using resvg.

## Rebuilding

The committed assets can be used directly. To edit and rebuild on macOS:

```sh
python3 -m venv /tmp/field-art-env
/tmp/field-art-env/bin/pip install fonttools
/tmp/field-art-env/bin/python source/build_assets.py
npm install --prefix source
node source/render.cjs
```

Run these commands from this directory. The generator uses installed Arial, Arial Bold and Andale Mono from `/System/Library/Fonts/Supplemental/` to outline the lettering. Adjust those paths if using another environment. `render.cjs` only converts the authored SVGs to preview PNGs; it does not generate or alter the material texture.

## Raster provenance

`enamel-texture.png` was generated with the built-in image-generation tool. All other artwork was authored as vectors. Exact prompt:

> Generate a production raster MATERIAL TEXTURE asset, not a product mockup. Flat orthographic macro view of a clean warm ivory painted enamel surface from a well cared for 1970s laboratory instrument. Uniform pale ivory #d8d3bf overall, very fine subtle irregular paint grain, barely perceptible organic tonal variation. Extremely low contrast, matte finish, even diffuse lighting across entire surface, edge-to-edge. 1536x1024 landscape. No vignette, no shadows, no objects, no controls, no letters, no scratches, no stains, no borders, no decorative motifs. This texture will be a faint optional material overlay behind separately drawn vector UI controls.
