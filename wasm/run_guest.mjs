// フリースタンディングゲスト (mojitonpp_guest.wasm) のスモークテスト兼ホスト側手順の例。
// wasm3 ホストでも同じ手順 (バッファ確保 → 入力書き込み → 関数呼び出し → メモリ読み取り) を踏む。
//
// 使い方: node run_guest.mjs <guest.wasm> [input]
import { readFileSync } from 'node:fs';

const wasmPath = process.argv[2];
if (wasmPath === undefined) {
  console.error('usage: node run_guest.mjs <guest.wasm> [input]');
  process.exit(1);
}
const input = process.argv[3] ?? 'img_002.png\nimg_001.png\nimg_010.png\nimg_003.png\nimg_004.png\nimg_005.png\nimg_006.png\nimg_007.png\nimg_008.png\nimg_009.png\nmeta.txt';

const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const { exports } = instance;

// 1. 入力バッファの確保と書き込み
const bytes = Buffer.from(input, 'utf8');
const scratch = Number(exports.mojiton_scratch(bytes.length));
new Uint8Array(exports.memory.buffer).set(bytes, scratch);

// 2. 検出の実行
const outPtr = Number(exports.mojiton_detect(scratch, bytes.length));

// 3. 結果の読み取り (memory.grow の可能性があるため再取得)
const memory = new Uint8Array(exports.memory.buffer);
const outEnd = memory.indexOf(0, outPtr);
const result = Buffer.from(memory.slice(outPtr, outEnd)).toString('utf8');
console.log(result.trimEnd());

// 検証: 11個中10個が img_NNN.png (10/11 ≒ 0.909 >= 閾値0.9)、meta.txt は除外される
const files = Array.from({ length: 10 }, (_, i) => `img_${String(i + 1).padStart(3, '0')}.png`);
const expected = `img_|${files.join('|')}\n`;
if (result !== expected) {
  console.error(`MISMATCH\nexpected: ${JSON.stringify(expected)}`);
  process.exit(1);
}
console.error('smoke test OK');
