
const fs = require('fs');
const src = fs.readFileSync('lab/wifidrv1/wifidrv1.c', 'utf8');
const lines = src.split('\n');
// strip comments/strings so prose cannot look like a use
const clean = lines.map(l => l.replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/.*/,'').replace(/"(\\.|[^"\\])*"/g,'""'));
// file-scope declarations: 'static <type> ... <name>' at column 0 (variable or function)
const decls = new Map();
clean.forEach((l, i) => {
  if (!/^static\s/.test(l)) return;
  const m = l.match(/^static\s+[A-Za-z_][A-Za-z0-9_\s\*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*(=|;|\()/);
  if (!m) return;
  const name = m[1];
  if (['const','unsigned','struct','void','int','u32','bool','char','size_t','dma_addr_t','in'].includes(name)) return;
  if (!decls.has(name)) decls.set(name, i + 1);
});
let bad = 0, checked = 0;
for (const [name, dline] of decls) {
  let first = -1;
  for (let i = 0; i < clean.length; i++) {
    if (i + 1 === dline) continue;
    if (new RegExp('\\b' + name + '\\b').test(clean[i])) { first = i + 1; break; }
  }
  if (first === -1) continue;
  checked++;
  if (first < dline) { bad++; console.log('  !! ' + name + ' used at line ' + first + ' BEFORE its declaration at ' + dline); }
}
console.log('file-scope declarations checked:', checked, '| used-before-declared:', bad);
console.log('braces', (src.match(/{/g)||[]).length-(src.match(/}/g)||[]).length,
            'parens', (src.match(/\(/g)||[]).length-(src.match(/\)/g)||[]).length);
const w = [...src.matchAll(/(iowrite32|omo_wr)\([^;]*/g)].map(m=>m[0]);
console.log('writes:', w.length, '| touching out[5]:', w.filter(x=>/3f12f0|400392f0|MSG5/.test(x)).length);
