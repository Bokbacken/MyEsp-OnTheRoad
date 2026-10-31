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

const sectionDoc = M.parse('@meta\nid: old\n\n@about\ntitle.sv: Gammal\n@map\nS.\n');
const metaSection = M.sections(sectionDoc).find(s => s.tag === '@meta');
M.replaceSection(sectionDoc, metaSection.start, 'id: new\nworld: 42');
assert.equal(M.serialize(sectionDoc), '@meta\nid: new\nworld: 42\n@about\ntitle.sv: Gammal\n@map\nS.\n');
assert.throws(() => M.replaceSection(sectionDoc, metaSection.start, '@about\ninvalid'), /direktivrad/);

const hashLegend = M.parse('@map 7 #note\n.S\n@legend map=7\n# = ice\nx = ice #note\n');
assert.deepEqual(M.mapIds(hashLegend), [7]);
assert.deepEqual(M.listLegend(hashLegend, 7).map(e => [e.glyph, e.value]), [['#', 'ice'], ['x', 'ice']]);
assert.throws(() => M.addLegend(hashLegend, 7, '#', {kind: 'ice'}), /redan/);

// paint keeps comments, protects S, relocates S
M.paintCell(doc, 1, 2, 1, 'k');
assert.equal(M.getGrid(doc, 1)[1], '#Skp#');
assert.throws(() => M.paintCell(doc, 1, 1, 1, '#'), /Startrutan/);
M.paintCell(doc, 1, 1, 2, 'S');
assert.deepEqual(M.getGrid(doc, 1), ['#####', '#.kp#', '#So=#', '#####']);
assert.throws(() => M.paintCell(doc, 1, 9, 9, '#'), /utanför/);
assert.ok(M.serialize(doc).includes('# fri kommentar'));
assert.ok(M.serialize(doc).includes('g = gate 2  # kommentar'));

// Painting over grouped cells removes references that no longer match their glyph type.
const grouped = M.parse('@map\nSo=.\n@plategroup 1: 1,0\n@gategroup 2: 2,0\n');
M.paintCell(grouped, 1, 1, 0, '#');
M.paintCell(grouped, 1, 2, 0, 'o');
assert.deepEqual(M.getGrid(grouped, 1), ['S#o.']);
assert.ok(!grouped.lines.some(l => l.startsWith('@plategroup') || l.startsWith('@gategroup')));
const groupedLegend = M.parse('@map\nSop.\n@legend\np = plate red\n@plategroup 1: 1,0\n');
M.paintCell(groupedLegend, 1, 1, 0, 'p');
assert.ok(groupedLegend.lines.includes('@plategroup 1: 1,0'));

