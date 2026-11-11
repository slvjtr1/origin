// Holds the native scoring (best_test) against the web app's own bestRate / pickBest, run here in Node on the
// same captured stream lists. Usage: node tests/best/compare.js <web app dir (holds index.html, player.js)> [-v]
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm'), cp = require('child_process');
const dir = process.argv[2], verbose = process.argv.includes('-v');
if (!dir) { console.error('usage: compare.js <web app dir> [-v]'); process.exit(2); }
const page = fs.readFileSync(path.join(dir, 'index.html'), 'utf8'), player = fs.readFileSync(path.join(dir, 'player.js'), 'utf8');
function fn(src, name) { const i = src.indexOf('function ' + name + '('); if (i < 0) throw new Error('no ' + name);
  let d = 0, k = src.indexOf('{', i); for (; k < src.length; k++) { if (src[k] === '{') d++; else if (src[k] === '}' && --d === 0) break; } return src.slice(i, k + 1); }
const code = ['streamInfo', 'isP2pUrl', 'sizeMB', 'seedsOf', 'bestRate', 'pickBest'].map(n => fn(page, n)).join('\n') + '\n' +
  (/var RES_N=\{[^}]*\};/.exec(page)[0]) + '\n' + ['resOf', 'hdrOf', 'audioOf'].map(n => fn(player, n)).join('\n') +
  '\nwindow.streamTags={resOf:resOf,hdrOf:hdrOf,audioOf:audioOf};';
const ctx = { window: {}, TVC: null, netMbps: () => 0, T: (s, o) => o ? s.replace(/\{(\w+)\}/g, (m, k) => o[k] != null ? o[k] : m) : s };
vm.createContext(ctx); vm.runInContext(code, ctx);
// the streams as the web app reads an addon's answer (fetchStreams)
function webList(fx) { const out = []; for (const ad of fx.addons) for (const s of ad.streams) { let url = s.url;
    if (!url && s.infoHash) url = 'http://127.0.0.1:8080/p2p?ih=' + s.infoHash + '&idx=' + (s.fileIdx == null ? '' : s.fileIdx);
    if (!url) continue; out.push({ addon: ad.name, name: s.name || '', title: s.title || s.description || '', url, behaviorHints: s.behaviorHints ? { filename: s.behaviorHints.filename || '' } : null }); }
  return out; }
const TVS = [ // [label, caps]
  ['FHD webOS 4 (DTS)', { maxRes: 1080, hdr: false, dv: false, av1: false, dts: true, truehd: false }],
  ['4K HDR + DV, AV1', { maxRes: 2160, hdr: true, dv: true, av1: true, dts: false, truehd: false }],
  ['4K HDR, no DV, DTS', { maxRes: 2160, hdr: true, dv: false, av1: false, dts: true, truehd: false }],
  ['4K SDR, nothing else', { maxRes: 2160, hdr: false, dv: false, av1: false, dts: false, truehd: false }]];
const SPEEDS = [0, 12, 30, 80, 250];
const bin = path.join(__dirname, '../../build/best_test'), fdir = path.join(__dirname, 'fixtures');
let cases = 0, rateDiffs = 0, pickDiffs = 0, picksShown = 0;
const close = (a, b) => Math.abs(a - b) < 1e-6 * Math.max(1, Math.abs(a));
for (const f of fs.readdirSync(fdir).filter(x => x.endsWith('.json')).sort()) {
  const fx = JSON.parse(fs.readFileSync(path.join(fdir, f))), list = webList(fx);
  console.log('\n## ' + f.replace('.json', '') + '  (' + list.length + ' streams, ' + fx.dur + ' min)');
  for (const [tvl, c] of TVS) for (const mbps of SPEEDS) {
    cases++;
    const nat = JSON.parse(cp.execFileSync(bin, [path.join(fdir, f), c.maxRes, +c.hdr, +c.dv, +c.av1, +c.dts, +c.truehd, mbps], { encoding: 'utf8' }));
    for (let i = 0; i < list.length; i++) { const w = ctx.bestRate(list[i], c, mbps, fx.dur), n = nat.rates[i];
      const same = (!w && !n) || (w && n && close(w.sc, n[0]) && close(w.need, n[1]) && close(w.cap, n[2]) && w.sum === n[3]);
      if (!same) { rateDiffs++; if (rateDiffs <= 25) console.log('  RATE DIFF #' + i + ' ' + tvl + ' ' + mbps + ' Mbps\n    web ' + (w ? [w.sc, w.need, w.cap, w.sum].join(' | ') : 'out') + '\n    nat ' + (n ? n.join(' | ') : 'out') + '\n    ' + ctx.streamInfo(list[i]).slice(0, 200)); } }
    const wb = ctx.pickBest(list, fx.dur, c, mbps), wi = wb ? list.indexOf(wb.s) : -1;
    if (wi !== nat.pick) { pickDiffs++; console.log('  PICK DIFF ' + tvl + ' ' + mbps + ' Mbps: web #' + wi + ' native #' + nat.pick); }
    if (verbose || mbps === 30 || mbps === 0) { picksShown++;
      console.log('  ' + (wi === nat.pick ? 'same' : 'DIFF') + '  ' + tvl.padEnd(20) + String(mbps || 'unmeasured').padStart(10) + (mbps ? ' Mbps' : '     ') + '  #' + String(nat.pick).padEnd(3) +
        (wb ? ' sc ' + wb.sc.toFixed(1).padStart(6) + '  ' + wb.sum + '\n' + ' '.repeat(48) + ctx.streamInfo(wb.s).slice(0, 110) : ' nothing suits this TV')); }
  }
}
console.log('\n' + cases + ' cases: ' + rateDiffs + ' stream scores differ, ' + pickDiffs + ' picks differ');
process.exit(rateDiffs || pickDiffs ? 1 : 0);
