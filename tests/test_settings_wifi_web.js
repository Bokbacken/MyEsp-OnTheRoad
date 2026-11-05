const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync('main/web/settings.html', 'utf8');
const script = html.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
const start = script.indexOf('let saved = [];');
const end = script.indexOf("$('save-only-btn')", start);
assert.ok(start >= 0 && end > start);
const code = script.slice(start, end);
new vm.Script(code);

class Element {
  constructor() {
    this.children = [];
    this.handlers = {};
    this.className = '';
    this._text = '';
    this.hidden = false;
  }
  set textContent(value) { this._text = value; this.children = []; }
  get textContent() { return this._text + this.children.map(child => child.textContent).join(''); }
  set innerHTML(value) { this.html = value; this.children = []; }
  get innerHTML() { return this.html || ''; }
  get lastChild() { return this.children[this.children.length - 1]; }
  append(...children) { this.children.push(...children); }
  appendChild(child) { this.children.push(child); return child; }
  addEventListener(name, callback) { this.handlers[name] = callback; }
}

(async () => {
  const elements = new Map();
  const requests = [];
  const outcomes = [
    new Error('Åtgärden avbröts'),
    new Error('Felaktig admin-PIN'),
    {nets: [{ssid: 'Road network', secure: true, connected: false}], max: 10}
  ];
  const sandbox = {
    $: id => {
      if (!elements.has(id)) elements.set(id, new Element());
      return elements.get(id);
    },
    document: {createElement: () => new Element()},
    fetchAdmin: async (url, options) => {
      requests.push({url, options});
      const outcome = outcomes.shift();
      if (outcome instanceof Error) throw outcome;
      return {json: async () => outcome};
    },
    toast() {},
    confirm: () => true
  };
  vm.createContext(sandbox);
  vm.runInContext(code, sandbox);

  const loadButton = sandbox.$('load-nets-btn');
  sandbox.setNetworksLocked(true);
  assert.equal(loadButton.hidden, false);
  assert.match(sandbox.$('saved').innerHTML, /Admin-PIN krävs/);

  await loadButton.handlers.click();
  assert.equal(loadButton.hidden, false, 'cancelling PIN entry leaves the load action available');
  await loadButton.handlers.click();
  assert.equal(loadButton.hidden, false, 'a rejected PIN leaves the load action available');
  await loadButton.handlers.click();
  assert.equal(requests.length, 3);
  assert.equal(requests[2].url, '/api/nets');
  assert.equal(sandbox.$('saved').children.length, 1);
  assert.match(sandbox.$('saved').textContent, /Road network/);

  const scan = script.slice(script.indexOf('async function scan()'));
  assert.match(scan, /await loadNets\(\)/, 'an authenticated scan refreshes the saved network data');
  const fillSizes = script.slice(script.indexOf("  for (const o of $('text_size').options)"),
                                script.indexOf('  if (c.rotation !== undefined)'));
  const dataStart = script.indexOf('  const data = {', script.indexOf("$('cfg-form').addEventListener"));
  const dataEnd = script.indexOf('\n  };', dataStart) + '\n  };'.length;
  for (let zoom = 0; zoom < 3; ++zoom) {
    for (let text = 0; text < 4; ++text) {
      sandbox.$('text_size').options = [0, 1, 2, 3].map(value => ({value}));
      sandbox.c = {text_size: text, screen_zoom: zoom, text_size_max: 3};
      vm.runInContext(fillSizes, sandbox);
      assert.equal(sandbox.$('text_size').value, text);
      assert.equal(sandbox.$('screen_zoom').value, zoom);
      sandbox.$('tz').value = 'UTC0';
      sandbox.host = 'ontheroad';
      const payload = vm.runInContext(`(() => { ${script.slice(dataStart, dataEnd)} return data; })()`, sandbox);
      assert.equal(payload.text_size, text);
      assert.equal(payload.screen_zoom, zoom);
    }
  }
  sandbox.c = {};
  vm.runInContext(fillSizes, sandbox);
  assert.equal(sandbox.$('text_size').value, 1);
  assert.equal(sandbox.$('screen_zoom').value, 1);
  console.log('Saved Wi-Fi networks: PIN-gated loading can be retried after cancel or rejection');
  console.log('Text size and screen zoom: independent load/save values and Normal migration');
})().catch(error => { console.error(error); process.exitCode = 1; });
