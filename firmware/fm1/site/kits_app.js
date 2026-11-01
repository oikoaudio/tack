/* SPDX-License-Identifier: GPL-3.0-only */
/* The kit bank page: site/kits_body.html's controls on site/kits_core.js. make_web.py inlines this after that file and
 * fm1-base's fm1ports.js and fm1ota.js, which give Updater and IS_OTA */
const FIRMWARE_ID = "{{ID}}";
const PICKED_PORTS = "fm1-ports";             // the ports the user picked, shared with the install and backup pages
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const plural = (n, one) => `${n} ${one}${n === 1 ? "" : "s"}`;

let busy = false, chosen = null, access = null, lastSaid = 0, lastShare = -100;
let ports = [];
try { ports = JSON.parse(localStorage.getItem(PICKED_PORTS) || "[]").filter((x) => typeof x === "string"); } catch (_) { /* none */ }

function say(html, kind = "") {
  $("status").innerHTML = kind === "err" ? "" : html;
  $("alert").innerHTML = kind === "err" ? html : "";
  $("statusBox").className = "status" + (kind ? " " + kind : "");
  lastSaid = performance.now();
}
/* progress in words at most every 2 s, or every 10 percent: a screen reader reads each change */
function sayProgress(what, share) {
  const pct = Math.floor(share * 100);
  $("bar").hidden = false;
  $("bar").value = pct;
  if (performance.now() - lastSaid < 2000 && pct - lastShare < 10) return;
  lastShare = pct;
  say(`${esc(what)}: ${pct} percent`);
}
function setBusy(on) {
  busy = on;
  for (const id of ["files", "folder", "save"]) $(id).disabled = on;
  $("fill").disabled = on || !chosen || !chosen.kits.length;
  if (on) { $("ask").hidden = true; lastShare = -100; } else $("bar").hidden = true;
}
window.addEventListener("beforeunload", (e) => { if (busy) { e.preventDefault(); e.returnValue = ""; } });

function updater() {
  const up = new Updater(access);
  ports.forEach((n) => up.allow(n));
  return up;
}
/* after "not found": the ports the page did not ask, each with Try */
function showPorts(up) {
  const names = up.ports().filter((n) => !up.askable(n));
  $("portlist").replaceChildren(...names.map((n) => {
    const b = document.createElement("button"), li = document.createElement("li"), name = document.createElement("span");
    b.type = "button"; b.className = "quiet"; b.textContent = "Try";
    b.setAttribute("aria-label", `Try ${n}`);
    b.addEventListener("click", () => tryPort(n, b));
    name.textContent = n;
    li.append(name, b);
    return li;
  }));
  if (!names.length) { const li = document.createElement("li"); li.textContent = "No other MIDI ports."; $("portlist").append(li); }
  $("portpick").hidden = false;
}
async function tryPort(name, button) {
  if (busy) return;
  button.disabled = true;
  $("pickstatus").textContent = "Asking…";
  let id = null;
  try { id = await updater().probe(name); } catch (_) { /* no answer */ }
  button.disabled = false;
  if (!id) { $("pickstatus").textContent = `No FM-1 answered on ${name}.`; return; }
  ports.push(name);
  try { localStorage.setItem(PICKED_PORTS, JSON.stringify(ports)); } catch (_) { /* until the page is closed */ }
  $("pickstatus").textContent = `Found an FM-1 on ${name}. This browser remembers it. Press your button again.`;
}
async function connect() {
  if (!navigator.requestMIDIAccess) throw kitFail("nomidi", "This browser has no Web MIDI. Open this page in Chrome or Edge.");
  try { access = access || await navigator.requestMIDIAccess({ sysex: true }); }
  catch (_) { throw kitFail("denied", "MIDI with SysEx was not allowed. Allow MIDI in the site settings (the address bar), then try again."); }
  say("Looking for Tack…");
  $("portpick").hidden = true;
  const up = updater(), dev = await up.findAny((id) => !IS_OTA(id));
  if (!dev) {
    showPorts(up);
    throw kitFail("notfound", "Tack not found. Plug the FM-1 in with a USB data cable, close other apps that use its MIDI, and try again.");
  }
  if (dev.id.text.startsWith(FIRMWARE_ID)) return dev;
  dev.link.close();
  throw kitFail("model", `The FM-1 runs other firmware (${dev.id.text}), not Tack. Install Tack first.`);
}

