// SPDX-License-Identifier: GPL-3.0-only
// The kit bank page's logic against the firmware's own handler (tests/kit_wire.c).
//     node firmware/fm1/tests/kits_page_test.mjs KIT_WIRE
import assert from "node:assert/strict";
import { execFileSync, spawn } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createInterface } from "node:readline";
import { bankRead, bankSend, kitsFrom, KIT_BYTES, KIT_SLOTS, zipRead, zipWrite } from "../site/kits_core.js";

// the FM-1, as a Web MIDI link. `lose` drops the Nth message sent, as the FM-1 does with one that comes too soon, and
// `late` holds back the reply to the Nth message until the next send, past that send's drain
class Wire {
  constructor(path) {
    this.proc = spawn(path, { stdio: ["pipe", "pipe", "inherit"] });
    this.lines = createInterface({ input: this.proc.stdout })[Symbol.asyncIterator]();
    this.q = []; this.sent = 0; this.lose = new Set(); this.late = new Set(); this.held = null; this.chain = Promise.resolve();
    this.lost = false;
  }
  async say(line) {
    this.proc.stdin.write(line + "\n");
    const replies = [];
    for (;;) {
      const { value } = await this.lines.next();
      if (value === ".") return replies;
      replies.push(Array.from(Buffer.from(value, "hex")));
    }
  }
  drain() { this.q.length = 0; }
  send(bytes) {
    const n = ++this.sent, lost = this.lose.delete(n);
    this.chain = this.chain.then(async () => {
      if (this.held) this.q.push(this.held);
      this.held = null;
      if (lost) return;
      const replies = await this.say(Buffer.from(bytes).toString("hex"));
      if (this.late.delete(n)) this.held = replies[0];
      else this.q.push(...replies);
    });
    return true;
  }
  async read(ms) {
    await this.chain;
    if (this.q.length) return this.q.shift();
    await new Promise((r) => setTimeout(r, Math.min(ms, 5)));
    return null;
  }
  close() { this.proc.stdin.end(); }
}

function kit(n, name = `KIT${n}`, size = 255) {
  const data = new Uint8Array(size).fill(n & 0x7F);
  data.fill(0x20, 0, 8);
  Buffer.from(name).copy(data);
  return data;
}
const names = (r) => r.kits.map((k) => k.path);
const text = (raw) => Buffer.from(raw.subarray(0, 8)).toString("latin1");
const wire = () => new Wire(process.argv[2]);
const rejected = async (work, code) => assert.equal(await work.then(() => "no error", (e) => e.code), code);
const tests = [];
const test = (name, run) => tests.push([name, run]);

