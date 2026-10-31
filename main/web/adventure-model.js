/* Lossless .adv text model for the graphical editor. Only map rows, @legend lines, @dark rectangles
   and @plategroup/@gategroup coordinates are rewritten; every other line is kept byte for byte. */
(function (root) {
'use strict';

const MAX_W = 128, MAX_H = 128, MAX_MAPS = 4, MAX_LEGEND = 128, MAX_DARK = 16;
const COLORS = ['red', 'green', 'blue', 'yellow', 'purple', 'cyan'];
const RESERVED = new Set(['S', ' ', '\t', '@']);
const GROUP_RE = /^[0-9A-Za-z]$/;

const TERRAIN = [
  ['.', 'Gräs'], [',', 'Sand'], ['_', 'Golv'], ['#', 'Vägg'], ['%', 'Spricka'], ['T', 'Träd'],
  ['~', 'Vatten'], ['^', 'Lava'], ['o', 'Tryckplatta'], ['=', 'Grind'], ['Q', 'Checkpoint'],
  ['S', 'Start'], ['X', 'Utgång']
];
const OBJECTS = [
  ['R', 'Sten (gräs)'], ['r', 'Sten (golv)'], ['c', 'Låda (gräs)'], ['C', 'Låda (golv)'],
  ['k', 'Nyckel (gräs)'], ['K', 'Nyckel (golv)'], ['D', 'Dörr'], ['B', 'Båt'], ['*', 'Juvel (gräs)'],
  ['+', 'Juvel (golv)'], ['$', 'Juvel (sand)'], ['b', 'Buske'], ['p', 'Trolldryck'], ['h', 'Hjärta'],
  ['u', 'Dräkt'], ['l', 'Extraliv'], ['a', 'Bomb 3'], ['z', 'Bomb 4'], ['f', 'Ficklampa'],
  ['j', 'Hoppstylta'], ['m', 'Mina']
];

function parse(text) {
  const eol = text.includes('\r\n') ? '\r\n' : '\n';
  const trailing = /\r?\n$/.test(text);
  const body = trailing ? text.replace(/\r?\n$/, '') : text;
  return {eol, trailing, lines: text === '' ? [] : body.split(/\r?\n/)};
}
function serialize(doc) {
  return doc.lines.join(doc.eol) + (doc.trailing && doc.lines.length ? doc.eol : '');
}

function stripComment(s) {
  const text = s.trimStart();
  const i = text.indexOf(' #');
  return i < 0 ? text : text.slice(0, i);
}
function mapOf(text) {
  const m = /(?:^|\s)map=(\d+)(?=\s|:|$)/.exec(text);
  return m ? Number(m[1]) : 1;
}

// Sections: header line index, tag, arg text and the exclusive end index.
function sections(doc) {
  const out = [];
  doc.lines.forEach((line, i) => {
    const t = line.trim();
    if (t[0] !== '@') return;
    const sp = t.search(/[ \t]/);
    const tag = sp < 0 ? t : t.slice(0, sp);
    const arg = sp < 0 ? '' : t.slice(sp + 1).trim();
    if (out.length) out[out.length - 1].end = i;
    out.push({tag, arg, start: i, end: doc.lines.length});
  });
  return out;
}
function mapSections(doc) {
  return sections(doc).filter(s => s.tag === '@map').map(s => {
    const idText = s.arg.split('#', 1)[0].trim();
    return {...s, id: idText ? Number(idText) : 1};
  });
}
function mapIds(doc) { return mapSections(doc).map(s => s.id); }

function isRow(line) {
  const t = line.trim();
  return t !== '' && !(t[0] === '#' && /\s/.test(t[1] || ''));
}
function rowIndexes(doc, sec) {
  const rows = [];
  for (let i = sec.start + 1; i < sec.end; i++) if (isRow(doc.lines[i])) rows.push(i);
  return rows;
}
function findMap(doc, id) {
  const sec = mapSections(doc).find(s => s.id === id);
  if (!sec) throw new Error(`Karta ${id} finns inte.`);
  return sec;
}
function getGrid(doc, id) {
  return rowIndexes(doc, findMap(doc, id)).map(i => doc.lines[i]);
}
function setGrid(doc, id, rows) {
  const sec = findMap(doc, id);
  const idx = rowIndexes(doc, sec);
  const width = rows.length ? rows[0].length : 0;
  if (rows.length < 1 || rows.length > MAX_H || width < 1 || width > MAX_W || rows.some(r => r.length !== width))
    throw new Error(`Kartan måste vara rektangulär och högst ${MAX_W}×${MAX_H}.`);
  const common = Math.min(idx.length, rows.length);
  for (let k = 0; k < common; k++) doc.lines[idx[k]] = rows[k];
  if (rows.length > idx.length) {
    const at = (idx.length ? idx[idx.length - 1] : sec.start) + 1;
    doc.lines.splice(at, 0, ...rows.slice(idx.length));
  } else if (rows.length < idx.length) {
    for (let k = idx.length - 1; k >= rows.length; k--) doc.lines.splice(idx[k], 1);
  }
}

function legendGlyphs(doc, id) {
  return new Set(listLegend(doc, id).map(e => e.glyph));
}
function listLegend(doc, id) {
  const out = [];
  for (const s of sections(doc)) {
    if (s.tag !== '@legend' || mapOf(s.arg) !== id) continue;
    for (let i = s.start + 1; i < s.end; i++) {
      const raw = doc.lines[i];
      const t = stripComment(raw).trim();
      const eq = t.indexOf('=', t[0] === '=' ? 1 : 0);
      if (!t || eq < 0) continue;
      const glyph = t.slice(0, eq).trim();
      if (glyph.length !== 1) continue;
      out.push({glyph, value: t.slice(eq + 1).trim().replace(/\s+/g, ' '), line: i});
    }
  }
  return out;
}

function legendValue(def) {
  const kind = def.kind;
  const needGroup = () => {
    if (!GROUP_RE.test(String(def.group || ''))) throw new Error('Grupp måste vara 0–9 eller A–Z.');
    return String(def.group).toUpperCase();
  };
  const needColor = () => {
    if (!COLORS.includes(def.color)) throw new Error('Välj en färg.');
    return def.color;
  };
  switch (kind) {
    case 'plate':
      if (def.color) return def.group ? `plate ${needGroup()} ${needColor()}` : `plate ${needColor()}`;
      return def.group === undefined || def.group === '' ? 'plate' : `plate ${needGroup()}`;
    case 'gate': case 'lever': return `${kind} ${needGroup()}`;
    case 'key': case 'door': case 'box': return `${kind} ${needColor()}`;
    case 'belt':
      if (!['up', 'right', 'down', 'left'].includes(def.dir)) throw new Error('Välj bandets riktning.');
      return `belt ${def.dir}`;
    case 'ice': case 'skates': case 'suction': case 'poison': case 'swamp': case 'sharp_rocks': return kind;
    default: throw new Error('Okänd legendtyp.');
  }
}
function checkGlyph(glyph) {
  if (typeof glyph !== 'string' || glyph.length !== 1 || glyph.charCodeAt(0) <= 32 || glyph.charCodeAt(0) >= 127)
    throw new Error('Glyfen måste vara ett synligt ASCII-tecken.');
  if (RESERVED.has(glyph)) throw new Error(`Glyfen ${glyph} är reserverad.`);
}
function legendSection(doc, id) {
  return sections(doc).find(s => s.tag === '@legend' && mapOf(s.arg) === id);
}
function addLegend(doc, id, glyph, def) {
  checkGlyph(glyph);
  const value = legendValue(def);
  const existing = listLegend(doc, id);
  if (existing.some(e => e.glyph === glyph)) throw new Error(`Glyfen ${glyph} finns redan i legenden.`);
  if (existing.length >= MAX_LEGEND) throw new Error('Högst 128 legendposter per karta.');
  const line = `${glyph} = ${value}`;
  const sec = legendSection(doc, id);
  if (sec) {
    let at = sec.end;
    while (at > sec.start + 1 && doc.lines[at - 1].trim() === '') at--;
    doc.lines.splice(at, 0, line);
  } else {
    if (doc.lines.length && doc.lines[doc.lines.length - 1].trim() !== '') doc.lines.push('');
    doc.lines.push(id === 1 ? '@legend' : `@legend map=${id}`, line);
  }
}
function removeLegend(doc, id, glyph) {
  const e = listLegend(doc, id).find(x => x.glyph === glyph);
  if (!e) throw new Error(`Glyfen ${glyph} finns inte i legenden.`);
  doc.lines.splice(e.line, 1);
}
function updateLegend(doc, id, glyph, def) {
  const e = listLegend(doc, id).find(x => x.glyph === glyph);
  if (!e) throw new Error(`Glyfen ${glyph} finns inte i legenden.`);
  doc.lines[e.line] = `${glyph} = ${legendValue(def)}`;
}
function builtinGlyphs() {
  return new Set([...TERRAIN, ...OBJECTS].map(e => e[0]).concat('123456789'.split('')));
}
function resourceGlyphs(doc, id) {
  const out = new Set();
  for (const s of sections(doc)) {
    if (s.tag !== '@resources') continue;
    const header = mapOf(s.arg);
    for (let i = s.start + 1; i < s.end; i++) {
      const t = stripComment(doc.lines[i]).trim();
      if (!t) continue;
      const rowMap = /^map=(\d+):/.exec(t);
      const body = rowMap ? t.slice(rowMap[0].length).trim() : t;
      if ((rowMap ? Number(rowMap[1]) : header) !== id) continue;
      const glyph = body.split(':')[0].trim();
      if (glyph.length === 1) out.add(glyph);
    }
  }
  return out;
}
function suggestGlyph(doc, id) {
  const used = new Set([...builtinGlyphs(), ...legendGlyphs(doc, id), ...resourceGlyphs(doc, id), ...RESERVED]);
  for (const c of 'abdeghijnqstvwxyABEFGHIJLMNOPUVWYZ&!?:;<>[]{}|0') if (!used.has(c)) return c;
  for (let code = 33; code < 127; code++) {
    const c = String.fromCharCode(code);
    if (!used.has(c) && c !== '#') return c;
  }
  return '';
}

// Glyph usage on a grid: counts, and glyphs that no built-in/legend/resource entry explains.
function usage(doc, id) {
  const known = new Set([...builtinGlyphs(), ...legendGlyphs(doc, id), ...resourceGlyphs(doc, id)]);
  const counts = {};
  for (const row of getGrid(doc, id)) for (const c of row) counts[c] = (counts[c] || 0) + 1;
  const undefinedGlyphs = Object.keys(counts).filter(c => !known.has(c));
  const unused = listLegend(doc, id).map(e => e.glyph).filter(g => !counts[g]);
  return {counts, undefinedGlyphs, unused};
}

function darkSections(doc, id) {
  return sections(doc).filter(s => s.tag === '@dark');
}
function parseRect(text) {
  const m = /^(\d+)\s*,\s*(\d+)\s+(\d+)\s*,\s*(\d+)$/.exec(text.trim());
  return m ? {x1: +m[1], y1: +m[2], x2: +m[3], y2: +m[4]} : null;
}
function listDark(doc, id) {
  const out = [];
  for (const s of darkSections(doc)) {
    const headerArg = stripComment(s.arg).replace(/(?:^|\s)map=\d+/, '').trim();
    const headerMap = mapOf(s.arg);
    const inline = headerArg ? parseRect(headerArg) : null;
    if (inline && headerMap === id) out.push({...inline, line: s.start, inline: true});
    for (let i = s.start + 1; i < s.end; i++) {
      let t = stripComment(doc.lines[i]).trim();
      if (!t) continue;
      let map = headerMap;
      const rowMap = /^map=(\d+):\s*/.exec(t);
      if (rowMap) { map = Number(rowMap[1]); t = t.slice(rowMap[0].length); }
      const rect = parseRect(t);
      if (rect && map === id) out.push({...rect, line: i, inline: false});
    }
  }
  return out;
}
function addDark(doc, id, rect) {
  if (listDark(doc, id).length >= MAX_DARK) throw new Error('Högst 16 mörka områden per karta.');
  const grid = getGrid(doc, id);
  const x1 = Math.min(rect.x1, rect.x2), x2 = Math.max(rect.x1, rect.x2);
  const y1 = Math.min(rect.y1, rect.y2), y2 = Math.max(rect.y1, rect.y2);
  if (x1 < 0 || y1 < 0 || x2 >= grid[0].length || y2 >= grid.length) throw new Error('Området ligger utanför kartan.');
  const line = `${x1},${y1} ${x2},${y2}`;
  const sec = darkSections(doc).find(s => mapOf(s.arg) === id && !stripComment(s.arg).replace(/(?:^|\s)map=\d+/, '').trim());
  if (sec) {
    let at = sec.end;
    while (at > sec.start + 1 && doc.lines[at - 1].trim() === '') at--;
    doc.lines.splice(at, 0, line);
  } else {
    if (doc.lines.length && doc.lines[doc.lines.length - 1].trim() !== '') doc.lines.push('');
    doc.lines.push(id === 1 ? '@dark' : `@dark map=${id}`, line);
  }
}
function removeDark(doc, id, index) {
  const rect = listDark(doc, id)[index];
  if (!rect) throw new Error('Det mörka området finns inte.');
  if (rect.inline) {
    const t = doc.lines[rect.line];
    const sel = /(?:^|\s)map=\d+/.exec(t);
    doc.lines[rect.line] = sel ? `@dark${sel[0].startsWith(' ') ? sel[0] : ' ' + sel[0]}` : '@dark';
  } else {
    doc.lines.splice(rect.line, 1);
  }
}

const GROUP_LINE = /^(\s*@)(plategroup|gategroup)(\s+)(?:(map=(\d+)\s+))?([0-9A-Za-z])(\s+map=(\d+))?(\s*:\s*)([^#]*?)(\s+#.*|\s*)$/;
function remapGroupCells(doc, id, fn, includeGroup = () => true) {
  for (let i = doc.lines.length - 1; i >= 0; i--) {
    const m = GROUP_LINE.exec(doc.lines[i]);
    if (!m || ((m[5] || m[8]) ? Number(m[5] || m[8]) : 1) !== id || !includeGroup(m[2])) continue;
    const cells = m[10].trim().split(/\s+/).filter(Boolean).map(tok => {
      const c = /^(\d+),(\d+)$/.exec(tok);
      if (!c) return tok;
      const r = fn(+c[1], +c[2]);
      return r ? `${r[0]},${r[1]}` : null;
    }).filter(t => t !== null);
    // A group line without cells is not valid, so it is removed together with its last cell.
    if (!cells.length) doc.lines.splice(i, 1);
    else doc.lines[i] = `${m[1]}${m[2]}${m[3]}${m[4] || ''}${m[6]}${m[7] || ''}${m[9]}${cells.join(' ')}${m[11].trim() ? m[11] : ''}`;
  }
}

function supportsGroupCell(doc, id, group, glyph) {
  const kind = group === 'plategroup' ? 'plate' : 'gate';
  if (glyph === (kind === 'plate' ? 'o' : '=')) return true;
  return listLegend(doc, id).some(e => e.glyph === glyph && e.value.split(/\s+/)[0] === kind);
}
function checkCell(grid, x, y) {
  if (y < 0 || y >= grid.length || x < 0 || x >= grid[0].length) throw new Error('Rutan ligger utanför kartan.');
}
function withChar(row, x, c) { return row.slice(0, x) + c + row.slice(x + 1); }
// Paint one glyph. 'S' relocates the start cell; the start cell itself is protected.
function paintCell(doc, id, x, y, glyph) {
  const grid = getGrid(doc, id);
  checkCell(grid, x, y);
  if (glyph !== 'S' && RESERVED.has(glyph) && glyph !== ' ') throw new Error(`Glyfen ${glyph} kan inte ritas.`);
  if (glyph.length !== 1 || glyph.charCodeAt(0) <= 32 || glyph.charCodeAt(0) >= 127) throw new Error('Ogiltig glyf.');
  if (grid[y][x] === 'S' && glyph !== 'S') throw new Error('Startrutan S kan inte skrivas över; flytta den istället.');
  const previous = grid[y][x];
  if (glyph === 'S') {
    for (let r = 0; r < grid.length; r++) {
      const at = grid[r].indexOf('S');
      if (at >= 0) grid[r] = withChar(grid[r], at, '.');
    }
  }
  grid[y] = withChar(grid[y], x, glyph);
  setGrid(doc, id, grid);
  if (previous !== glyph) {
    remapGroupCells(doc, id, (cx, cy) => cx === x && cy === y ? null : [cx, cy],
      group => !supportsGroupCell(doc, id, group, glyph));
  }
}
// Move the glyph at (fx,fy) to (tx,ty); the origin becomes floor. Group coordinates follow the cell.
function moveCell(doc, id, fx, fy, tx, ty) {
  const grid = getGrid(doc, id);
  checkCell(grid, fx, fy); checkCell(grid, tx, ty);
  if (fx === tx && fy === ty) return;
  const glyph = grid[fy][fx];
  if (glyph === '.') throw new Error('Det finns inget att flytta på en tom golvruta.');
  if (grid[ty][tx] === 'S') throw new Error('Målrutan är startrutan.');
  grid[fy] = withChar(grid[fy], fx, '.');
  grid[ty] = withChar(grid[ty], tx, glyph);
  setGrid(doc, id, grid);
  remapGroupCells(doc, id, (x, y) => {
    if (x === fx && y === fy) return [tx, ty];
    if (x === tx && y === ty) return null;
    return [x, y];
  });
}
const FILLS = ['.', ',', '_', '#', '~', '^', 'T'];
function resizeMap(doc, id, width, height, fill) {
  const grid = getGrid(doc, id);
  const f = fill || '.';
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width > MAX_W || height > MAX_H)
    throw new Error(`Storleken måste vara 1–${MAX_W} i båda riktningar.`);
  if (!FILLS.includes(f)) throw new Error('Ogiltig fyllnadsglyf.');
  const next = [];
  for (let y = 0; y < height; y++) {
    const row = y < grid.length ? grid[y] : '';
    next.push(row.slice(0, width).padEnd(width, f));
  }
  if (!next.some(r => r.includes('S'))) throw new Error('Den nya storleken skulle ta bort startrutan S.');
  setGrid(doc, id, next);
  remapGroupCells(doc, id, (x, y) => (x < width && y < height ? [x, y] : null));
}

// A walled room with S in the top-left corner and, for the main map, an exit X in the opposite corner.
function blankGrid(width, height, withExit = true) {
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 4 || height < 4 || width > MAX_W || height > MAX_H)
    throw new Error(`Storleken måste vara 4–${MAX_W} i båda riktningar.`);
  const rows = [];
  for (let y = 0; y < height; y++) {
    rows.push(y === 0 || y === height - 1 ? '#'.repeat(width) : '#' + '.'.repeat(width - 2) + '#');
  }
  rows[1] = rows[1].slice(0, 1) + 'S' + rows[1].slice(2);
  if (withExit) {
    const last = rows[height - 2];
    rows[height - 2] = last.slice(0, width - 2) + 'X' + last.slice(width - 1);
  }
  return rows;
}
function oneLine(text, max) {
  return String(text || '').replace(/[\r\n#]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, max);
}
// Safe folder and file stems: ASCII letters, digits, underscore and hyphen.
function validStem(name) { return /^[A-Za-z0-9_-]{1,40}$/.test(name) && !['saves', 'maps', 'bitmaps'].includes(name.toLowerCase()); }
function newAdventure(opts) {
  const id = oneLine(opts.id, 40);
  if (!validStem(id)) throw new Error('Namnet får bara innehålla A–Z, a–z, 0–9, _ och - (högst 40 tecken).');
  const title = oneLine(opts.title, 60) || id;
  const mapId = Number.isInteger(opts.mapId) && opts.mapId >= 0 && opts.mapId <= 255 ? opts.mapId : 90;
  const lines = [
    '@meta', `id: ${id}`, `world: ${mapId}`, `map_id: ${mapId}`, '',
    '@about', `title.sv: ${title}`, `title.en: ${title}`, 'desc.sv: Nytt äventyr.', 'desc.en: New adventure.', '',
    '@rules', 'start_energy: 8', 'energy_cap: 12', 'lives: 3', '',
    '@map', ...blankGrid(opts.width, opts.height), ''
  ];
  return lines.join('\n');
}
// Stable 90..239 id from a name, so new files rarely share a save identity with the built-in worlds.
function suggestMapId(name) {
  let h = 0;
  for (const c of String(name)) h = (h * 31 + c.charCodeAt(0)) >>> 0;
  return 90 + (h % 150);
}
function nextMapId(doc) {
  const used = new Set(mapIds(doc));
  for (let id = 2; id <= 255; id++) if (!used.has(id)) return id;
  return 0;
}
function addMap(doc, id, width, height) {
  const ids = mapIds(doc);
  if (!Number.isInteger(id) || id < 2 || id > 255) throw new Error('Kart-id måste vara 2–255.');
  if (ids.includes(id)) throw new Error(`Karta ${id} finns redan.`);
  if (ids.length >= MAX_MAPS) throw new Error('En fil kan ha högst 4 kartor.');
  const rows = blankGrid(width, height, false);
  if (doc.lines.length && doc.lines[doc.lines.length - 1].trim() !== '') doc.lines.push('');
  doc.lines.push(`@map ${id}`, ...rows);
}

const api = {
  newAdventure, suggestMapId, nextMapId, addMap, blankGrid, validStem, MAX_MAPS, FILLS,
  MAX_W, MAX_H, MAX_LEGEND, MAX_DARK, COLORS, TERRAIN, OBJECTS,
  parse, serialize, sections, mapIds, getGrid, setGrid,
  listLegend, resourceGlyphs, addLegend, removeLegend, updateLegend, legendValue, suggestGlyph, usage,
  listDark, addDark, removeDark, paintCell, moveCell, resizeMap
};
if (typeof module !== 'undefined' && module.exports) module.exports = api;
else root.AdvModel = api;
})(typeof self !== 'undefined' ? self : this);
