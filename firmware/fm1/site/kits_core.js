/* SPDX-License-Identifier: GPL-3.0-only */
/* The kit bank page's logic, without the page: which files are kits, .zip files, and the bank's SysEx (app/kit_sysex.c,
 * as tools/fm1_kits.py speaks it). tests/kits_page_test.mjs runs this against the firmware's handler. make_web.py inlines
 * it after fm1ota.js, in one scope, so names here must not repeat that file's. */
const KT_HEAD = [0x7D, 0x4B, 0x54];
export const KIT_SLOTS = 128, KIT_BYTES = 256, KIT_CHUNK = 64;
const KIT_SIZES = [255, 256];                 // what the LXR writes, and a whole slot. Any other .snd is audio, not a kit
const KIT_SAVED = 255;                        // what the LXR writes, and tools/fm1_kits.py --save
const BANK_FILE = /^\d{3}-.*\.snd$/i;         // what saving the bank writes
const EMPTY_KIT = /^initkit[\0 ]*$/i;         // an LXR project's unused slot. Their last few bytes differ, so copies don't catch them

/* sent: the kits that arrived, on a failure after the bank was erased */
export function kitFail(code, message, sent) { return Object.assign(new Error(message), { code, sent }); }

const isSnd = (path) => /\.snd$/i.test(path);
const leaf = (path) => path.split("/").pop();
function kitPathOrder(a, b) {                 // as Python sorts paths: folder by folder, by code point
  const x = a.path.split("/"), y = b.path.split("/");
  for (let i = 0; i < Math.min(x.length, y.length); i++)
    if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
  return x.length - y.length;
}

/* A kit needs a name: a blank one can't be seen in the bank, and 0xFF first reads as an empty slot, which PRESET stops at */
function kitNamed(raw, stem) {
  if (raw[0] !== 0xFF && raw.subarray(0, 8).some((b) => b !== 0x20 && b !== 0)) return raw;
  const name = (stem.toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 8) || "KIT").padEnd(8);
  const out = raw.slice();
  for (let i = 0; i < 8; i++) out[i] = name.charCodeAt(i);
  return out;
}

/* files: [{path, data}], data null when it could not be read -> what goes in the bank, as tools/fm1_kits.py picks it:
 * sorted by path, one of each kit unless the files came from saving a bank, where the copies hold their slots */
export function kitsFrom(files) {
  const seen = new Set(), found = [], notKits = [];
  let copies = 0, empties = 0;
  for (const f of files.slice().sort(kitPathOrder)) {
    const base = leaf(f.path);
    if (!isSnd(base) || base.startsWith("._") || f.path.split("/").includes("__MACOSX")) continue;
    if (!f.data || f.data.length < KIT_SIZES[0] || f.data.length > KIT_SIZES[1]) { notKits.push(f.path); continue; }
    if (!BANK_FILE.test(base) && EMPTY_KIT.test(String.fromCharCode(...f.data.subarray(0, 8)))) { empties++; continue; }
    const same = f.data.join();
    if (seen.has(same) && !BANK_FILE.test(base)) { copies++; continue; }
    seen.add(same);
    const raw = new Uint8Array(KIT_BYTES);
    raw.set(f.data);
    found.push({ path: f.path, raw: kitNamed(raw, base.replace(/\.[^.]*$/, "")) });
  }
  return { kits: found.slice(0, KIT_SLOTS), notKits, copies, empties, over: Math.max(0, found.length - KIT_SLOTS) };
}