test("kits sort by folder, then name", () => {
  const r = kitsFrom([{ path: "b/a.snd", data: kit(1) }, { path: "a/z.SND", data: kit(2) }, { path: "a/B.snd", data: kit(3) }, { path: "a.snd", data: kit(4) }]);
  assert.deepEqual(names(r), ["a/B.snd", "a/z.SND", "a.snd", "b/a.snd"]);
});
test("a kit's second copy is left out, and a saved bank keeps its copies", () => {
  const r = kitsFrom([{ path: "a.snd", data: kit(1) }, { path: "b.snd", data: kit(1) }]);
  assert.deepEqual(names(r), ["a.snd"]);
  assert.equal(r.copies, 1);
  const bank = kitsFrom([{ path: "000-KIT1.snd", data: kit(1) }, { path: "001-KIT1.snd", data: kit(1) }]);
  assert.equal(bank.kits.length, 2);
});
test("macOS's files beside a kit and other files are passed over", () => {
  const r = kitsFrom([{ path: "__MACOSX/a.snd", data: kit(1) }, { path: "._a.snd", data: kit(2) }, { path: "readme.txt", data: kit(3) }]);
  assert.deepEqual([r.kits.length, r.notKits.length], [0, 0]);
});
test("an LXR project's empty slots are left out, unless they came from saving a bank", () => {
  const blank = (n) => { const d = kit(9, "Initkit\0"); d[250] = n; return d; };   // they differ only near the end
  const r = kitsFrom([{ path: "00-LAMBO.SND", data: kit(1, "Lambo") }, { path: "16-INITK.SND", data: blank(1) }, { path: "20-INITK.SND", data: blank(2) }]);
  assert.deepEqual([names(r), r.empties, r.copies], [["00-LAMBO.SND"], 2, 0]);
  assert.equal(kitsFrom([{ path: "016-Initkit.snd", data: blank(1) }]).kits.length, 1);
});
test("a file of the wrong size is named, not sent", () => {
  const r = kitsFrom([{ path: "long.snd", data: kit(1, "L", 257) }, { path: "short.snd", data: kit(2, "S", 254) }, { path: "gone.snd", data: null },
                      { path: "old.snd", data: kit(3, "O", 255) }, { path: "whole.snd", data: kit(4, "W", 256) }]);
  assert.deepEqual(r.notKits, ["gone.snd", "long.snd", "short.snd"]);
  assert.deepEqual(names(r), ["old.snd", "whole.snd"]);
  assert.ok(r.kits.every((k) => k.raw.length === KIT_BYTES));
});
test("a kit with no name takes its file's", () => {
  const blank = kit(1, "");
  const empty = kit(2, "");
  empty[0] = 0xFF;
  const r = kitsFrom([{ path: "dark tom-2.snd", data: blank }, { path: "z_y.snd", data: empty }, { path: "---.snd", data: kit(3, "") }]);
  assert.deepEqual(r.kits.map((k) => text(k.raw)), ["KIT     ", "DARKTOM2", "ZY      "]);
});
test("the bank takes the first 128 and says how many it left out", () => {
  const files = Array.from({ length: 130 }, (_, i) => ({ path: `${String(i).padStart(3, "0")}.snd`, data: kit(i, `K${i}`) }));
  const r = kitsFrom(files);
  assert.deepEqual([r.kits.length, r.over], [KIT_SLOTS, 2]);
  assert.equal(r.kits[127].path, "127.snd");
});

test("a saved bank's .zip reads back", async () => {
  const entries = [{ name: "000-KIT1.snd", data: kit(1) }, { name: "001-KIT2.snd", data: kit(2) }];
  const got = await zipRead(zipWrite(entries), "bank.zip");
  assert.deepEqual(got.map((f) => f.path), ["bank.zip/000-KIT1.snd", "bank.zip/001-KIT2.snd"]);
  assert.deepEqual(got[1].data, entries[1].data);
});
test("a saved bank's .zip passes Python's zipfile check", () => {
  const dir = mkdtempSync(join(tmpdir(), "kits-"));
  try {
    const path = join(dir, "bank.zip");
    execFileSync("python3", ["-c", "import sys; open(sys.argv[1], 'wb').write(bytes.fromhex(sys.argv[2]))", path,
                             Buffer.from(zipWrite([{ name: "000-KIT1.snd", data: kit(1) }])).toString("hex")]);
    const out = execFileSync("python3", ["-c", "import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); print(z.testzip(), z.namelist(), z.getinfo('000-KIT1.snd').file_size)", path]);
    assert.equal(String(out).trim(), "None ['000-KIT1.snd'] 255");
  } finally { rmSync(dir, { recursive: true }); }
});
test("a kit pack's compressed .zip reads, in folders, with other files in it", async () => {
  const dir = mkdtempSync(join(tmpdir(), "kits-"));
  try {
    const path = join(dir, "pack.zip");
    execFileSync("python3", ["-c", `import sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED)
z.writestr("pack/KICKS.SND", b"KICKS   " + bytes(247))
z.writestr("__MACOSX/pack/._KICKS.SND", b"mac")
z.writestr("pack/read me.txt", b"hello")
z.writestr("pack/big.snd", bytes(5000))
z.close()`, path]);
    const r = kitsFrom(await zipRead(new Uint8Array(readFileSync(path)), "pack.zip"));
    assert.deepEqual(names(r), ["pack.zip/pack/KICKS.SND"]);
    assert.deepEqual(r.notKits, ["pack.zip/pack/big.snd"]);
    assert.equal(text(r.kits[0].raw), "KICKS   ");
  } finally { rmSync(dir, { recursive: true }); }
});
test("a damaged .zip is refused", async () => {
  const good = zipWrite([{ name: "a.snd", data: kit(1) }]);
  await rejected(zipRead(new Uint8Array(50), "x.zip"), "zip");
  const flipped = good.slice();
  flipped[40] ^= 1;                           // a byte of the kit, so its check fails
  await rejected(zipRead(flipped, "x.zip"), "zip");
  await rejected(zipRead(good.slice(0, good.length - 30), "x.zip"), "zip");
});

