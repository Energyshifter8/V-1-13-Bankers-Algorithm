// app.js - interactive UI for the banker's algorithm.
// The page never decides whether a state is safe: every number shown here
// comes from the JSON answer of the C program (see api.js).

import { api } from "./api.js";
import { renderGraph, renderLegend } from "./graph.js";
import { frame, lastPosition } from "./stepper.js";

const $ = (id) => document.getElementById(id);
const letter = (j) => String.fromCharCode(65 + j);
const vec = (v) => "(" + v.join(", ") + ")";

const ui = {
  resp: null,      // last JSON answer from bin/banker
  prev: null,      // state before the last command (to highlight changes)
  draft: null,     // values typed in the matrix editor
  examples: [],
  pos: 0,          // position in the safety trace (see stepper.js)
  timer: null,
  view: "current", // "current" or "trial" (pretend state of a denied request)
  typed: [],       // commands typed in the console (for arrow up/down)
  typedPos: 0,
};

// State and safety trace shown by the graph and the stepper.
function src() {
  return ui.view === "trial" && ui.resp.trial ? ui.resp.trial : ui.resp;
}

// ------------------------------------------------------------------
// Rendering
// ------------------------------------------------------------------

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") node.className = v;
    else if (k.startsWith("on")) node.addEventListener(k.slice(2), v);
    else node.setAttribute(k, v);
  }
  for (const c of children) {
    if (c === null || c === undefined) continue;
    node.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return node;
}

function renderBadge() {
  const badge = $("safe-badge");
  const safe = ui.resp.safety.safe;
  badge.textContent = safe ? "SAFE" : "UNSAFE";
  badge.className = "badge " + (safe ? "safe" : "unsafe");
}

// The last RQ command of the C program, drawn as request edges.
function lastRequest() {
  const r = ui.resp;
  if (r.customer === undefined || !/^\s*rq\b/i.test(r.command || "")) return null;
  return { customer: r.customer, vector: r.vector };
}

function renderGraphView() {
  const s = src().state;
  const f = frame(s, src().safety, ui.pos);
  renderGraph($("graph"), {
    state: s,
    status: f.status,
    order: f.order,
    work: ui.pos > 0 ? f.work : null,
    request: lastRequest(),
    show: {
      alloc: $("show-alloc").checked,
      need: $("show-need").checked,
      labels: $("show-labels").checked,
    },
  });
}

function renderStepper() {
  const s = src().state;
  const safety = src().safety;
  const f = frame(s, safety, ui.pos);
  $("step-info").innerHTML = f.html;

  const finish = el("span", { class: "finish-cells" });
  f.finish.forEach((v, i) =>
    finish.append(el("span", { class: (v ? "t" : "") + (f.current === i ? " cur" : "") },
      `T${i}:${v ? "T" : "F"}`)));
  $("work-finish").replaceChildren(
    el("div", {}, "Work = ", el("span", { class: "vec" }, vec(f.work))),
    el("div", {}, "Finish = ", finish),
  );

  const seq = $("sequence");
  seq.replaceChildren(el("span", { class: "muted" }, "Safe sequence: "));
  f.sequence.forEach((c, k) => {
    if (k) seq.append(el("span", { class: "arrow" }, "→"));
    seq.append(el("span", { class: "chip done" }, "T" + c));
  });
  if (f.done && !safety.safe) {
    f.finish.forEach((v, i) => !v && seq.append(el("span", { class: "chip fail" }, "T" + i + " ✗")));
  }
  $("step-play").textContent = ui.timer ? "⏸ Pause" : "▶ Play";
}

function goTo(pos) {
  ui.pos = Math.max(0, Math.min(pos, lastPosition(src().safety)));
  renderGraphView();
  renderStepper();
}

function stopPlaying() {
  clearInterval(ui.timer);
  ui.timer = null;
}

