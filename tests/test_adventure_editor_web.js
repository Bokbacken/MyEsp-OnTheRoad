const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const page = fs.readFileSync('main/web/adventure-editor.html', 'utf8');
assert.match(page, /id="source"/);
assert.match(page, /id="browse"/);
assert.match(page, /id="validate"/);
assert.match(page, /id="save"/);
assert.match(page, /kommentarer och syntax/i);
assert.match(page, /\/api\/adventure\/validate/);
assert.match(page, /\/api\/fs\/download/);
assert.match(page, /\/api\/fs\/upload/);
assert.match(page, /overwrite:'1'/);
assert.match(page, /\/otr\/adventure\//);

assert.match(page, /<script src="\/adventure-model\.js"><\/script>/);
assert.match(page, /id="map-canvas"/);
assert.match(page, /id="treasure-item"/);
assert.match(page, /id="treasure-amount"/);
assert.match(page, /id="treasure-place"/);
assert.match(page, /M\.addTreasure\(doc, mapId, pos\.x, pos\.y/);
assert.match(page, /function updateTreasureAmounts\(\)/);
assert.match(page, /id="lg-add"/);
assert.match(page, /textContent = 'Ändra'/);
assert.match(page, /M\.updateLegend/);
assert.match(page, /data-tool="move"/);
assert.match(page, /data-tool="dark"/);
assert.match(page, /id="nf-create"/);
assert.match(page, /id="am-add"/);
assert.match(page, /id="rs-fill"/);
assert.match(page, /id="zoom"/);
assert.match(page, /id="display-image"/);
assert.match(page, /id="display-text"/);
assert.match(page, /id="reveal-layers"/);
assert.match(page, /M\.coordinateLayers\(doc, mapId\)/);
assert.match(page, /lagrens koordinater/);
assert.match(page, /GLYPH_ART/);
assert.match(page, /adventure-sprites\/\$\{encodeURIComponent\(name\)\}\.png/);
assert.match(page, /function renderResourceImages\(\)/);
assert.match(page, /naturalWidth !== 32 \|\| image\.naturalHeight !== 32/);
assert.match(page, /root:'sd', path:`\$\{folder\}\/bitmaps\/\$\{name\}`/);
assert.match(page, /function addSectionFromForm\(\)/);
assert.match(page, /choice: \$\{choice\} \| \$\{choiceEn\} => \$\{target\}/);
assert.match(page, /Valets mål måste vara ett annat @node-id eller END/);
assert.match(page, /resource_bag\.png/);
assert.match(page, /id="group-list"/);
assert.match(page, /id="section-editors"/);
assert.match(page, /title="Automatisk anpassar kartan till tillgänglig bredd\."/);
assert.match(page, /@media \(max-width: 40rem\)/);
assert.match(page, /Karta \$\{id\} saknar kartrader/);
assert.match(page, /if \(mapMode\(\) && doc\)/);
assert.match(page, /\/api\/files\/mkdir/);
assert.doesNotMatch(page, /1–80/);
assert.match(fs.readFileSync('tools/embed_web.py', 'utf8'), /"\/adventure-model\.js"/);
assert.match(fs.readFileSync('tools/embed_web.py', 'utf8'), /relative_to\(ROOT\)\.as_posix\(\)/);
for (const name of ['flash', 'pogo', 'poison', 'swamp', 'sharp', 'skates', 'box', 'color_plate', 'suction', 'resource_bag']) {
  const png = fs.readFileSync(`docs/adventures/sprites/${name}.png`);
  assert.equal(png.toString('hex', 0, 8), '89504e470d0a1a0a');
  assert.equal(png.readUInt32BE(16), 32);
  assert.equal(png.readUInt32BE(20), 32);
}

const script = page.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
new vm.Script(script);
const elements = {
  'treasure-item': {value: 'potion'},
  'treasure-amount': {
    value: '',
    replaceChildren(...options) { this.options = options; this.value = options[0].value; }
  }
};
const controls = vm.createContext({
  $: id => elements[id],
  doc: {},
  document: {createElement: () => ({})}
});
vm.runInContext(script.slice(script.indexOf('function updateTreasureAmounts()'),
                            script.indexOf('function legendLabel(')), controls);
controls.updateTreasureAmounts();
assert.equal(elements['treasure-amount'].value, '1');
assert.equal(elements['treasure-amount'].options.length, 256);
elements['treasure-amount'].value = '50';
controls.updateTreasureAmounts();
assert.equal(elements['treasure-amount'].value, '50', 'repeated placement retains amount');
elements['treasure-item'].value = 'flash';
controls.updateTreasureAmounts();
assert.deepEqual(elements['treasure-amount'].options.map(o => [o.value, o.textContent]),
                 [['1', '1'], ['2', '2'], ['3', '3']]);
elements['treasure-item'].value = 'gem';
controls.updateTreasureAmounts();
assert.equal(elements['treasure-amount'].value, '1');
assert.equal(elements['treasure-amount'].disabled, true);
const server = fs.readFileSync('main/app_web_adventure_editor.cpp', 'utf8');
const filesServer = fs.readFileSync('main/app_web_files.cpp', 'utf8');
assert.match(server, /request->content_len > MAX_BODY/);
assert.match(server, /text\/plain/);
assert.match(server, /query\.adminAllowed\(\)/);
assert.match(server, /Adv::validateText/);
assert.match(server, /\/api\/adventure\/validate/);
assert.match(filesServer, /q\.getInt\("max_size", 1, 16384, maxSize/);
assert.match(filesServer, /st\.st_size > maxSize/);
console.log('Adventure editor page and production-validation route checks passed');

// Execute the complete production script with a small DOM/canvas adapter, including
// startup, real event handlers and storage shared between successive page loads.
const M = require('../main/web/adventure-model.js');
class Element {
  constructor(tag = 'div') {
    this.tagName = tag.toUpperCase(); this.value = ''; this.children = [];
    this.hidden = false; this.disabled = false; this.style = {}; this.dataset = {};
    this.listeners = {}; this.attributes = {}; this.firstChild = {textContent:''};
    this.clientWidth = 640; this.clientHeight = 400; this.scrollLeft = this.scrollTop = 0;
    const classes = new Set();
    this.classList = {
      add: name => classes.add(name), remove: name => classes.delete(name),
      toggle(name, on = !classes.has(name)) { if (on) classes.add(name); else classes.delete(name); },
      contains: name => classes.has(name)
    };
  }
  append(...nodes) { this.children.push(...nodes); }
  replaceChildren(...nodes) {
    this.children = nodes;
    if (this.tagName === 'SELECT') this.value = nodes[0]?.value || '';
  }
  get options() { return this.children; }
  get childElementCount() { return this.children.length; }
  setAttribute(name, value) { this.attributes[name] = value; }
  addEventListener(name, fn) { (this.listeners[name] ||= []).push(fn); }
  getContext() { return new Proxy({}, {get: (obj, key) => obj[key] || (() => {})}); }
  getBoundingClientRect() { return {left:0, top:0, right:this.width, bottom:this.height, width:this.width, height:this.height}; }
  setPointerCapture() {}
  scrollTo({left, top}) { this.scrollLeft = left; this.scrollTop = top; }
}
function runtime(storage = new Map(), failure = () => false) {
  const nodes = {}, events = {}, windowEvents = {}, requests = [], confirmations = [];
  for (const match of page.matchAll(/<(\w+)\b([^>]*\bid="([^"]+)"[^>]*)>/g)) {
    const node = nodes[match[3]] = new Element(match[1]);
    node.id = match[3];
    node.value = /\bvalue="([^"]*)"/.exec(match[2])?.[1] || '';
    node.hidden = /\bhidden\b/.test(match[2]);
    node.disabled = /\bdisabled\b/.test(match[2]);
  }
  for (const [id, value] of Object.entries({'treasure-item':'key', zoom:'0', 'section-tag':'node', 'lg-kind':'plate'}))
    nodes[id].value = value;
  const tools = ['paint', 'erase', 'pick', 'move', 'dark'].map(tool => {
    const node = new Element('button'); node.dataset.tool = tool; return node;
  });
  const context = vm.createContext({
    document:{
      getElementById: id => { assert.ok(nodes[id], `missing DOM node ${id}`); return nodes[id]; },
      createElement: tag => new Element(tag),
      querySelectorAll: selector => { assert.equal(selector, '#tools button'); return tools; },
      addEventListener: (name, fn) => (events[name] ||= []).push(fn)
    },
    window:{AdvModel:M, addEventListener:(name, fn) => (windowEvents[name] ||= []).push(fn)},
    sessionStorage:{
      getItem(key) { if (failure('get', key)) throw new Error('Storage blocked'); return storage.get(key) ?? null; },
      setItem(key, value) { if (failure('set', key, value)) throw new Error('Storage quota'); storage.set(key, value); }
    },
    TextEncoder, URLSearchParams, URL, Image:class {}, console,
    confirm: text => { confirmations.push(text); return false; },
    fetch: async url => { requests.push(url); throw new Error('Unexpected SD request'); }
  });
  vm.runInContext(script, context);
  function event(node, type, extra = {}) {
    const e = {target:node, ...extra};
    node[`on${type}`]?.(e);
    for (const fn of node.listeners[type] || []) fn(e);
    for (const fn of events[type] || []) fn(e);
  }
  return {nodes, context, requests, confirmations, storage, tools, event,
    click: node => event(typeof node === 'string' ? nodes[node] : node, 'click'),
    run: code => vm.runInContext(code, context),
    pagehide: () => windowEvents.pagehide.forEach(fn => fn())};
}
const draftKey = 'otr-adventure-editor-draft-v1';
const fixture = '@map\nS...\n....\n@map 2\nS...\n....\n@treasures map=2\nmap=1: 1 0 gem # keep\n2 1 potion 4\n';
let app = runtime();
app.nodes.source.value = fixture;
app.run("opened = true; openedPath = '/otr/adventure/test/a.adv';");
app.nodes.path.value = '/otr/adventure/test/a.adv';
app.click('mode-map');
app.click(app.nodes['map-tabs'].children[1]);
app.nodes.zoom.value = '24'; app.event(app.nodes.zoom, 'change');
app.click('display-text');
app.click(app.tools[2]);
app.nodes['treasure-item'].value = 'potion'; app.event(app.nodes['treasure-item'], 'change');
app.nodes['treasure-amount'].value = '50'; app.event(app.nodes['treasure-amount'], 'change');
app.click('treasure-place');
assert.equal(app.nodes['layer-panel'].hidden, false);
assert.equal(app.nodes['reveal-layers'].attributes['aria-pressed'], undefined, 'placement does not enable independent reveal');
const row = app.nodes['layer-list'].children[0];
assert.deepEqual(row.children.map(node => node.textContent).slice(1), ['Visa', 'Ta bort']);
app.click(row.children[2]);
assert.ok(!app.nodes.source.value.includes('2 1 potion 4'));
assert.ok(app.nodes.source.value.includes('map=1: 1 0 gem # keep'));
let refreshed = runtime(app.storage);
assert.equal(refreshed.nodes.source.value, app.nodes.source.value);
assert.equal(refreshed.run('mapId'), 2);
assert.equal(refreshed.run('tool'), 'pick');
assert.equal(refreshed.nodes.zoom.value, '24');
assert.equal(refreshed.run('displayMode'), 'text');
assert.equal(refreshed.nodes['treasure-item'].value, 'potion');
assert.equal(refreshed.nodes['treasure-amount'].value, '50');
assert.equal(refreshed.run('treasurePlacement'), true);
assert.match(refreshed.nodes['draft-notice'].textContent, /Återställde.*osparade/);
assert.equal(refreshed.requests.length, 0);
refreshed.click('undo');
assert.equal(refreshed.nodes.source.value, fixture, 'undo survives refresh and restores deletion losslessly');
refreshed.event(refreshed.nodes['map-canvas'], 'pointerdown', {pointerId:1, clientX:84, clientY:36});
assert.ok(refreshed.nodes.source.value.includes('3 1 potion 50'), 'actual canvas handler places selected treasure and amount');
refreshed.click('undo');
assert.equal(refreshed.nodes.source.value, fixture);
refreshed.click('treasure-place');
assert.equal(refreshed.nodes['layer-panel'].hidden, true, 'ending placement hides its list');
refreshed.click('reveal-layers');
refreshed.click('treasure-place'); refreshed.click('treasure-place');
assert.equal(refreshed.nodes['layer-panel'].hidden, false, 'explicit reveal remains independent');
refreshed.click('reveal-layers');
assert.equal(refreshed.nodes['layer-panel'].hidden, true);
refreshed.click('mode-text');
const unsaved = fixture + '# osparad text åäö\n';
refreshed.nodes.source.value = unsaved; refreshed.event(refreshed.nodes.source, 'input');
refreshed.pagehide();
app = runtime(refreshed.storage);
assert.equal(app.nodes.source.value, unsaved, 'refresh restores exact unsaved textarea text');
assert.equal(app.nodes['map-panel'].hidden, true);
assert.equal(app.run('mapId'), 2, 'text mode retains map choice');
assert.equal(app.nodes.path.value, '/otr/adventure/test/a.adv');
assert.equal(app.requests.length, 0);
assert.ok(!JSON.stringify(JSON.parse(app.storage.get(draftKey))).includes('pin'), 'draft excludes PIN');
app.run('openFile()');
assert.equal(app.confirmations.length, 1);
assert.equal(app.requests.length, 0, 'cancelled open does not fetch or overwrite draft');
assert.equal(app.nodes.source.value, unsaved);

let blocked = false;
app = runtime(app.storage, (operation, key) => blocked && operation === 'set' && key === draftKey);
blocked = true;
app.nodes.source.value += '# newest\n'; app.event(app.nodes.source, 'input');
assert.ok(app.nodes.source.value.endsWith('# newest\n'));
assert.equal(app.nodes['draft-notice'].hidden, false);
assert.match(app.nodes['draft-notice'].textContent, /Kunde inte lagra utkastet.*Kopiera/);
assert.equal(app.nodes['draft-notice'].className, 'bad');
app.pagehide();
assert.ok(app.nodes.source.value.endsWith('# newest\n'));
app.nodes.source.value = 'å'.repeat(32769); app.event(app.nodes.source, 'input');
assert.equal(app.nodes.save.disabled, true);
assert.match(app.nodes['draft-notice'].textContent, /större än 64 KiB/);
const inaccessible = runtime(new Map(), () => true);
assert.equal(inaccessible.nodes['draft-notice'].hidden, false);
assert.match(inaccessible.nodes['draft-notice'].textContent, /Kunde inte återställa/);
let writes = 0;
const quotaStore = new Map(refreshed.storage);
const quotaApp = runtime(quotaStore, (operation, key, value) => {
  if (operation !== 'set' || key !== draftKey) return false;
  writes++;
  return JSON.parse(value).undo.length > 0;
});
quotaApp.click('mode-map');
quotaApp.run("commit(() => M.paintCell(doc, mapId, 3, 1, '#'))");
assert.equal(JSON.parse(quotaStore.get(draftKey)).undo.length, 0, 'quota fallback sacrifices undo, never source');
assert.equal(JSON.parse(quotaStore.get(draftKey)).source, quotaApp.nodes.source.value);
assert.match(quotaApp.nodes['draft-notice'].textContent, /ångrahistoriken kortades/);
const writesBeforeDraw = writes;
quotaApp.run('drawMap(); renderAll(); drawMap();');
assert.equal(writes, writesBeforeDraw, 'rendering does not write storage');
const limited = runtime(new Map(refreshed.storage));
limited.click('mode-map');
limited.run("for (let i = 0; i < 65; i++) commit(() => M.paintCell(doc, mapId, 3, 1, i % 2 ? '.' : '#'));");
assert.equal(limited.run('undoStack.length'), 60);
assert.equal(JSON.parse(limited.storage.get(draftKey)).undo.length, 60);
const recoverLimited = runtime(limited.storage);
assert.equal(recoverLimited.run('undoStack.length'), 60);
assert.equal(recoverLimited.nodes.source.value, limited.nodes.source.value);
assert.match(page, /#layer-list li \{ flex-wrap:nowrap/);
assert.match(page, /#layer-list button \{[^}]*min-height:0[^}]*padding:\.12rem \.45rem/);
console.log('Full production editor runtime recovery, placement, deletion and storage failure checks passed');
