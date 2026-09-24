const fs = require('node:fs');
const path = require('node:path');
const { Resvg } = require('@resvg/resvg-js');
const root = path.resolve(__dirname, '..');
for (const [input, output, width] of [
  ['field-01-preview.svg', 'field-01-preview.png', 2160],
  ['field-01-preview.svg', 'field-01-preview-1x.png', 1080],
  ['asset-sheet.svg', 'asset-sheet.png', 1620],
]) {
  const png = new Resvg(fs.readFileSync(path.join(root, input)), {
    fitTo: { mode: 'width', value: width },
  }).render().asPng();
  fs.writeFileSync(path.join(root, output), png);
  console.log(`${output}: ${width}px wide`);
}
