/* Built-in gameplay: original, procedurally drawn backgrounds that need no download.
   drawBuiltin(kind, g, W, H, t) renders the scene at time t (seconds); deterministic, so exports match previews. */
const Builtin = (() => {
  const rng = seed => () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
  const hash = (a, b) => { let h = (a * 374761393 + b * 668265263) >>> 0; h = ((h ^ (h >>> 13)) * 1274126177) >>> 0; return (h ^ (h >>> 16)) / 4294967296; };
  const lerp = (a, b, k) => a + (b - a) * k;
  const shade = (hex, k) => { const n = parseInt(hex.slice(1), 16); const c = [(n >> 16) & 255, (n >> 8) & 255, n & 255].map(v => Math.max(0, Math.min(255, Math.round(v * k)))); return `rgb(${c[0]},${c[1]},${c[2]})`; };

  /* ---------------- train-yard runner ---------------- */
  const SEG = 20, SPEED = 16, LANES = [-2.6, 0, 2.6];
  function segment(k){
    if (k < 2) return {free: 1, obs: []};
    const r = rng(k * 7919 + 13);
    const free = Math.floor(r() * 3), obs = [];
    for (let l = 0; l < 3; l++){
      if (l === free) continue;
      const roll = r();
      if (roll < 0.55) obs.push({lane: l, type: "train", len: 9 + Math.floor(r() * 6), color: ["#d6453d", "#2f6fd6", "#e8b923", "#3fae6a"][Math.floor(r() * 4)], z0: k * SEG + 3});
      else if (roll < 0.8) obs.push({lane: l, type: "hurdle", z0: k * SEG + 6 + r() * 6});
    }
    return {free, obs};
  }
  function runnerX(z){
    const k = Math.floor(z / SEG), f = (z - k * SEG) / SEG;
    const a = LANES[segment(k).free], b = LANES[segment(k + 1).free];
    const s = Math.max(0, Math.min(1, (f - 0.55) / 0.35));
    return lerp(a, b, s * s * (3 - 2 * s));
  }
  function drawRunnerScene(g, W, H, t){
    const z = t * SPEED, camX = runnerX(z + 4) * 0.85, camY = 2.6, hz = H * 0.38, f = Math.min(W, H) * 1.05;
    const P = (x, y, wz) => { const d = Math.max(0.05, wz - z + 1.2); return [W / 2 + (x - camX) * f / d, hz + (camY - y) * f / d, d]; };
    // sky + skyline
    let gr = g.createLinearGradient(0, 0, 0, hz); gr.addColorStop(0, "#5aa9f0"); gr.addColorStop(1, "#cfe8fb");
    g.fillStyle = gr; g.fillRect(0, 0, W, hz + 2);
    for (let i = 0; i < 18; i++){
      const bw = W / 12, x = ((i * bw * 1.3 - z * 0.6) % (W * 1.6) + W * 1.6) % (W * 1.6) - W * 0.3, bh = H * (0.05 + hash(i, 3) * 0.12);
      g.fillStyle = i % 2 ? "#9fb7cc" : "#8aa4bb"; g.fillRect(x, hz - bh, bw, bh);
    }
    // ground
    gr = g.createLinearGradient(0, hz, 0, H); gr.addColorStop(0, "#8b7d6b"); gr.addColorStop(1, "#5e5244");
    g.fillStyle = gr; g.fillRect(0, hz, W, H - hz);
    const quad = (pts, fill) => { g.beginPath(); g.moveTo(pts[0][0], pts[0][1]); for (let i = 1; i < pts.length; i++) g.lineTo(pts[i][0], pts[i][1]); g.closePath(); g.fillStyle = fill; g.fill(); };
    const far = z + 90;
    // side walls with colored panels
    for (const side of [-1, 1]){
      const x = side * 6.5;
      for (let wz = Math.floor(z / 8) * 8 + 96; wz >= Math.floor(z / 8) * 8; wz -= 8){
        if (wz + 8 < z) continue;
        const a = P(x, 0, wz), b = P(x, 0, wz + 8), c = P(x, 6, wz + 8), d = P(x, 6, wz);
        const k = Math.floor(wz / 8), hue = Math.floor(hash(k, side + 5) * 360);
        quad([a, b, c, d], k % 2 ? "#b9a48c" : "#a99378");
        const pa = P(x, 1.2, wz + 1.5), pb = P(x, 1.2, wz + 6), pc = P(x, 3.6, wz + 6), pd = P(x, 3.6, wz + 1.5);
        quad([pa, pb, pc, pd], `hsl(${hue},70%,55%)`);
      }
    }
    // tracks: sleepers then rails
    for (let s = Math.floor(z / 1.4) * 1.4 + 90; s >= z - 1; s -= 1.4){
      for (const lx of LANES){
        const a = P(lx - 1.05, 0, s), b = P(lx + 1.05, 0, s), c = P(lx + 1.05, 0, s + 0.45), d = P(lx - 1.05, 0, s + 0.45);
        if (a[2] < 0.2) continue;
        quad([a, b, c, d], "#6b4a2f");
      }
    }
    g.strokeStyle = "#c9ced6";
    for (const lx of LANES) for (const off of [-0.7, 0.7]){
      const a = P(lx + off, 0.08, z), b = P(lx + off, 0.08, far);
      g.lineWidth = Math.max(1.5, f * 0.07 / 1.2); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.stroke();
    }
    // obstacles + coins, far to near
    const items = [];
    for (let k = Math.floor(z / SEG); k < Math.floor(z / SEG) + 6; k++){
      const sg = segment(k);
      sg.obs.forEach(o => items.push(o));
      for (let c = 0; c < 6; c++) items.push({type: "coin", lane: sg.free, z0: k * SEG + 2 + c * 2.6, k, c});
    }
    items.sort((a, b) => (b.z0 + (b.len || 0)) - (a.z0 + (a.len || 0)));
    for (const o of items){
      const x = LANES[o.lane];
      if (o.type === "train"){
        const z0 = o.z0, z1 = o.z0 + o.len; if (z1 < z) continue;
        const zf = Math.max(z0, z - 0.6), w = 1.15, h = 3.3;
        const top = [P(x - w, h, zf), P(x + w, h, zf), P(x + w, h, z1), P(x - w, h, z1)];
        const sideX = x - w > camX ? x - w : x + w;
        if (Math.abs(sideX - camX) > 0.3) quad([P(sideX, 0, zf), P(sideX, 0, z1), P(sideX, h, z1), P(sideX, h, zf)], shade(o.color, 0.7));
        quad(top, shade(o.color, 1.15));
        if (z0 >= z - 0.6){
          const fr = [P(x - w, 0, z0), P(x + w, 0, z0), P(x + w, h, z0), P(x - w, h, z0)];
          quad(fr, o.color);
          const win = [P(x - w * 0.75, h * 0.55, z0), P(x + w * 0.75, h * 0.55, z0), P(x + w * 0.75, h * 0.85, z0), P(x - w * 0.75, h * 0.85, z0)];
          quad(win, "#1d2a3a");
          const l1 = P(x - w * 0.6, 0.6, z0), l2 = P(x + w * 0.6, 0.6, z0), r = Math.max(2, f * 0.12 / l1[2]);
          g.fillStyle = "#fff6c2"; g.beginPath(); g.arc(l1[0], l1[1], r, 0, 7); g.arc(l2[0], l2[1], r, 0, 7); g.fill();
        }
      } else if (o.type === "hurdle"){
        if (o.z0 < z - 0.5) continue;
        const w = 1.1, a = P(x - w, 0.5, o.z0), b = P(x + w, 0.5, o.z0), c = P(x + w, 1.2, o.z0), d = P(x - w, 1.2, o.z0);
        quad([P(x - w, 0, o.z0), P(x - w + 0.15, 0, o.z0), P(x - w + 0.15, 1.2, o.z0), P(x - w, 1.2, o.z0)], "#ddd");
        quad([P(x + w - 0.15, 0, o.z0), P(x + w, 0, o.z0), P(x + w, 1.2, o.z0), P(x + w - 0.15, 1.2, o.z0)], "#ddd");
        quad([a, b, c, d], "#f2f2f2");
        g.save(); g.beginPath(); g.moveTo(a[0], a[1]); g.lineTo(b[0], b[1]); g.lineTo(c[0], c[1]); g.lineTo(d[0], d[1]); g.closePath(); g.clip();
        g.fillStyle = "#e0392b"; const sw = (b[0] - a[0]) / 6;
        for (let i = 0; i < 6; i += 2) g.fillRect(a[0] + i * sw, d[1], sw, a[1] - d[1]);
        g.restore();
      } else {
        if (o.z0 < z + 3.8) continue;
        const p = P(x, 1.1 + Math.sin(t * 3 + o.c) * 0.08, o.z0), r = f * 0.32 / p[2], sx = Math.abs(Math.cos(t * 4 + o.c * 0.7));
        g.fillStyle = "#f5c518"; g.beginPath(); g.ellipse(p[0], p[1], Math.max(1, r * sx), r, 0, 0, 7); g.fill();
        g.fillStyle = "#fff3a8"; g.beginPath(); g.ellipse(p[0], p[1], Math.max(0.5, r * sx * 0.5), r * 0.55, 0, 0, 7); g.fill();
      }
    }
    // runner (original character): seen from behind
    const rx = runnerX(z + 2.2), base = P(rx, 0, z + 3.6), s = f * 0.62 / base[2];
    const cyc = Math.sin(t * 14), bx = base[0], by = base[1] - Math.abs(Math.sin(t * 7)) * s * 0.06;
    g.lineCap = "round";
    g.strokeStyle = "#1b2a49"; g.lineWidth = s * 0.22;
    g.beginPath(); g.moveTo(bx - s * 0.15, by - s * 0.95); g.lineTo(bx - s * 0.2 + cyc * s * 0.12, by - s * 0.05); g.stroke();
    g.beginPath(); g.moveTo(bx + s * 0.15, by - s * 0.95); g.lineTo(bx + s * 0.2 - cyc * s * 0.12, by - s * 0.05); g.stroke();
    g.fillStyle = "#f3f3f3"; g.fillRect(bx - s * 0.36 + cyc * s * 0.1, by - s * 0.12, s * 0.3, s * 0.14); g.fillRect(bx + s * 0.06 - cyc * s * 0.1, by - s * 0.12, s * 0.3, s * 0.14);
    g.fillStyle = "#14a3a3"; g.beginPath(); g.roundRect ? g.roundRect(bx - s * 0.42, by - s * 1.9, s * 0.84, s * 1.05, s * 0.2) : g.rect(bx - s * 0.42, by - s * 1.9, s * 0.84, s * 1.05); g.fill();
    g.strokeStyle = "#14a3a3"; g.lineWidth = s * 0.18;
    g.beginPath(); g.moveTo(bx - s * 0.4, by - s * 1.75); g.lineTo(bx - s * 0.62 - cyc * s * 0.15, by - s * 1.15); g.stroke();
    g.beginPath(); g.moveTo(bx + s * 0.4, by - s * 1.75); g.lineTo(bx + s * 0.62 + cyc * s * 0.15, by - s * 1.15); g.stroke();
    g.fillStyle = "#0e7f7f"; g.beginPath(); g.arc(bx, by - s * 2.15, s * 0.36, 0, 7); g.fill();
    g.fillStyle = "#f2a33a"; g.fillRect(bx - s * 0.12, by - s * 1.6, s * 0.24, s * 0.5);
  }

  /* ---------------- block parkour ---------------- */
  const STEP = 0.62;   // seconds per jump
  function block(i){
    if (i <= 0) return {x: 0, y: 0, z: 0, c: 0};
    const r = rng(i * 104729 + 7);
    const x = Math.sin(i * 0.37) * 2.2 + (hash(i, 1) - 0.5) * 1.2;
    const y = Math.round(Math.sin(i * 0.21) * 1.5 + (hash(i, 2) > 0.7 ? 1 : 0));
    return {x, y, z: i * 3.4, c: Math.floor(r() * 4)};
  }
  const BLOCK_COLORS = [["#5bb33a", "#7a5232"], ["#d8d0b0", "#a69467"], ["#7f8a95", "#5d666e"], ["#c9a44a", "#8d6f2a"]];
  function drawParkourScene(g, W, H, t){
    const i = Math.floor(t / STEP), k = t / STEP - i, a = block(i), b = block(i + 1);
    const ease = k * k * (3 - 2 * k);
    const cx = lerp(a.x, b.x, ease), cz = lerp(a.z, b.z, k), cy = lerp(a.y, b.y, ease) + 1.75 + Math.sin(k * Math.PI) * 1.1;
    const ahead = block(i + 3), yaw = Math.atan2(ahead.x - cx, ahead.z - cz), cs = Math.cos(yaw), sn = Math.sin(yaw);
    const hz = H * 0.42 + Math.sin(k * Math.PI) * H * 0.02, f = Math.min(W, H) * 0.95;
    const P = (x, y, z) => { const dx = x - cx, dz = z - cz; const rx = dx * cs - dz * sn, rz = dx * sn + dz * cs; const d = Math.max(0.05, rz); return [W / 2 + rx * f / d, hz + (cy - y) * f / d, d]; };
    let gr = g.createLinearGradient(0, 0, 0, H); gr.addColorStop(0, "#6fb6ff"); gr.addColorStop(0.45, "#bfe3ff"); gr.addColorStop(0.46, "#9dc7ef"); gr.addColorStop(1, "#1d3b6e");
    g.fillStyle = gr; g.fillRect(0, 0, W, H);
    g.fillStyle = "rgba(255,255,255,.85)";
    for (let c = 0; c < 7; c++){ const x = ((c * W * 0.37 - t * W * 0.01) % (W * 1.4) + W * 1.4) % (W * 1.4) - W * 0.2, y = H * (0.08 + hash(c, 9) * 0.18), w = W * (0.12 + hash(c, 4) * 0.12);
      g.fillRect(x, y, w, w * 0.18); g.fillRect(x + w * 0.2, y - w * 0.09, w * 0.55, w * 0.12); }
    g.fillStyle = "#fff6c8"; g.fillRect(W * 0.78, H * 0.07, W * 0.07, W * 0.07);
    const quad = (pts, fill) => { if (pts.some(p => p[2] < 0.12)) return; g.beginPath(); g.moveTo(pts[0][0], pts[0][1]); for (let n = 1; n < 4; n++) g.lineTo(pts[n][0], pts[n][1]); g.closePath(); g.fillStyle = fill; g.fill(); };
    const cube = (x, y, z, s, top, side) => {
      const x0 = x - s / 2, x1 = x + s / 2, z0 = z - s / 2, z1 = z + s / 2, y0 = y - s, y1 = y;
      if (cx < x0) quad([P(x0, y0, z0), P(x0, y0, z1), P(x0, y1, z1), P(x0, y1, z0)], shade(side, 0.8));
      if (cx > x1) quad([P(x1, y0, z0), P(x1, y0, z1), P(x1, y1, z1), P(x1, y1, z0)], shade(side, 0.8));
      quad([P(x0, y0, z0), P(x1, y0, z0), P(x1, y1, z0), P(x0, y1, z0)], side);
      quad([P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1)], top);
      // grass rim on the front face
      quad([P(x0, y1 - s * 0.18, z0), P(x1, y1 - s * 0.18, z0), P(x1, y1, z0), P(x0, y1, z0)], shade(top, 0.9));
    };
    // distant floating islands
    for (let n = 0; n < 6; n++){
      const iz = Math.floor(cz / 30) * 30 + 40 + n * 25, ix = (hash(n + Math.floor(cz / 30), 5) - 0.5) * 60, iy = -6 + hash(n, 7) * 8;
      cube(ix, iy, iz, 6, "#4f9e35", "#6b4a2e");
    }
    for (let j = i + 14; j >= Math.max(0, i - 1); j--){
      const bl = block(j), col = BLOCK_COLORS[bl.c];
      if (bl.z < cz - 1.5) continue;
      cube(bl.x, bl.y, bl.z, 1, col[0], col[1]);
    }
    g.fillStyle = "rgba(0,0,0,.25)"; g.fillRect(W / 2 - 1, H / 2 - 10, 2, 20); g.fillRect(W / 2 - 10, H / 2 - 1, 20, 2);
  }

  return {
    draw(kind, g, W, H, t){ (kind === "builtin-parkour" ? drawParkourScene : drawRunnerScene)(g, W, H, t); }
  };
})();