// Moving a cell moves its own group coordinate and removes an overwritten target reference.
doc = M.parse(SRC);
M.moveCell(doc, 1, 3, 1, 2, 1);
assert.equal(M.getGrid(doc, 1)[1], '#Sp.#');
assert.ok(doc.lines.includes('@plategroup 1: 1,2'));
M.moveCell(doc, 1, 3, 2, 3, 1);
assert.ok(doc.lines.includes('@gategroup 1: 3,1'));
assert.throws(() => M.moveCell(doc, 1, 4, 4, 1, 1), /utanför/);
assert.throws(() => M.moveCell(doc, 1, 2, 1, 1, 1), /startrutan/);
const selectedGroup = M.parse('@map 7 #note\n..So\n....\n@plategroup map=7 1: 3,0\n');
M.moveCell(selectedGroup, 7, 3, 0, 1, 1);
assert.ok(selectedGroup.lines.includes('@plategroup map=7 1: 1,1'));
const selectedGroupEdit = M.parse('@map 7\nSoo\n...\n@plategroup map=7 1: 1,0 2,0 # keep\n');
M.updateGroup(selectedGroupEdit, 7, 0, [[2, 0]]);
assert.ok(selectedGroupEdit.lines.includes('@plategroup map=7 1: 2,0 # keep'));
const selectedGate = M.parse('@map 7\n.S..\n..=.\n@gategroup 2 map=7: 2,1\n');
M.resizeMap(selectedGate, 7, 2, 2);
assert.ok(!selectedGate.lines.some(l => l.startsWith('@gategroup')));
const groupEdit = M.parse('@map\nSoo\n...\n@plategroup 1: 1,0 2,0\n');
assert.deepEqual(M.listGroups(groupEdit, 1).map(g => [g.kind, g.group, g.cells]), [['plategroup', '1', [[1, 0], [2, 0]]]]);
M.updateGroup(groupEdit, 1, 0, [[2, 0]]);
assert.ok(groupEdit.lines.includes('@plategroup 1: 2,0'));
assert.throws(() => M.updateGroup(groupEdit, 1, 0, [[0, 0]]), /unika plattor eller grindar/);
M.removeGroup(groupEdit, 1, 0);
assert.ok(!groupEdit.lines.some(l => l.startsWith('@plategroup')));

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
const resources = M.parse('@map\nS.\n@map 7\nS.\n@resources\nr : robe : Robe : Robe : 1 : robe.png\n@resources map=7\nmap=1: q : wrong : Wrong : 1 : wrong.png\nq : key : Key : Key : 1 : key.png\n');
assert.deepEqual(M.listResources(resources, 1).map(r => [r.glyph, r.id, r.image]), [['r', 'robe', 'robe.png'], ['q', 'wrong', '']]);
assert.deepEqual(M.listResources(resources, 7).map(r => [r.glyph, r.id, r.image]), [['q', 'key', 'key.png']]);
assert.deepEqual([...M.resourceGlyphs(resources, 1)], ['r', 'q']);
const newNode = M.parse('@map\nS.\n');
M.addSection(newNode, 'node', 'jakob.start', 'speaker: jakob\ntext.sv: Hej.\ntext.en: Hello.\nchoice: Berätta mer | Tell me more => jakob.where');
assert.ok(M.serialize(newNode).includes('@node jakob.start\nspeaker: jakob\ntext.sv: Hej.\ntext.en: Hello.\nchoice: Berätta mer | Tell me more => jakob.where'));
assert.throws(() => M.addSection(newNode, 'node', '', 'text.sv: Saknas id'), /behöver ett id/);
assert.throws(() => M.addSection(newNode, 'unknown'), /kan inte läggas till/);
assert.throws(() => M.addSection(newNode, 'node', 'bad', '@npc wrong'), /direktivrad/);
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
const inline = M.parse('@map\nS.\n@dark map=1 0,0 1,0 # keep\n');
assert.equal(M.listDark(inline, 1).length, 1);
M.updateDark(inline, 1, 0, {x1: 0, y1: 0, x2: 1, y2: 0});
assert.equal(M.listDark(inline, 1)[0].x2, 1);
assert.ok(inline.lines.includes('@dark map=1 0,0 1,0 # keep'));
M.removeDark(inline, 1, 0);
assert.equal(M.listDark(inline, 1).length, 0);

// resize
doc = M.parse(SRC);
M.resizeMap(doc, 1, 6, 5);
assert.deepEqual(M.getGrid(doc, 1), ['#####.', '#S.p#.', '#.o=#.', '#####.', '......']);
M.resizeMap(doc, 1, 6, 5, '#');
assert.deepEqual(M.getGrid(doc, 1), ['#####.', '#S.p#.', '#.o=#.', '#####.', '......']);
M.resizeMap(doc, 1, 3, 4);
assert.ok(doc.lines.includes('@plategroup 1: 1,2 2,1') && !doc.lines.some(l => l.startsWith('@gategroup')));
assert.throws(() => M.resizeMap(doc, 1, 1, 1), /startrutan/);
assert.throws(() => M.setGrid(doc, 1, ['ab', 'c']), /rektangulär/);

