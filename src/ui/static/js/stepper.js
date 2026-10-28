// stepper.js - turns the safety trace produced by the C program into
// frames that can be shown one by one.
//
// Position 0          : initialisation (Work = Available, Finish = false)
// Position 1 .. L     : the C program examined steps[pos - 1]
// Position L + 1      : final result (safe or unsafe)

const vec = (v) => "(" + v.join(", ") + ")";

export function lastPosition(safety) {
  return safety.steps.length + 1;
}

export function frame(state, safety, pos) {
  const steps = safety.steps;
  const L = steps.length;
  const finish = Array(state.n).fill(false);
  const order = Array(state.n).fill(null);
  let count = 0;

  const upto = Math.min(pos, L);
  for (let k = 0; k < upto; k++) {
    if (steps[k].ok) {
      finish[steps[k].customer] = true;
      order[steps[k].customer] = count++;
    }
  }

  const status = finish.map((f) => (f ? "finished" : ""));
  let work = state.available;
  let current = null;
  let html;

  if (pos === 0) {
    html = `Эхлэл: <b>Work = Available = ${vec(state.available)}</b>, бүх i-д <b>Finish[i] = false</b>.`;
  } else if (pos <= L) {
    const st = steps[pos - 1];
    const i = st.customer;
    current = i;
    status[i] = st.ok ? "finished" : "checking";
    work = st.ok ? st.next : st.work;
    html = `Алхам ${pos}/${L}: <b>T${i}</b> — Finish[${i}] = false, ` +
      `Need<sub>${i}</sub> <b>${vec(state.need[i])}</b> ≤ Work <b>${vec(st.work)}</b> ? `;
    if (st.ok) {
      html += `<span class="yes">тийм</span> → Work = Work + Allocation<sub>${i}</sub> = ` +
        `<b>${vec(st.next)}</b>, Finish[${i}] = true. T${i} дуусаад нөөцөө буцаана.`;
    } else {
      html += `<span class="no">үгүй</span> → T${i} одоохондоо дуусч чадахгүй, дараагийн thread-ийг шалгана.`;
    }
  } else {
    if (L > 0) work = steps[L - 1].ok ? steps[L - 1].next : steps[L - 1].work;
    if (safety.safe) {
      html = `Бүх Finish[i] = true → систем <span class="yes">SAFE</span> төлөвт байна. ` +
        `Safe sequence: <b>&lt;${safety.sequence.map((c) => "T" + c).join(", ")}&gt;</b>`;
    } else {
      const stuck = finish.map((f, i) => (f ? null : "T" + i)).filter(Boolean);
      stuck.forEach((name) => (status[Number(name.slice(1))] = "blocked"));
      html = `Need ≤ Work нөхцөлийг хангах thread үлдсэнгүй → систем <span class="no">UNSAFE</span>. ` +
        `Дуусч чадахгүй: <b>${stuck.join(", ")}</b>`;
    }
  }

  return {
    finish, order, status, work, current, html,
    sequence: safety.sequence.slice(0, count),
    done: pos > L,
  };
}