function togglePlay() {
  if (ui.timer) {
    stopPlaying();
  } else {
    if (ui.pos >= lastPosition(src().safety)) ui.pos = 0;
    ui.timer = setInterval(() => {
      if (ui.pos >= lastPosition(src().safety)) stopPlaying();
      else goTo(ui.pos + 1);
      renderStepper();
    }, Number($("step-speed").value));
  }
  renderStepper();
}

function renderStateTable() {
  const s = ui.resp.state;
  const prev = ui.prev && ui.prev.n === s.n && ui.prev.m === s.m ? ui.prev : null;
  const box = $("state-table");
  box.replaceChildren();

  for (const w of ui.resp.warnings) {
    box.append(el("div", { class: "result warn" }, "⚠ " + w));
  }

  const changed = (name, i, j) =>
    prev && (i === null ? prev[name][j] !== s[name][j] : prev[name][i][j] !== s[name][i][j]);

  const table = el("table");
  const head1 = el("tr", {}, el("th"));
  const head2 = el("tr", {}, el("th"));
  for (const title of ["Max", "Allocation", "Need"]) {
    head1.append(el("th", { class: "gap" }), el("th", { class: "group", colspan: s.m }, title));
    head2.append(el("th", { class: "gap" }));
    for (let j = 0; j < s.m; j++) head2.append(el("th", {}, letter(j)));
  }
  table.append(head1, head2);

  for (let i = 0; i < s.n; i++) {
    const tr = el("tr", { class: ui.resp.customer === i ? "hl" : "" }, el("td", { class: "name" }, "T" + i));
    for (const name of ["maximum", "allocation", "need"]) {
      tr.append(el("td", { class: "gap" }));
      for (let j = 0; j < s.m; j++) {
        tr.append(el("td", { class: changed(name, i, j) ? "changed" : "" }, s[name][i][j]));
      }
    }
    table.append(tr);
  }

  const avail = el("tr", {}, el("td", { class: "name" }, "Available"));
  avail.append(el("td", { class: "gap" }));
  for (let j = 0; j < s.m; j++) {
    avail.append(el("td", { class: changed("available", null, j) ? "changed" : "" }, s.available[j]));
  }
  const total = el("tr", {}, el("td", { class: "name" }, "Total"));
  total.append(el("td", { class: "gap" }));
  for (let j = 0; j < s.m; j++) total.append(el("td", {}, s.total[j]));

  box.append(table, el("table", { style: "margin-top:10px" }, avail, total));
}

// ------------------------------------------------------------------
// Matrix editor (Available, Max, Allocation)
// ------------------------------------------------------------------

function draftFromState(s) {
  return {
    n: s.n,
    m: s.m,
    available: [...s.available],
    maximum: s.maximum.map((r) => [...r]),
    allocation: s.allocation.map((r) => [...r]),
  };
}

function numberInput(value, original, onChange) {
  const input = el("input", { type: "text", inputmode: "numeric", value: String(value) });
  const check = () => {
    const ok = /^\s*\d+\s*$/.test(input.value);
    input.classList.toggle("invalid", !ok);
    input.classList.toggle("dirty", ok && Number(input.value) !== original);
    onChange(ok ? Number(input.value) : NaN);
  };
  input.addEventListener("input", check);
  return input;
}