/* ------------------------------------------------------------- .zip: kit packs arrive as one, and the saved bank is one */
const ZIP_CRC = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
function zipCrc(data) {
  let c = 0xFFFFFFFF;
  for (const b of data) c = ZIP_CRC[(c ^ b) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
async function zipInflate(raw) {
  const stream = new Blob([raw]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

/* a .zip -> its .snd files as [{path, data}]. A kit that can't be read (encrypted, or too big to be a kit) has data null */
export async function zipRead(bytes, prefix) {
  const damaged = () => kitFail("zip", `${prefix} is damaged, or is not a .zip file.`);
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
  let end = -1;
  for (let i = bytes.length - 22; i >= 0 && i >= bytes.length - 22 - 0xFFFF && end < 0; i--)
    if (v.getUint32(i, true) === 0x06054B50) end = i;
  if (end < 0) throw damaged();
  const out = [], names = new TextDecoder();
  try {
    let at = v.getUint32(end + 16, true);
    for (let k = v.getUint16(end + 10, true); k > 0; k--) {
      if (v.getUint32(at, true) !== 0x02014B50) throw damaged();
      const flags = v.getUint16(at + 8, true), method = v.getUint16(at + 10, true), crc = v.getUint32(at + 16, true);
      const packed = v.getUint32(at + 20, true), size = v.getUint32(at + 24, true), local = v.getUint32(at + 42, true);
      const nameLen = v.getUint16(at + 28, true);
      const name = names.decode(bytes.subarray(at + 46, at + 46 + nameLen)).replace(/\\/g, "/");
      at += 46 + nameLen + v.getUint16(at + 30, true) + v.getUint16(at + 32, true);
      if (!isSnd(name)) continue;
      const path = `${prefix}/${name}`;
      if ((flags & 1) || (method !== 0 && method !== 8) || size > KIT_SIZES[1]) { out.push({ path, data: null }); continue; }
      if (v.getUint32(local, true) !== 0x04034B50) throw damaged();
      const start = local + 30 + v.getUint16(local + 26, true) + v.getUint16(local + 28, true);
      const raw = bytes.subarray(start, start + packed);
      const data = method ? await zipInflate(raw) : raw.slice();
      if (zipCrc(data) !== crc) throw damaged();
      out.push({ path, data });
    }
  } catch (e) {
    throw e.code === "zip" ? e : damaged();
  }
  return out;
}

/* [{name, data}] -> a .zip, stored without compression, as 256 bytes barely shrink */
export function zipWrite(entries) {
  const le = (n, v) => Array.from({ length: n }, (_, i) => (v >>> (8 * i)) & 0xFF);
  const parts = [], dir = [];
  let at = 0;
  for (const { name, data } of entries) {
    const path = Array.from(name, (c) => c.charCodeAt(0) & 0x7F);
    const same = [...le(2, 20), 0, 0, 0, 0, 0, 0, 0x21, 0, ...le(4, zipCrc(data)), ...le(4, data.length), ...le(4, data.length),
                  ...le(2, path.length), 0, 0];       // the date is the format's earliest, so a saved bank's bytes depend on the kits only
    const head = [0x50, 0x4B, 3, 4, ...same, ...path];
    parts.push(Uint8Array.from(head), data);
    dir.push(0x50, 0x4B, 1, 2, ...le(2, 20), ...same, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...le(4, at), ...path);
    at += head.length + data.length;
  }
  parts.push(Uint8Array.from(dir), Uint8Array.from([0x50, 0x4B, 5, 6, 0, 0, 0, 0, ...le(2, entries.length), ...le(2, entries.length),
                                                    ...le(4, dir.length), ...le(4, at), 0, 0]));
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

/* ---------------------------------------------------------------------------------------------- the bank over SysEx */
const stopFirst = (sent) => kitFail("playing", `Tack is playing. Press PLAY on the FM-1 to stop it, then try again.${sent === undefined ? " Nothing changed." : ""}`, sent);

/* The FM-1 drops a SysEx that arrives while it still holds the one before, about one read in 3000, so a request that
 * can be repeated goes again. A reply that answers it starts with the first `same` bytes of the body, unless the status
 * says the FM-1 refused it: those replies carry nothing else */
async function kitAsk(link, cmd, body = [], { same = 0, wait = 5000, tries = 1 } = {}) {
  const message = [0xF0, ...KT_HEAD, cmd, ...body, 0xF7];
  for (let t = 0; t < tries; t++) {
    link.drain();
    if (!link.send(message)) break;
    for (const end = Date.now() + wait; ;) {
      const p = await link.read(end - Date.now());
      if (!p) break;
      if (p[1] !== KT_HEAD[0] || p[2] !== KT_HEAD[1] || p[3] !== KT_HEAD[2] || p[4] !== 0x7F || p[5] !== cmd) continue;
      const refused = p[6] !== 0 && p[6] !== 4;
      if (refused || body.slice(0, same).every((b, i) => p[7 + i] === b)) return p;
    }
    if (link.lost) break;
  }
  throw kitFail(link.lost ? "lost" : "silent", link.lost ? "The FM-1 was disconnected." : "The FM-1 stopped answering. Check that it runs Tack, and try again.");
}

/* a slot -> its 256 bytes, or null when it is empty */
async function slotRead(link, slot, sent) {
  const raw = new Uint8Array(KIT_BYTES);
  for (let part = 0; part < KIT_BYTES / KIT_CHUNK; part++) {
    const p = await kitAsk(link, 3, [slot, part], { same: 2, wait: 250, tries: 8 });
    if (p[6] === 1) throw stopFirst(sent);
    if (p[6] === 4) return null;
    if (p[6] !== 0 || p.length !== 10 + 2 * KIT_CHUNK) throw kitFail("read", `The FM-1 sent slot ${slot + 1} in a form this page can't read.`, sent);
    for (let i = 0; i < KIT_CHUNK; i++)
      raw[part * KIT_CHUNK + i] = parseInt(String.fromCharCode(p[9 + 2 * i], p[10 + 2 * i]), 16);
  }
  return raw;
}

/* the bank -> [{name, data}], the files `fm1 backup` writes: slot number, then the kit's name */
export async function bankRead(link, onProgress = () => {}) {
  const files = [];
  for (let slot = 0; slot < KIT_SLOTS; slot++) {
    const raw = await slotRead(link, slot);
    onProgress((slot + 1) / KIT_SLOTS);
    if (!raw) continue;
    const name = String.fromCharCode(...raw.subarray(0, 8)).replace(/[^A-Za-z0-9]/g, "");
    files.push({ name: `${String(slot).padStart(3, "0")}-${name}.snd`, data: raw.subarray(0, KIT_SAVED) });
  }
  return files;
}

/* Erases the bank, writes the kits and reads them all back. Writes must be read back: their replies don't name a slot,
 * so a late reply to a repeated write can pass for the next write's, and hide a kit that never arrived */
export async function bankSend(link, kits, onProgress = () => {}) {
  const erased = (await kitAsk(link, 1))[6];
  if (erased === 1) throw stopFirst();
  if (erased) throw kitFail("flash", "The FM-1 could not erase its kit bank.");
  for (let i = 0; i < kits.length; i++) {
    const hex = Array.from(kits[i].raw, (b) => b.toString(16).toUpperCase().padStart(2, "0")).join("");
    const body = [i, ...Array.from(hex, (c) => c.charCodeAt(0))];
    const st = (await kitAsk(link, 2, body, { wait: 1000, tries: 4 }).catch((e) => { e.sent = i; throw e; }))[6];
    if (st === 1) throw stopFirst(i);
    if (st) throw kitFail("flash", `The FM-1 refused ${leaf(kits[i].path)}.`, i);
    onProgress((i + 1) / (2 * kits.length), "writing");
  }
  for (let i = 0; i < kits.length; i++) {
    const back = await slotRead(link, i, i).catch((e) => { e.sent = i; throw e; });
    if (!back || back.some((b, k) => b !== kits[i].raw[k])) throw kitFail("check", `${leaf(kits[i].path)} did not arrive intact.`, i);
    onProgress((kits.length + i + 1) / (2 * kits.length), "checking");
  }
}
