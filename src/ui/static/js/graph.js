// graph.js - resource graph (resource-allocation graph with claim edges,
// OSC 10th ed. §8.3.2 and §8.6.2) drawn as SVG.
//
//   circle  Ti          thread / customer
//   box     A, B, ...   resource type, one dot per instance
//                       (filled = allocated, hollow = available)
//   solid arrow R -> T  assignment edge, label = Allocation[i][j]
//   dashed arrow T -> R claim edge,      label = Need[i][j]
//   blue arrow T -> R   the last request (RQ) of a thread

const NS = "http://www.w3.org/2000/svg";
const letter = (j) => String.fromCharCode(65 + j);

function svg(tag, attrs = {}, text) {
  const node = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
  if (text !== undefined) node.textContent = text;
  return node;
}

function marker(id, color) {
  const m = svg("marker", {
    id, viewBox: "0 0 10 10", refX: 9, refY: 5,
    markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse",
  });
  m.append(svg("path", { d: "M0,0 L10,5 L0,10 z", fill: color }));
  return m;
}

// Quadratic curve from p to q bent sideways by `bend` pixels.
function curve(p, q, bend) {
  const mx = (p.x + q.x) / 2, my = (p.y + q.y) / 2;
  const dx = q.x - p.x, dy = q.y - p.y;
  const len = Math.hypot(dx, dy) || 1;
  const c = { x: mx - (dy / len) * bend, y: my + (dx / len) * bend };
  return { d: `M${p.x},${p.y} Q${c.x},${c.y} ${q.x},${q.y}`, p, c, q };
}

// Point at parameter t of a quadratic curve.
function at(cv, t) {
  const u = 1 - t;
  return {
    x: u * u * cv.p.x + 2 * u * t * cv.c.x + t * t * cv.q.x,
    y: u * u * cv.p.y + 2 * u * t * cv.c.y + t * t * cv.q.y,
  };
}

/**
 * Draw the graph into `container`.
 * opts = {
 *   state,            // state object from the C program
 *   status: [],       // per thread: "", "checking", "finished", "blocked"
 *   order: [],        // per thread: position in the safe sequence or null
 *   work: [] | null,  // Work vector of the current safety step
 *   request: {customer, vector} | null,
 *   show: {alloc, need, labels},
 * }
 */