function renderEditor() {
  const s = ui.resp.state;
  const d = ui.draft;
  const box = $("matrix-editor");
  box.replaceChildren();

  const avail = el("table");
  const ah = el("tr", {}, el("th"));
  const ar = el("tr", {}, el("td", { class: "rowname" }, "Available"));
  for (let j = 0; j < d.m; j++) {
    ah.append(el("th", {}, letter(j)));
    ar.append(el("td", {}, numberInput(d.available[j], s.available[j], (v) => (d.available[j] = v))));
  }
  avail.append(ah, ar);

  const table = el("table", { style: "margin-top:8px" });
  const h1 = el("tr", {}, el("th"));
  const h2 = el("tr", {}, el("th"));
  for (const title of ["Max", "Allocation"]) {
    h1.append(el("th", { class: "sep" }), el("th", { class: "group", colspan: d.m }, title));
    h2.append(el("th", { class: "sep" }));
    for (let j = 0; j < d.m; j++) h2.append(el("th", {}, letter(j)));
  }
  table.append(h1, h2);

  for (let i = 0; i < d.n; i++) {
    const tr = el("tr", {}, el("td", { class: "rowname" }, "T" + i));
    tr.append(el("td", { class: "sep" }));
    for (let j = 0; j < d.m; j++) {
      tr.append(el("td", {}, numberInput(d.maximum[i][j], s.maximum[i][j], (v) => (d.maximum[i][j] = v))));
    }
    tr.append(el("td", { class: "sep" }));
    for (let j = 0; j < d.m; j++) {
      tr.append(el("td", {}, numberInput(d.allocation[i][j], s.allocation[i][j], (v) => (d.allocation[i][j] = v))));
    }
    table.append(tr);
  }
  box.append(avail, table);
  $("size-n").value = d.n;
  $("size-m").value = d.m;
}

// Commands that turn the current C state into the draft.
function editorCommands() {
  const s = ui.resp.state;
  const d = ui.draft;
  const all = [d.available, ...d.maximum, ...d.allocation].flat();
  if (all.some((v) => Number.isNaN(v))) {
    throw new Error("Улаанаар тэмдэглэсэн нүдэнд сөрөг бус бүхэл тоо оруулна уу.");
  }
  const cmds = [];
  if (d.available.some((v, j) => v !== s.available[j])) cmds.push("AVAIL " + d.available.join(" "));
  for (let i = 0; i < d.n; i++) {
    const maxChanged = d.maximum[i].some((v, j) => v !== s.maximum[i][j]);
    const allocChanged = d.allocation[i].some((v, j) => v !== s.allocation[i][j]);
    if (!maxChanged && !allocChanged) continue;
    // ALLOC 0 first so that MAX can never be rejected by the old allocation
    if (maxChanged) {
      cmds.push(`ALLOC ${i} ` + Array(d.m).fill(0).join(" "));
      cmds.push(`MAX ${i} ` + d.maximum[i].join(" "));
    }
    cmds.push(`ALLOC ${i} ` + d.allocation[i].join(" "));
  }
  return cmds;
}

// ------------------------------------------------------------------
// Request / Release
// ------------------------------------------------------------------

function renderRequestPanel() {
  const s = ui.resp.state;
  const select = $("req-customer");
  const keep = Math.min(Number(select.value) || 0, s.n - 1);
  select.replaceChildren(...Array.from({ length: s.n }, (_, i) => el("option", { value: i }, "T" + i)));
  select.value = keep;

  const box = $("req-vector");
  const old = [...box.querySelectorAll("input")].map((x) => x.value);
  box.replaceChildren();
  for (let j = 0; j < s.m; j++) {
    box.append(el("label", {}, letter(j),
      el("input", { type: "text", inputmode: "numeric", value: old[j] ?? "0", "aria-label": letter(j) })));
  }
  box.append(el("p", { class: "hint", id: "req-hint", style: "width:100%" }));
  updateRequestHint();
}

function updateRequestHint() {
  const s = ui.resp.state;
  const i = Number($("req-customer").value);
  $("req-hint").textContent =
    `T${i}: Need ${vec(s.need[i])}, Allocation ${vec(s.allocation[i])}, Available ${vec(s.available)}`;
}

function requestCommand(kind) {
  const inputs = [...$("req-vector").querySelectorAll("input")];
  let empty = false;
  for (const input of inputs) {
    const bad = input.value.trim() === "";
    input.classList.toggle("invalid", bad);
    empty ||= bad;
  }
  if (empty) throw new Error("Нөөц бүрт тоо оруулна уу (0 байж болно).");
  return `${kind} ${$("req-customer").value} ` + inputs.map((x) => x.value.trim()).join(" ");
}

