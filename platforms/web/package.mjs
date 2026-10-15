import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createIcon } from './icons.mjs';

const shellDirectory = path.dirname(fileURLToPath(import.meta.url));
const shellFiles = ['index.html', 'bootstrap.mjs', 'name-input.mjs', 'persistence.mjs', 'layout.mjs', 'locale.mjs', 'shell.css', 'manifest.webmanifest', 'icon.svg', 'sw-runtime.js'];
const gameFiles = ['SpaceCadetPinball.js', 'SpaceCadetPinball.wasm', 'SpaceCadetPinball.data'];
const hash = value => createHash('sha256').update(value).digest('hex');

export async function packageWeb({ buildDirectory, outputDirectory, sourceVersion }) {
  const buildRoot = path.resolve(buildDirectory);
  const outputRoot = path.resolve(outputDirectory);
  if (!sourceVersion?.trim()) throw new Error('A source version is required.');
  if (outputRoot === buildRoot || outputRoot === shellDirectory || shellDirectory.startsWith(outputRoot + path.sep)) throw new Error('Output must be a separate package directory.');
  const entries = [];
  // Read every required file before changing the output directory.
  for (const name of [...shellFiles, ...gameFiles]) {
    const bytes = await fs.readFile(path.join(gameFiles.includes(name) ? buildRoot : shellDirectory, name));
    if (!bytes.length) throw new Error(`Empty required file: ${name}`);
    entries.push({ name, bytes });
  }
  for (const size of [192, 512]) entries.push({ name: `icon-${size}.png`, bytes: createIcon(size) });
  const assets = entries.map(({ name, bytes }) => ({ url: name, size: bytes.length, sha256: hash(bytes) })).sort((a, b) => a.url.localeCompare(b.url));
  const version = hash(JSON.stringify({ sourceVersion, assets }));
  const manifest = { schema: 1, version, sourceVersion, assets };
  const worker = `/* Generated immutable asset set. */\nconst PINBALL_BUILD_VERSION = '${version}';\nimportScripts('./sw-runtime.js');\n`;
  // Packages are immutable: a different build gets a new directory, never partial replacement.
  await fs.mkdir(outputRoot, { recursive: true });
  const currentFiles = await fs.readdir(outputRoot);
  if (currentFiles.length) {
    const currentManifest = JSON.parse(await fs.readFile(path.join(outputRoot, 'asset-manifest.json'), 'utf8'));
    if (currentManifest.version !== version) throw new Error('Output contains another version. Use a new directory.');
    for (const asset of assets) {
      if (hash(await fs.readFile(path.join(outputRoot, asset.url))) !== asset.sha256) throw new Error(`Existing package is damaged: ${asset.url}`);
    }
    if (await fs.readFile(path.join(outputRoot, 'sw.js'), 'utf8') !== worker) throw new Error('Existing service worker is damaged.');
    return manifest;
  }
  for (const { name, bytes } of entries) await fs.writeFile(path.join(outputRoot, name), bytes, { flag: 'wx' });
  await fs.writeFile(path.join(outputRoot, 'sw.js'), worker, { flag: 'wx' });
  await fs.writeFile(path.join(outputRoot, 'asset-manifest.json'), JSON.stringify(manifest, null, 2) + '\n', { flag: 'wx' });
  return manifest;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const flags = new Map();
  for (let i = 2; i < process.argv.length; i += 2) flags.set(process.argv[i], process.argv[i + 1]);
  if (!flags.get('--build-dir') || !flags.get('--output') || !flags.get('--source-version')) {
    console.error('Usage: node platforms/web/package.mjs --build-dir <emcc output> --output <new package directory> --source-version <engine commit>');
    process.exitCode = 1;
  } else {
    try {
      const manifest = await packageWeb({ buildDirectory: flags.get('--build-dir'), outputDirectory: flags.get('--output'), sourceVersion: flags.get('--source-version') });
      console.log(JSON.stringify({ version: manifest.version, sourceVersion: manifest.sourceVersion, assets: manifest.assets.length, output: path.resolve(flags.get('--output')) }));
    } catch (error) {
      console.error(error.message);
      process.exitCode = 1;
    }
  }
}
