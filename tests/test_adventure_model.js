const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const M = require('../main/web/adventure-model.js');

function advFiles(dir, out = []) {
  for (const e of fs.readdirSync(dir, {withFileTypes: true})) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) advFiles(p, out);
    else if (e.name.endsWith('.adv')) out.push(p);
  }
  return out;
}
const files = [...advFiles('sdcard_bundle/otr/adventure'), ...advFiles('docs/adventures')];
assert.ok(files.length >= 10);
let maps = 0;
for (const file of files) {
  const text = fs.readFileSync(file, 'utf8');
  const doc = M.parse(text);
  assert.equal(M.serialize(doc), text, file);
  for (const id of M.mapIds(doc)) {
    const grid = M.getGrid(doc, id);
    M.setGrid(doc, id, grid);
    M.usage(doc, id);
    M.listLegend(doc, id);
    M.listDark(doc, id);
    maps++;
  }
  assert.equal(M.serialize(doc), text, `${file} after no-op setGrid`);
}
assert.ok(maps >= 15);

const SRC = [
  '# kommentar', '@meta', 'id: t', '', '@map', '#####', '#S.p#', '#.o=#', '#####', '# fri kommentar',
  '@legend', 'g = gate 2  # kommentar', '@plategroup 1: 1,2 2,1', '@gategroup 1: 3,2',
  '@map 7', '....', '.S..', '@legend map=7', 'x = ice', '@dark map=7', '0,0 1,1', ''
].join('\n');

let doc = M.parse(SRC);
assert.deepEqual(M.mapIds(doc), [1, 7]);
assert.deepEqual(M.getGrid(doc, 1), ['#####', '#S.p#', '#.o=#', '#####']);
assert.deepEqual(M.getGrid(doc, 7), ['....', '.S..']);

// paint keeps comments, protects S, relocates S
M.paintCell(doc, 1, 2, 1, 'k');
assert.equal(M.getGrid(doc, 1)[1], '#Skp#');
assert.throws(() => M.paintCell(doc, 1, 1, 1, '#'), /Startrutan/);
M.paintCell(doc, 1, 1, 2, 'S');
assert.deepEqual(M.getGrid(doc, 1), ['#####', '#.kp#', '#So=#', '#####']);
assert.throws(() => M.paintCell(doc, 1, 9, 9, '#'), /utanför/);
assert.ok(M.serialize(doc).includes('# fri kommentar'));
assert.ok(M.serialize(doc).includes('g = gate 2  # kommentar'));

// moving a cell moves group coordinates with it (and swaps references to the target)
doc = M.parse(SRC);
M.moveCell(doc, 1, 3, 1, 2, 1);
assert.equal(M.getGrid(doc, 1)[1], '#Sp.#');
assert.ok(doc.lines.includes('@plategroup 1: 1,2 3,1'));
M.moveCell(doc, 1, 3, 2, 3, 1);
assert.ok(doc.lines.includes('@gategroup 1: 3,1'));
assert.throws(() => M.moveCell(doc, 1, 4, 4, 1, 1), /utanför/);
assert.throws(() => M.moveCell(doc, 1, 2, 1, 1, 1), /startrutan/);

// legend helper
doc = M.parse(SRC);
assert.deepEqual(M.listLegend(doc, 1).map(e => [e.glyph, e.value]), [['g', 'gate 2']]);
assert.deepEqual(M.listLegend(doc, 7).map(e => [e.glyph, e.value]), [['x', 'ice']]);
M.addLegend(doc, 1, 'v', {kind: 'belt', dir: 'left'});
M.addLegend(doc, 7, 'n', {kind: 'plate', group: 'a', color: 'red'});
assert.ok(doc.lines.includes('v = belt left') && doc.lines.includes('n = plate A red'));
assert.throws(() => M.addLegend(doc, 1, 'v', {kind: 'ice'}), /redan/);
assert.throws(() => M.addLegend(doc, 1, 'S', {kind: 'ice'}), /reserverad/);
assert.throws(() => M.addLegend(doc, 1, '@', {kind: 'ice'}), /reserverad/);
assert.throws(() => M.addLegend(doc, 1, 'w', {kind: 'key'}), /färg/);
assert.throws(() => M.addLegend(doc, 1, 'w', {kind: 'gate', group: '!'}), /Grupp/);
M.addLegend(doc, 1, 'w', {kind: 'door', color: 'blue'});
M.updateLegend(doc, 1, 'w', {kind: 'box', color: 'cyan'});
assert.ok(doc.lines.includes('w = box cyan'));
M.removeLegend(doc, 1, 'w');
assert.ok(!doc.lines.some(l => l.startsWith('w =')));
const fresh = M.parse('@map\n.S\n');
M.addLegend(fresh, 1, 'i', {kind: 'ice'});
assert.equal(M.serialize(fresh), '@map\n.S\n\n@legend\ni = ice\n');
assert.notEqual(M.suggestGlyph(doc, 1), '');
assert.ok(!['S', '#', ' ', 'g', 'v'].includes(M.suggestGlyph(doc, 1)));
const u = M.usage(M.parse('@map\nSgq\n@legend\ng = ice\nz2 = ice\n'), 1);
assert.deepEqual(u.undefinedGlyphs, ['q']);

// dark rectangles
doc = M.parse(SRC);
assert.deepEqual(M.listDark(doc, 7).map(r => [r.x1, r.y1, r.x2, r.y2]), [[0, 0, 1, 1]]);
M.addDark(doc, 7, {x1: 3, y1: 1, x2: 2, y2: 0});
assert.deepEqual(M.listDark(doc, 7).map(r => [r.x1, r.y1, r.x2, r.y2]), [[0, 0, 1, 1], [2, 0, 3, 1]]);
M.addDark(doc, 1, {x1: 1, y1: 1, x2: 2, y2: 2});
assert.ok(doc.lines.includes('@dark') && doc.lines.includes('1,1 2,2'));
assert.throws(() => M.addDark(doc, 1, {x1: 0, y1: 0, x2: 9, y2: 9}), /utanför/);
M.removeDark(doc, 7, 0);
assert.equal(M.listDark(doc, 7).length, 1);
const inline = M.parse('@map\nS.\n@dark map=1 0,0 1,0\n');
assert.equal(M.listDark(inline, 1).length, 1);
M.removeDark(inline, 1, 0);
assert.equal(M.listDark(inline, 1).length, 0);

// resize
doc = M.parse(SRC);
M.resizeMap(doc, 1, 6, 5);
assert.deepEqual(M.getGrid(doc, 1), ['#####.', '#S.p#.', '#.o=#.', '#####.', '......']);
M.resizeMap(doc, 1, 3, 4);
assert.ok(doc.lines.includes('@plategroup 1: 1,2 2,1') && !doc.lines.some(l => l.startsWith('@gategroup')));
assert.throws(() => M.resizeMap(doc, 1, 1, 1), /startrutan/);
assert.throws(() => M.setGrid(doc, 1, ['ab', 'c']), /rektangulär/);

// CRLF is preserved
const crlf = '@map\r\n.S\r\n';
const cd = M.parse(crlf);
M.paintCell(cd, 1, 0, 0, '#');
assert.equal(M.serialize(cd), '@map\r\n#S\r\n');
console.log(`Adventure model checks passed (${files.length} files, ${maps} maps round-tripped)`);
