const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('main/web/songs_picker.html', 'utf8');
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
  addEventListener() {}
  setAttribute(name, value) { this[name] = value; }
  querySelectorAll() { return this.children; }
  showModal() { this.open = true; }
  close() { this.open = false; if (this.onclose) this.onclose(); }
}
const elements = new Map(), timers = [], requests = [];
const selectIds = ['playlist', 'song-font', 'view-mode'];
const get = id => {
  if (!elements.has(id)) elements.set(id, new Element(selectIds.includes(id) ? 'select' : 'div'));
  return elements.get(id);
};
get('draft-dialog').children = ['save', 'discard', ''].map(choice => {
  const b = new Element('button'); b.dataset.choice = choice; return b;
});
let lists = [], nextId = 1, promptValue = 'Road <trip>', fail = false, dirty = false, current = 'Old.kar';
let font = '';
const song = { path: 'Pop/Artist_Title.kar', title: 'Title <script>', artist: 'Artist', dir: 'Pop', size: 9 };
const rootSong = { path: 'Root_Title.kar', title: 'Root title', artist: 'Root artist', dir: '', size: 10 };
function response(data, ok = true) { return { ok, status: ok ? 200 : 409, json: async () => data }; }
async function fetch(url, options = {}) {
  const u = new URL(url, 'http://localhost'), body = options.body ? JSON.parse(options.body) : null;
  requests.push({ path: u.pathname, query: u.searchParams, body });
  if (fail) return response({ ok: false, error: 'SD write failed' }, false);
  if (u.pathname === '/api/karaoke/songs') {
    const all = u.searchParams.get('all') === '1';
    const dir = u.searchParams.get('dir') || '';
    const dirs = all || u.searchParams.has('q') || dir ? [] : [{ name: 'Pop', path: 'Pop', count: 1 }];
    const entries = u.searchParams.has('q') || all ? [song, rootSong] : dir ? [song] : [rootSong];
    return response({ ok: true, dirs, songs: entries, count: entries.length, current, dirty, playing: true });
  }
  if (u.pathname === '/api/karaoke/playlists' && !body) {
    const id = Number(u.searchParams.get('id')), offset = Number(u.searchParams.get('offset'));
    return response({ ok: true, playlists: lists.filter(p => !id || p.id === id).map(p => ({
      ...p, count: p.songs.length, songs: id ? p.songs.slice(offset, offset + 100) : []
    })) });
  }
  if (u.pathname === '/api/karaoke/playlists') {
    const p = lists.find(p => p.id === body.id);
    if (body.action === 'create') {
      lists.push({ id: nextId++, name: body.name, songs: [] });
      return response({ ok: true, id: lists.at(-1).id });
    }
    if (body.action === 'rename') p.name = body.name;
    if (body.action === 'delete') lists = lists.filter(p => p !== lists.find(p => p.id === body.id));
    if (body.action === 'add') p.songs.push({ ...song, crc: 123 });
    if (body.action === 'remove') p.songs = p.songs.filter(s => s.crc !== body.crc);
    if (body.action === 'up' || body.action === 'down') {
      const i = p.songs.findIndex(s => s.crc === body.crc), to = i + (body.action === 'up' ? -1 : 1);
      [p.songs[i], p.songs[to]] = [p.songs[to], p.songs[i]];
    }
    return response({ ok: true, id: body.id });
  }
  if (u.pathname === '/api/karaoke/open') {
    if (dirty && !body.decision)
      return response({ ok: false, code: 'unsaved', current: '/sdcard/otr/karaoke/' + current }, false);
    dirty = false; current = body.path; return response({ ok: true });
  }
  if (u.pathname === '/api/karaoke/song' && !body)
    return response({ ok: true, sf2: font, fonts: ['Bank with spaces.sf2'] });
  if (u.pathname === '/api/karaoke/song') {
    if (body.action === 'font') font = body.sf2;
    else dirty = false;
    return response({ ok: true });
  }
  throw new Error('Unexpected request: ' + url);
}
const sandbox = {
  document: { getElementById: get, createElement: tag => new Element(tag), documentElement: {},
    querySelectorAll: () => [] },
  navigator: { language: 'sv' }, localStorage: { getItem: () => null, setItem() {} },
  window: { addEventListener() {} }, URLSearchParams, fetch, console,
  setTimeout: fn => { timers.push(fn); return timers.length; }, clearTimeout() {},
  confirm: () => true, prompt: () => promptValue
};
const context = vm.createContext(sandbox);
vm.runInContext(script, context);
const run = code => vm.runInContext(code, context);
async function flush() { for (let i = 0; i < 15; i++) await Promise.resolve(); }
async function main() {
  await flush();
  assert.equal(get('draft').hidden, true);
  assert.equal(get('view-mode').value, 'folders');
  assert.equal(requests.find(r => r.path === '/api/karaoke/songs').query.get('dir'), '');
  assert.equal(get('list').children.length, 2, 'folder mode lists subfolders and direct folder contents');
  await run("go('Pop')"); await flush();
  assert.equal(get('list').children.length, 2, 'folder browsing has a parent entry and direct songs only');
  assert.equal(get('list').children[0].children[1].textContent, '..');
  assert.equal(get('list').children[1].children[1].children[0].textContent, 'Title <script>');
  await run("go('')"); await flush();
  await run("setViewMode('all')"); await flush();
  assert.equal(requests.filter(r => r.path === '/api/karaoke/songs').at(-1).query.get('all'), '1');
  assert.equal(get('list').children.length, 2, 'all-songs mode lists songs without folder entries');
  await run("setViewMode('folders')"); await flush();
  await run("editList('create')");
  assert.equal(get('playlist').children[0].textContent, 'Road <trip> (0)');
  const songRow = get('list').children.find(row => row.children.some(child => child.textContent === '+ Lista'));
  assert.ok(songRow);
  assert.equal(songRow.children.at(-2).textContent, '+ Lista');
  await songRow.children.at(-2).onclick({ stopPropagation() {} });
  assert.equal(Number(get('playlist').value), 1);
  assert.equal(lists[0].songs.length, 1);
  await run("editSong(songs[0])");
  assert.equal(get('song-dialog').open, true);
  get('song-font').value = 'Bank with spaces.sf2';
  const before = requests.length;
  get('song-dialog').close();
  assert.equal(requests.length, before, 'Cancel must not write song settings');
  await run("editSong(songs[0])");
  get('song-font').value = 'Bank with spaces.sf2';
  await get('save-font').onclick();
  assert.equal(font, 'Bank with spaces.sf2');
  assert.equal(get('song-dialog').open, false);
  // Save/discard/cancel is a three-way decision, bound to the previously open song.
  for (const decision of ['', 'save', 'discard']) {
    run('busy = false');
    dirty = true; current = 'Old.kar';
    const pending = run("openSong(songs[0], $('list').children[0])");
    await flush();
    assert.equal(get('draft-dialog').open, true);
    get('draft-dialog').children.find(b => b.dataset.choice === decision).onclick();
    await pending;
    if (!decision) assert.equal(current, 'Old.kar');
    else {
      assert.equal(current, rootSong.path);
      const last = requests.filter(r => r.path === '/api/karaoke/open').at(-1).body;
      assert.equal(last.decision, decision);
      assert.equal(last.current, '/sdcard/otr/karaoke/Old.kar');
    }
  }
  dirty = true;
  await run('load(false)');
  assert.equal(get('draft').hidden, false);
  run('busy = false');
  await run("resolveDraft('save')");
  assert.equal(get('draft').hidden, true);
  run("setLang('en', true)");
  assert.equal(get('playlist')['aria-label'], 'Playlists');
  console.log('Karaoke picker: separate folder/all modes, top playlist selection, SF2 and save/discard/cancel passed');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
