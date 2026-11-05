const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('main/web/playlists.html', 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
class Element {
  constructor(tag = 'div') {
    this.tag = tag; this.children = []; this.style = {}; this.dataset = {}; this.disabled = false;
    this.classList = { add() {}, remove() {}, toggle() {} }; this._text = ''; this._value = null;
  }
  set textContent(value) { this._text = value; this.children = []; this._value = null; }
  get textContent() { return this._text + this.children.map(c => c.textContent).join(''); }
  set value(value) { this._value = String(value); }
  get value() { return this._value ?? (this.tag === 'select' ? this.children[0]?.value || '' : ''); }
  appendChild(child) { this.children.push(child); return child; }
  addEventListener(name, action) { this['on' + name] = action; }
  setAttribute(name, value) { this[name] = value; }
  querySelectorAll() { return this.children; }
  focus() {}
}
const elements = new Map(), requests = [], selectIds = ['playlist'];
const get = id => {
  if (!elements.has(id)) elements.set(id, new Element(selectIds.includes(id) ? 'select' : 'div'));
  return elements.get(id);
};
let lists = [], nextId = 1, fail = false;
const storage = new Map();
function response(data, ok = true) { return { ok, status: ok ? 200 : 503, json: async () => data }; }
async function fetch(url, options = {}) {
  const u = new URL(url, 'http://localhost'), body = options.body ? JSON.parse(options.body) : null;
  requests.push({ path: u.pathname, query: u.searchParams, body });
  if (fail) return response({ ok: false, error: 'SD write failed' }, false);
  if (u.pathname !== '/api/karaoke/playlists') throw new Error('Unexpected request: ' + url);
  if (!body) {
    const id = Number(u.searchParams.get('id')), offset = Number(u.searchParams.get('offset'));
    return response({ ok: true, playlists: lists.filter(p => !id || p.id === id).map(p => ({
      ...p, count: p.songs.length, songs: id ? p.songs.slice(offset, offset + 100) : []
    })) });
  }
  if (body.action === 'create') {
    lists.push({ id: nextId++, name: body.name, songs: [] });
    return response({ ok: true, id: lists.at(-1).id });
  }
  const p = lists.find(p => p.id === body.id);
  if (body.action === 'rename') p.name = body.name;
  if (body.action === 'delete') lists = lists.filter(list => list !== p);
  if (body.action === 'remove') p.songs = p.songs.filter(s => s.crc !== body.crc);
  if (body.action === 'up' || body.action === 'down') {
    const i = p.songs.findIndex(s => s.crc === body.crc), to = i + (body.action === 'up' ? -1 : 1);
    [p.songs[i], p.songs[to]] = [p.songs[to], p.songs[i]];
  }
  return response({ ok: true, id: body.id });
}
const sandbox = {
  document: { getElementById: get, createElement: tag => new Element(tag), documentElement: {},
    querySelectorAll: () => [] },
  navigator: { language: 'sv' }, localStorage: { getItem: key => storage.get(key) || null, setItem: (key, value) => storage.set(key, value) },
  window: { addEventListener() {} }, URLSearchParams, fetch, console, confirm: () => true,
};
vm.runInContext(script, vm.createContext(sandbox));
const run = code => vm.runInContext(code, sandbox);
async function flush() { for (let i = 0; i < 20; i++) await Promise.resolve(); }
async function main() {
  await flush();
  get('list-name').value = 'First list';
  await get('list-form').onsubmit({ preventDefault() {} });
  assert.equal(lists[0].name, 'First list');
  assert.equal(get('playlist').value, '1');
  assert.equal(storage.get('otr_playlist'), '1');
  lists[0].songs = Array.from({ length: 205 }, (_, i) => ({
    path: `Folder/Song ${i}.kar`, title: `Song ${i}`, artist: 'Artist', crc: i, size: i + 1
  }));
  await run('loadPlaylist()');
  assert.equal(get('playlist-songs').children.length, 205, 'playlist contents must page beyond 100 songs');
  assert.equal(requests.filter(r => r.path === '/api/karaoke/playlists' && r.query.has('offset')).length >= 3, true);
  await get('playlist-songs').children[1].children[1].onclick({ stopPropagation() {} });
  assert.equal(lists[0].songs[0].crc, 1, 'move-up changes persistent playlist order');
  await get('playlist-songs').children[1].children.at(-1).onclick({ stopPropagation() {} });
  assert.equal(lists[0].songs.length, 204, 'remove deletes the selected item');
  run('beginRename()');
  get('list-name').value = 'Renamed';
  await get('list-form').onsubmit({ preventDefault() {} });
  assert.equal(lists[0].name, 'Renamed');
  fail = true;
  await run('deleteList()');
  assert.equal(lists.length, 1);
  assert.equal(get('notice').textContent, 'SD write failed');
  fail = false;
  await run('deleteList()');
  assert.equal(lists.length, 0);
  assert.equal(get('playlist-songs').children.length, 0);
  console.log('Karaoke playlists page: CRUD, 205-song paging, order, removal and write errors passed');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