const some = (n) => kitsFrom(Array.from({ length: n }, (_, i) => ({ path: `${i}.snd`, data: kit(i + 1, `K${i}`) }))).kits;
async function on(run) {
  const w = wire();
  try { await run(w); } finally { w.close(); }
}
test("kits sent to the firmware come back byte for byte, under the names `fm1 backup` uses", () => on(async (w) => {
  const kits = some(3);
  await bankSend(w, kits);
  const got = await bankRead(w);
  assert.deepEqual(got.map((f) => f.name), kits.map((k) => `00${names({ kits: [k] })[0].replace(".snd", "")}-${text(k.raw).trim()}.snd`));
  got.forEach((f, i) => assert.deepEqual(f.data, kits[i].raw.subarray(0, 255)));
}));
test("a full bank goes in, and a bank saved and put back is the same", () => on(async (w) => {
  const kits = some(KIT_SLOTS);
  await bankSend(w, kits);
  const first = await bankRead(w);
  assert.equal(first.length, KIT_SLOTS);
  const again = kitsFrom(await zipRead(zipWrite(first), "bank.zip"));
  assert.equal(again.kits.length, KIT_SLOTS);
  await bankSend(w, again.kits);
  assert.deepEqual(await bankRead(w), first);
}));
test("a smaller fill leaves nothing of the bank before it", () => on(async (w) => {
  await bankSend(w, some(5));
  await bankSend(w, some(2));
  assert.equal((await bankRead(w)).length, 2);
}));
test("while playing, nothing is erased and both directions say to stop", () => on(async (w) => {
  await bankSend(w, some(2));
  await w.say("playing 1");
  await rejected(bankSend(w, some(1)), "playing");
  await rejected(bankRead(w), "playing");
  await w.say("playing 0");
  assert.equal((await bankRead(w)).length, 2);
}));
test("a failed erase and a refused write stop the fill, and a refused write says how far it got", () => on(async (w) => {
  await w.say("erase_fails 1");
  await rejected(bankSend(w, some(1)), "flash");
  await w.say("erase_fails 0");
  await w.say("write_fails 2");
  const e = await bankSend(w, some(4)).catch((x) => x);
  assert.deepEqual([e.code, e.sent], ["flash", 2]);
}));
test("a kit that changed on the way is caught by the read-back", () => on(async (w) => {
  await w.say("bend 1");
  const e = await bankSend(w, some(3)).catch((x) => x);
  assert.deepEqual([e.code, e.sent], ["check", 1]);
}));
test("a dropped write and a dropped read go again", () => on(async (w) => {
  w.lose = new Set([3, 8]);                   // the second kit's write, and a read during the check
  await bankSend(w, some(3));
  assert.equal((await bankRead(w)).length, 3);
}));
test("a kit lost behind a late reply is caught by the read-back", () => on(async (w) => {
  // the first kit's write times out and goes again. Its first reply answers the repeat, and the repeat's reply comes
  // after the second kit's send, whose write never arrives
  w.late = new Set([2, 3]);
  w.lose = new Set([4]);
  const e = await bankSend(w, some(3)).catch((x) => x);
  assert.deepEqual([e.code, e.sent], ["check", 1]);
}));

let failed = 0;
for (const [name, run] of tests) {
  try {
    await run();
    console.log(`  ok ${name}`);
  } catch (e) {
    failed++;
    console.log(`FAIL ${name}\n${e.stack}`);
  }
}
process.exit(failed ? 1 : 0);
