// Checks the browser's mirror decoder against a frame produced by the real C
// encoder. Run: node decode_check.mjs vector.txt
import fs from 'node:fs';

const lines = fs.readFileSync(process.argv[2], 'utf8').trim().split(/\r?\n/);
const b64 = lines[0];
const [, packedLen] = lines[1].split(' ').map(Number);
const probes = lines.slice(2).map(l => l.split(',').map(Number));

// This is the decode path from docs/app/js/view.js, verbatim.
const bin = Buffer.from(b64, 'base64').toString('binary');
const packed = new Uint8Array(packedLen);
let o = 0;
for (let i = 0; i + 1 < bin.length && o < packedLen; i += 2) {
  const run = bin.charCodeAt(i);
  const val = bin.charCodeAt(i + 1);
  for (let k = 0; k < run && o < packedLen; k++) packed[o++] = val;
}

const pixel = (x, y) => {
  const i = (y * 160 + x) >> 1;
  return (x & 1) ? (packed[i] & 0x0f) : ((packed[i] >> 4) & 0x0f);
};

console.log(`decoded ${o}/${packedLen} bytes`);
let bad = 0;
for (const [x, y, expect] of probes) {
  const got = pixel(x, y);
  const ok = got === expect;
  if (!ok) bad++;
  console.log(`  [${ok ? 'PASS' : 'FAIL'}] (${x},${y}) expected ${expect}, decoder gave ${got}`);
}
if (o !== packedLen) { console.log('  [FAIL] decoded length mismatch'); bad++; }
console.log(bad ? `\nFAILED (${bad})` : '\nBrowser decoder matches the C encoder.');
process.exit(bad ? 1 : 0);
