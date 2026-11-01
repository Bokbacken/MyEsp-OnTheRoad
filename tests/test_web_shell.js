const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

class Element {
  constructor(tag) { this.tag = tag; this.children = []; this.className = ''; this.attributes = {}; this.text = ''; }
  set textContent(value) { this.text = value; this.children = []; }
  get textContent() { return this.text + this.children.map(c => c.textContent).join(''); }
  setAttribute(name, value) { this.attributes[name] = value; }
  appendChild(child) {
    if (child.parent) child.parent.children.splice(child.parent.children.indexOf(child), 1);
    this.children.push(child); child.parent = this; return child;
  }
  append(...children) { children.forEach(c => this.appendChild(c)); }
  prepend(child) { this.appendChild(child); this.children.unshift(this.children.pop()); }
  before(child) {
    const parent = this.parent, at = parent.children.indexOf(this);
    parent.children.splice(at, 0, child); child.parent = parent;
  }
  replaceChildren(...children) {
    this.children.forEach(c => { c.parent = null; });
    this.children = []; this.text = ''; this.append(...children);
  }
  matches(selector) {
    if (selector === 'a[href="/"]') return this.tag === 'a' && this.href === '/';
    const [tag, cls] = selector.split('.');
    return (!tag || this.tag === tag) && (!cls || this.className.split(' ').includes(cls));
  }
  querySelectorAll(selector) {
    const selectors = selector.split(',').map(s => s.trim()), out = [];
    const visit = el => {
      for (const child of el.children) {
        if (selectors.some(s => child.matches(s))) out.push(child);
        visit(child);
      }
    };
    visit(this); return out;
  }
  querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
}

async function check(path, existingHeader) {
  const body = new Element('body');
  if (existingHeader) {
    const header = new Element('header'); header.className = 'otr-brand';
    const head = new Element('div'); head.className = 'otr-head';
    const brand = new Element('div'); brand.className = 'otr-project'; brand.textContent = 'ONTHEROAD';
    const small = new Element('small'); small.textContent = 'Inställningar';
    brand.appendChild(small); head.appendChild(brand); header.appendChild(head); body.appendChild(header);
    const nav = new Element('nav'); nav.className = 'otr-nav'; body.appendChild(nav);
  }
  const footer = new Element('footer'); footer.textContent = 'MyEsp-OnTheRoad'; body.appendChild(footer);
  const document = {
    body, hidden: false, title: 'OnTheRoad – Inställningar',
    createElement: tag => new Element(tag),
    querySelector: s => body.querySelector(s), querySelectorAll: s => body.querySelectorAll(s),
    addEventListener() {}
  };
  let wifi = {hostname: 'kitchen-console', connected: true, ssid: 'test', rssi: -45};
  let refresh;
  const sandbox = {
    document, location: {pathname: path}, window: {addEventListener() {}},
    fetch: async () => ({ok: true, json: async () => ({wifi})}),
    setInterval: fn => { refresh = fn; }, console
  };
  vm.createContext(sandbox);
  vm.runInContext(fs.readFileSync('main/web/device-name.js', 'utf8'), sandbox);
  await new Promise(resolve => setTimeout(resolve, 0));
  const brand = document.querySelector('.otr-project');
  assert(brand.textContent.startsWith('kitchen-console'));
  assert.equal(document.title, 'kitchen-console – Inställningar');
  assert.equal(footer.textContent, 'kitchen-console');
  const nav = document.querySelector('nav.otr-nav');
  assert.equal(nav.children.length, 12);
  assert.equal(document.querySelectorAll('.otr-chrome').length, 1);
  assert.equal(nav.children.filter(c => c.attributes['aria-current'] === 'page').length, 1);
  wifi = {...wifi, hostname: 'road-unit'};
  await refresh();
  assert(brand.textContent.startsWith('road-unit'));
  assert.equal(footer.textContent, 'road-unit', 'repeat rename updates footer, not just the first name');
  sandbox.window.OtrDevice.apply({hostname: 'new-name'});
  assert(brand.textContent.startsWith('new-name'), 'settings save applies name immediately');
  assert.equal(document.title, 'new-name – Inställningar');
  sandbox.window.OtrDevice.setTitle('Byn');
  assert.equal(document.title, 'new-name – Byn');
  footer.textContent = 'road-unit v1 · esp32s3';
  await refresh();
  assert.equal(document.title, 'road-unit – Byn', 'status refresh preserves dynamic page title');
  assert.equal(footer.textContent, 'road-unit v1 · esp32s3', 'status refresh preserves live footer metadata');
  return nav.children.map(c => c.href);
}

(async () => {
  const expected = await check('/settings', true);
  for (const path of ['/', '/game', '/play', '/adventure', '/adventure-editor', '/karaoke',
    '/karaoke/songs', '/players', '/diag', '/files', '/update', '/songs_picker.html']) {
    assert.deepEqual(await check(path, !path.includes('karaoke')), expected);
  }
  console.log('Shared web shell: identical navigation, active route and repeated live name changes passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