const REASONS = {
  exceeds_need: ["bad", "✖ Татгалзсан: хүсэлт нь Need-ээс их буюу maximum claim-аа хэтрүүлсэн (алдаа)."],
  unavailable: ["warn", "⏳ Хүлээнэ: Available нөөц хүрэлцэхгүй. Thread бусад нь нөөцөө чөлөөлөхийг хүлээнэ."],
  unsafe: ["bad", "✖ Татгалзсан: олгосон тохиолдолд систем UNSAFE төлөвт орно. " +
    "\"Туршилтын төлөв\" товчоор шалтгааныг алхамчилж харна уу."],
  exceeds_allocation: ["bad", "✖ Татгалзсан: эзэмшиж буй нөөцөөсөө их чөлөөлөх боломжгүй."],
};

function explain(resp) {
  const msg = resp.message.trim();
  if (resp.outcome === "granted") {
    showResult("ok", "✔ Зөвшөөрөгдлөө: систем safe төлөвт үлдэнэ. Safe sequence: <" +
      resp.safety.sequence.map((c) => "T" + c).join(", ") + ">", msg);
  } else if (resp.outcome === "released") {
    showResult("ok", "✔ Нөөц чөлөөлөгдөж Available нэмэгдлээ.", msg);
  } else if (resp.outcome === "denied") {
    const [kind, text] = REASONS[resp.reason] || ["bad", "✖ Татгалзсан."];
    showResult(kind, text, msg);
  } else if (resp.outcome === "error") {
    showResult("bad", "C программ алдаа мэдээллээ:", msg);
  } else {
    showResult("", "C программын гаралт:", msg || "(хоосон)");
  }
}

function renderViewSwitch() {
  const sw = $("view-switch");
  sw.hidden = !ui.resp.trial;
  if (!ui.resp.trial) ui.view = "current";
  for (const b of sw.querySelectorAll("button")) {
    b.classList.toggle("active", b.dataset.view === ui.view);
  }
}

// ------------------------------------------------------------------
// Commands
// ------------------------------------------------------------------

function showResult(kind, title, detail) {
  const box = $("result");
  box.hidden = false;
  box.className = "result " + kind;
  box.replaceChildren(el("div", {}, title));
  if (detail) box.append(el("pre", {}, detail));
}

function update(resp) {
  ui.prev = ui.resp ? ui.resp.state : null;
  ui.resp = resp;
  ui.draft = draftFromState(resp.state);
  stopPlaying();
  ui.view = "current";
  ui.pos = lastPosition(resp.safety);
  render();
}

function render() {
  renderBadge();
  renderViewSwitch();
  renderGraphView();
  renderStepper();
  renderRequestPanel();
  renderStateTable();
  renderEditor();
}

// History of every command sent to the C program and its answer.
function log(resp) {
  const list = $("history");
  const item = el("li", {},
    el("span", { class: "cmd" }, resp.command || "(start)"),
    el("span", { class: "tag " + resp.outcome }, resp.outcome + (resp.reason ? ": " + resp.reason : "")));
  if (resp.message.trim()) item.append(el("div", { class: "out" }, resp.message.trimEnd()));
  list.prepend(item);
  while (list.children.length > 200) list.lastChild.remove();
}

async function run(command) {
  const resp = await api.command(command);
  log(resp);
  update(resp);
  return resp;
}

async function runBatch(commands) {
  const results = await api.batch(commands);
  results.forEach(log);
  const failed = results.find((r) => r.outcome === "error");
  if (results.length) update(results[results.length - 1]);
  return failed;
}

async function guard(fn) {
  try {
    await fn();
    $("conn").classList.remove("down");
  } catch (err) {
    showResult("bad", "Алдаа", err.message);
    if (/холбогдож/.test(err.message)) $("conn").classList.add("down");
  }
}