// new files, extra maps, size limits
const fresh2 = M.newAdventure({id: 'min_bana', title: 'Min # bana\nrad', mapId: 123, width: 128, height: 128});
const fd = M.parse(fresh2);
assert.deepEqual(M.mapIds(fd), [1]);
const fg = M.getGrid(fd, 1);
assert.equal(fg.length, 128);
assert.ok(fg.every(r => r.length === 128));
assert.equal(fg.join('').split('S').length - 1, 1);
assert.equal(fg[1][1], 'S');
assert.equal(fg[126][126], 'X');
assert.ok(fresh2.includes('title.sv: Min bana rad') && fresh2.includes('map_id: 123'));
assert.ok(new TextEncoder().encode(fresh2).length < 65536);
assert.throws(() => M.newAdventure({id: 'x', width: 129, height: 10}), /Storleken/);
assert.throws(() => M.newAdventure({id: 'x', width: 3, height: 10}), /Storleken/);
assert.throws(() => M.newAdventure({id: '../x', width: 8, height: 8}), /Namnet/);
assert.throws(() => M.newAdventure({id: 'saves', width: 8, height: 8}), /Namnet/);
assert.ok(M.suggestMapId('abc') >= 90 && M.suggestMapId('abc') <= 239);
assert.equal(M.suggestMapId('abc'), M.suggestMapId('abc'));
const small = M.parse(M.newAdventure({id: 'a', width: 4, height: 4}));
assert.deepEqual(M.getGrid(small, 1), ['####', '#S.#', '#.X#', '####']);
assert.equal(M.nextMapId(small), 2);
M.addMap(small, 2, 6, 5);
M.addMap(small, 9, 4, 4);
assert.deepEqual(M.mapIds(small), [1, 2, 9]);
assert.ok(!M.getGrid(small, 2).join('').includes('X'));
assert.equal(M.nextMapId(small), 3);
assert.throws(() => M.addMap(small, 2, 4, 4), /redan/);
assert.throws(() => M.addMap(small, 1, 4, 4), /2–255/);
M.addMap(small, 3, 4, 4);
assert.throws(() => M.addMap(small, 4, 4, 4), /högst 4/);
assert.equal(M.getGrid(M.parse(M.serialize(small)), 9).length, 4);
const big = M.parse(M.newAdventure({id: 'b', width: 128, height: 128}));
M.resizeMap(big, 1, 128, 128, '~');
assert.throws(() => M.resizeMap(big, 1, 129, 128), /1–128/);
assert.throws(() => M.resizeMap(big, 1, 0, 8), /1–128/);
assert.throws(() => M.resizeMap(big, 1, 130, 8, '.'), /1–128/);
assert.throws(() => M.resizeMap(big, 1, 100, 100, 'Z'), /fyllnad/);
const grow = M.parse(M.newAdventure({id: 'g', width: 10, height: 10}));
M.resizeMap(grow, 1, 12, 11, '~');
assert.equal(M.getGrid(grow, 1)[10][11], '~');
assert.equal(M.getGrid(grow, 1)[5][10], '~');
assert.throws(() => M.setGrid(big, 1, Array(129).fill('S'.repeat(4))), /128×128/);

// CRLF is preserved
const crlf = '@map\r\n.S\r\n';
const cd = M.parse(crlf);
M.paintCell(cd, 1, 0, 0, '#');
assert.equal(M.serialize(cd), '@map\r\n#S\r\n');
if (process.env.ADV_SAMPLE_DIR) {
  fs.mkdirSync(process.env.ADV_SAMPLE_DIR, {recursive: true});
  for (const [name, w, h] of [['min', 4, 4], ['mid', 40, 30], ['max', 128, 128], ['wide', 128, 4], ['tall', 4, 128]]) {
    fs.writeFileSync(path.join(process.env.ADV_SAMPLE_DIR, `${name}.adv`), M.newAdventure({id: name, title: name, mapId: M.suggestMapId(name), width: w, height: h}));
  }
  const multi = M.parse(M.newAdventure({id: 'multi', width: 20, height: 12}));
  M.addMap(multi, 2, 128, 128);
  fs.writeFileSync(path.join(process.env.ADV_SAMPLE_DIR, 'multi.adv'), M.serialize(multi));
}
console.log(`Adventure model checks passed (${files.length} files, ${maps} maps round-tripped)`);