async function readChoice(list) {
  const files = [];
  for (const f of list) {
    const path = f.webkitRelativePath || f.name;
    if (/\.zip$/i.test(path)) files.push(...await zipRead(new Uint8Array(await f.arrayBuffer()), path));
    else if (/\.snd$/i.test(path)) files.push({ path, data: f.size > KIT_BYTES ? null : new Uint8Array(await f.arrayBuffer()) });
  }
  return kitsFrom(files);
}
function describe({ kits, notKits, copies, empties, over }) {
  const notes = [];
  if (empties) notes.push(`${plural(empties, "empty Initkit slot")} left out`);
  if (copies) notes.push(`${copies === 1 ? "1 exact copy" : `${copies} exact copies`} left out`);
  if (over) notes.push(`${plural(over, "kit")} past the bank's ${KIT_SLOTS} slots left out`);
  if (notKits.length) notes.push(`${notKits.length === 1 ? "1 file is" : `${notKits.length} files are`} not a kit: ${notKits.slice(0, 5).map((p) => p.split("/").pop()).join(", ")}${notKits.length > 5 ? " and more" : ""}`);
  const named = kits.map((k) => esc(k.path.split("/").pop()));
  return `<b>${plural(kits.length, "kit")}</b>${kits.length > 1 ? `, from ${named[0]} to ${named[kits.length - 1]}` : kits.length ? `, ${named[0]}` : ""}.` +
    (notes.length ? ` ${esc(notes.join(". "))}.` : "");
}
async function choose(e, other) {
  chosen = null;
  $("ask").hidden = true;
  other.value = "";
  const info = $("fileInfo");
  info.hidden = true;
  const list = Array.from(e.target.files || []);
  if (list.length) {
    try {
      chosen = await readChoice(list);
      info.innerHTML = describe(chosen);
      info.hidden = !chosen.kits.length && !chosen.notKits.length;
      say(chosen.kits.length ? "Ready. Stop Tack first, then press Replace the bank." : "No kits there. Choose .SND files, a folder of them, or a .zip of them.", chosen.kits.length ? "" : "err");
    } catch (err) {
      say(esc(err.message), "err");
    }
  }
  setBusy(false);
}
$("files").addEventListener("change", (e) => choose(e, $("folder")));
$("folder").addEventListener("change", (e) => choose(e, $("files")));

$("fill").addEventListener("click", () => {
  if (busy || !chosen) return;
  $("askText").innerHTML = `Replace every kit on Tack with these ${plural(chosen.kits.length, "kit")}? The kits on the FM-1 now are erased. To keep them, press Save the bank first.`;
  $("ask").hidden = false;
  $("askYes").focus();
});
$("askNo").addEventListener("click", () => { $("ask").hidden = true; $("fill").focus(); });
$("askYes").addEventListener("click", async () => {
  if (busy || !chosen) return;
  setBusy(true);
  let dev = null;
  try {
    dev = await connect();
    say("Erasing the bank…");
    await bankSend(dev.link, chosen.kits, (share, step) => sayProgress(step === "writing" ? "Writing kits" : "Checking kits", share));
    say(`Done. Tack has ${plural(chosen.kits.length, "kit")}, and each one read back the same as its file. Turn <kbd>PRESET</kbd> to hear them.`);
  } catch (err) {
    const left = err.sent === undefined ? "" : ` ${err.sent ? `The first ${plural(err.sent, "kit")} arrived.` : "No kits arrived."} Press Replace the bank to send them all again.`;
    say(esc(err.message + left), "err");
  } finally {
    if (dev) dev.link.close();
    setBusy(false);
  }
});

$("save").addEventListener("click", async () => {
  if (busy) return;
  setBusy(true);
  let dev = null;
  try {
    dev = await connect();
    const files = await bankRead(dev.link, (share) => sayProgress("Reading the bank", share));
    if (!files.length) { say("The bank is empty, so there is nothing to save."); return; }
    const d = new Date(), pad = (v) => String(v).padStart(2, "0");
    const name = `tack-kits-${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}.zip`;
    const url = URL.createObjectURL(new Blob([zipWrite(files)], { type: "application/zip" }));
    const a = document.createElement("a");
    a.href = url; a.download = name;
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
    say(`Done. <b>${esc(name)}</b> is in your downloads, with ${plural(files.length, "kit")}. Choose that file above to put them back.`);
  } catch (err) {
    say(esc(err.message), "err");
  } finally {
    if (dev) dev.link.close();
    setBusy(false);
  }
});