function bindConsole() {
  const input = $("console-input");
  $("console-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const command = input.value.trim();
    if (!command) return;
    ui.typed.push(command);
    ui.typedPos = ui.typed.length;
    input.value = "";
    guard(async () => explain(await run(command)));
  });
  input.addEventListener("keydown", (e) => {
    if (e.key !== "ArrowUp" && e.key !== "ArrowDown") return;
    e.preventDefault();
    ui.typedPos += e.key === "ArrowUp" ? -1 : 1;
    ui.typedPos = Math.max(0, Math.min(ui.typedPos, ui.typed.length));
    input.value = ui.typed[ui.typedPos] ?? "";
  });
}

function bindControls() {
  bindConsole();
  $("btn-request").addEventListener("click", () =>
    guard(async () => explain(await run(requestCommand("RQ")))));
  $("btn-release").addEventListener("click", () =>
    guard(async () => explain(await run(requestCommand("RL")))));
  $("req-customer").addEventListener("change", updateRequestHint);

  for (const b of $("view-switch").querySelectorAll("button")) {
    b.addEventListener("click", () => {
      stopPlaying();
      ui.view = b.dataset.view;
      ui.pos = lastPosition(src().safety);
      renderViewSwitch();
      renderGraphView();
      renderStepper();
    });
  }

  for (const id of ["show-alloc", "show-need", "show-labels"]) {
    $(id).addEventListener("change", () => ui.resp && renderGraphView());
  }

  $("step-first").addEventListener("click", () => { stopPlaying(); goTo(0); });
  $("step-prev").addEventListener("click", () => { stopPlaying(); goTo(ui.pos - 1); });
  $("step-next").addEventListener("click", () => { stopPlaying(); goTo(ui.pos + 1); });
  $("step-last").addEventListener("click", () => { stopPlaying(); goTo(Infinity); });
  $("step-play").addEventListener("click", togglePlay);
  $("step-speed").addEventListener("change", () => {
    if (ui.timer) { stopPlaying(); togglePlay(); }
  });

  $("btn-apply").addEventListener("click", () =>
    guard(async () => {
      const cmds = editorCommands();
      if (cmds.length === 0) {
        showResult("", "Өөрчлөлт алга.");
        return;
      }
      const failed = await runBatch([...cmds, "SAVE"]);
      if (failed) showResult("bad", "C программ засварыг хүлээж аваагүй:", failed.message);
      else showResult("ok", `${cmds.length} команд амжилттай (SAVE хийгдсэн).`, cmds.join("\n"));
    })
  );

  $("btn-discard").addEventListener("click", () => {
    ui.draft = draftFromState(ui.resp.state);
    renderEditor();
  });

  $("btn-new").addEventListener("click", () =>
    guard(async () => {
      const resp = await run(`NEW ${$("size-n").value} ${$("size-m").value}`);
      showResult(resp.outcome === "error" ? "bad" : "ok", resp.message.trim());
    })
  );

  $("btn-reset").addEventListener("click", () =>
    guard(async () => {
      const resp = await run("RESET");
      showResult("ok", resp.message.trim());
    })
  );

  $("btn-restart").addEventListener("click", () =>
    guard(async () => {
      update(await api.restart());
      showResult("ok", "C процесс дахин эхэллээ (§8.6.3.3 жишээ).");
    })
  );

  $("example-select").addEventListener("change", () => {
    const ex = ui.examples[$("example-select").selectedIndex];
    $("example-desc").textContent = ex ? ex.description : "";
  });

  $("example-load").addEventListener("click", () =>
    guard(async () => {
      const ex = ui.examples[$("example-select").selectedIndex];
      if (!ex) return;
      const failed = await runBatch(ex.commands);
      if (failed) showResult("bad", "Сценари ачаалахад алдаа гарлаа:", failed.message);
      else showResult("ok", `"${ex.title}" ачааллаа.`);
    })
  );
}

async function init() {
  bindControls();
  renderLegend($("legend"));
  await guard(async () => {
    ui.examples = await api.examples();
    const select = $("example-select");
    for (const ex of ui.examples) select.append(el("option", {}, ex.title));
    select.dispatchEvent(new Event("change"));
    const resp = await api.state();
    log(resp);
    update(resp);
  });
}

init();