export function renderGraph(container, opts) {
  const { state: s, show } = opts;
  const R = 26;                                  // thread radius
  const boxW = 128;
  const perRow = 8;                              // instance dots per row
  const maxDots = 40;                            // more -> draw a bar instead
  const dotRows = (t) => (t > maxDots ? 1 : Math.max(1, Math.ceil(t / perRow)));
  const boxH = 58 + Math.max(...s.total.map(dotRows)) * 13;

  const width = Math.max(760, s.n * 96 + 80, s.m * (boxW + 50) + 80);
  const threadY = 64;
  const boxY = 330;
  const height = boxY + boxH + 30;

  const xs = (count, i) => (width / count) * (i + 0.5);
  const thread = Array.from({ length: s.n }, (_, i) => ({ x: xs(s.n, i), y: threadY }));
  const res = Array.from({ length: s.m }, (_, j) => ({ x: xs(s.m, j), y: boxY }));

  const root = svg("svg", {
    viewBox: `0 0 ${width} ${height}`, width, role: "img",
    "aria-label": "Resource allocation graph",
  });
  const defs = svg("defs");
  defs.append(marker("arrow-alloc", "#4b5469"), marker("arrow-need", "#9aa3b8"),
    marker("arrow-request", "#2f5bd3"));
  root.append(defs);

  const edges = svg("g");
  const labels = svg("g");
  const nodes = svg("g");
  root.append(edges, labels, nodes);

  // Attachment points: bottom of a circle, top of a box (spread by thread)
  const threadPort = (i, j) => {
    const off = s.m > 1 ? ((j / (s.m - 1)) - 0.5) * R * 1.2 : 0;
    return { x: thread[i].x + off, y: thread[i].y + R };
  };
  const resPort = (j, i) => {
    const off = s.n > 1 ? ((i / (s.n - 1)) - 0.5) * (boxW - 24) : 0;
    return { x: res[j].x + off, y: res[j].y };
  };

  const drawEdge = (cls, from, to, bend, label, labelT, data) => {
    const cv = curve(from, to, bend);
    const path = svg("path", { d: cv.d, class: cls, "marker-end": `url(#${data.marker})` });
    path.dataset.thread = data.i;
    path.dataset.res = data.j;
    edges.append(path);
    if (show.labels && label !== null) {
      const pt = at(cv, labelT);
      const t = svg("text", {
        x: pt.x, y: pt.y + 4, "text-anchor": "middle",
        class: "edge-label " + data.kind,
      }, label);
      t.dataset.thread = data.i;
      t.dataset.res = data.j;
      labels.append(t);
    }
  };

  for (let i = 0; i < s.n; i++) {
    for (let j = 0; j < s.m; j++) {
      const a = s.allocation[i][j];
      const nd = s.need[i][j];
      if (show.alloc && a > 0) {
        drawEdge("edge-alloc", resPort(j, i), threadPort(i, j), 18, a, 0.3,
          { i, j, kind: "alloc", marker: "arrow-alloc" });
      }
      if (show.need && nd > 0) {
        drawEdge("edge-need", threadPort(i, j), resPort(j, i), 18, nd, 0.3,
          { i, j, kind: "need", marker: "arrow-need" });
      }
    }
  }

  const req = opts.request;
  if (req) {
    req.vector.forEach((k, j) => {
      if (k > 0) {
        drawEdge("edge-request", threadPort(req.customer, j), resPort(j, req.customer), 0,
          "+" + k, 0.55, { i: req.customer, j, kind: "request", marker: "arrow-request" });
      }
    });
  }

  // Threads
  for (let i = 0; i < s.n; i++) {
    const status = opts.status[i] || "";
    const isReq = req && req.customer === i;
    const g = svg("g", { class: `node-thread ${status} ${isReq ? "requesting" : ""}`, tabindex: 0 });
    g.dataset.thread = i;
    g.append(svg("title", {},
      `T${i}\nMax (${s.maximum[i].join(", ")})\nAllocation (${s.allocation[i].join(", ")})` +
      `\nNeed (${s.need[i].join(", ")})`));
    g.append(svg("circle", { cx: thread[i].x, cy: thread[i].y, r: R }));
    g.append(svg("text", { x: thread[i].x, y: thread[i].y + 5, "text-anchor": "middle" }, "T" + i));
    if (opts.order[i] !== null && opts.order[i] !== undefined) {
      g.append(svg("text", {
        x: thread[i].x, y: thread[i].y - R - 8, "text-anchor": "middle", class: "order",
      }, `#${opts.order[i] + 1}`));
    }
    nodes.append(g);
  }

  // Resource types
  for (let j = 0; j < s.m; j++) {
    const total = s.total[j];
    const free = s.available[j];
    const held = total - free;
    const x0 = res[j].x - boxW / 2;
    const g = svg("g", { class: "node-resource", tabindex: 0 });
    g.dataset.res = j;
    g.append(svg("title", {}, `${letter(j)}: total ${total}, available ${free}, allocated ${held}`));
    g.append(svg("rect", { x: x0, y: boxY, width: boxW, height: boxH }));
    g.append(svg("text", { x: x0 + 10, y: boxY + 20, class: "title" }, letter(j)));
    g.append(svg("text", { x: x0 + boxW - 10, y: boxY + 20, "text-anchor": "end", class: "sub" },
      `${free}/${total} чөлөөтэй`));

    if (total <= maxDots) {
      for (let k = 0; k < total; k++) {
        const cx = x0 + 14 + (k % perRow) * 14;
        const cy = boxY + 38 + Math.floor(k / perRow) * 13;
        g.append(svg("circle", { cx, cy, r: 4.5, class: k < held ? "dot-held" : "dot-free" }));
      }
    } else {
      const bw = boxW - 20;
      g.append(svg("rect", { x: x0 + 10, y: boxY + 32, width: bw, height: 10, class: "dot-free" }));
      g.append(svg("rect", { x: x0 + 10, y: boxY + 32, width: total ? (bw * held) / total : 0,
        height: 10, class: "dot-held" }));
    }
    if (opts.work) {
      g.append(svg("text", { x: x0 + 10, y: boxY + boxH - 10, class: "sub" }, `Work = ${opts.work[j]}`));
    }
    nodes.append(g);
  }

  // Hover: highlight the edges of one node
  const focus = (key, value) => {
    for (const e of root.querySelectorAll("path[data-thread], text[data-thread]")) {
      e.classList.toggle("dim", value !== null && e.dataset[key] !== String(value));
    }
  };
  for (const g of nodes.children) {
    const key = g.dataset.thread !== undefined ? "thread" : "res";
    const on = () => focus(key, g.dataset[key]);
    const off = () => focus(key, null);
    g.addEventListener("mouseenter", on);
    g.addEventListener("mouseleave", off);
    g.addEventListener("focus", on);
    g.addEventListener("blur", off);
  }

  container.replaceChildren(root);
}

export function renderLegend(container) {
  const item = (svgInner, text) => {
    const span = document.createElement("span");
    span.innerHTML = `<svg width="34" height="16" viewBox="0 0 34 16">${svgInner}</svg>`;
    span.append(text);
    return span;
  };
  container.replaceChildren(
    item('<circle cx="10" cy="8" r="6" fill="#fff" stroke="#8a93a8" stroke-width="2"/>', "Thread (Ti)"),
    item('<rect x="2" y="2" width="28" height="12" rx="3" fill="#fff" stroke="#4b5469" stroke-width="2"/>', "Нөөцийн төрөл"),
    item('<circle cx="9" cy="8" r="4" fill="#4b5469"/><circle cx="23" cy="8" r="4" fill="#fff" stroke="#4b5469"/>', "Эзэмшсэн / чөлөөтэй instance"),
    item('<line x1="2" y1="8" x2="30" y2="8" stroke="#4b5469" stroke-width="2"/>', "R→T Allocation"),
    item('<line x1="2" y1="8" x2="30" y2="8" stroke="#9aa3b8" stroke-width="2" stroke-dasharray="5 4"/>', "T→R Need (claim)"),
    item('<line x1="2" y1="8" x2="30" y2="8" stroke="#2f5bd3" stroke-width="3"/>', "Сүүлийн Request"),
    item('<circle cx="10" cy="8" r="6" fill="#fff8e6" stroke="#e3a008" stroke-width="3"/>', "Шалгаж буй"),
    item('<circle cx="10" cy="8" r="6" fill="#e3f6eb" stroke="#1f9d55" stroke-width="2"/>', "Finish = true"),
    item('<circle cx="10" cy="8" r="6" fill="#fdeaea" stroke="#d64545" stroke-width="2"/>', "Дуусгах боломжгүй"),
  );
}
