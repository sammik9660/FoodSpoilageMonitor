import fs from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const pkg = 'C:/Program Files/WindowsApps/OpenAI.CodexPrimaryRuntime.v26-1112-737-0_26.1112.737.0_x64__3k8sg7r9htsxt/dependencies/node/node_modules/@oai/artifact-tool/dist/artifact_tool.mjs';
const { FileBlob, PresentationFile } = await import(pathToFileURL(pkg).href);
const root = 'C:/Users/user/Desktop/FoodSpoilageMonitor/deliverables/presentation_20261118';
const source = 'C:/Users/user/Documents/카카오톡 받은 파일/스마트음식물보관용기_최종발표.pptx';
const deck = await PresentationFile.importPptx(await FileBlob.load(source));
console.log('slides', deck.slides.items.length);
for (let i = 0; i < deck.slides.items.length; i++) {
  const image = await deck.export({ slide: deck.slides.items[i], format: 'png', scale: 0.5 });
  await fs.writeFile(path.join(root, 'build', 'source-render', `slide-${String(i + 1).padStart(2, '0')}.png`), new Uint8Array(await image.arrayBuffer()));
  if ((i + 1) % 5 === 0) console.log('rendered', i + 1);
}
