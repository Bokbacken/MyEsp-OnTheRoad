const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('main/web/files.html', 'utf8');
const script = html.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
new vm.Script(script);
const start = script.indexOf("$('nvs-read').onclick");
const end = script.indexOf('// ---- Upload', start);
assert.ok(start > 0 && end > start);
const code = script.slice(start, end);

async function run(id, response, confirmed = true) {
  const elements = new Map();
  const requests = [], messages = [], downloads = [];
  let revoked = false;
  const sandbox = {
    $: key => {
      if (!elements.has(key)) elements.set(key, {textContent: ''});
      return elements.get(key);
    },
    guarded: action => action().catch(error => messages.push(error.message)),
    confirm: () => confirmed,
    fetch: async (url, options) => { requests.push({url, options}); return response; },
    pinHdr: () => ({'X-Admin-Pin': 'synthetic-pin'}),
    answer: async r => {
      const j = await r.json();
      if (!r.ok || j.ok === false) throw new Error(j.error);
      return j;
    },
    size: bytes => `${bytes} B`,
    toast: text => messages.push(text),
    URL: {createObjectURL: () => 'blob:test', revokeObjectURL: () => { revoked = true; }},
    setTimeout: fn => fn(),
    document: {
      createElement: () => ({click() { downloads.push(this.download); }, remove() {}}),
      body: {appendChild() {}}
    }
  };
  vm.runInNewContext(code, sandbox);
  await sandbox.$(id).onclick();
  return {requests, messages, downloads, elements, revoked};
}

(async () => {
  const good = {ok: true, json: async () => ({ok: true, bytes: 20480, namespaces: 2, used: 30, free: 96, total: 126}),
    blob: async () => new Blob(['synthetic NVS image'])};
  let result = await run('nvs-read', good);
  assert.match(result.elements.get('nvs-info').textContent, /20480 B.*2 namnrymder.*30 använda, 96 lediga av 126/);
  assert.equal(result.requests[0].options.cache, 'no-store');
  assert.equal(result.requests[0].options.headers['X-Admin-Pin'], 'synthetic-pin');
  result = await run('nvs-backup', good);
  assert.deepEqual(result.downloads, ['otr-nvs.bin']);
  assert.equal(result.revoked, true);
  assert.equal(result.requests[0].url, '/api/nvs/backup');
  result = await run('nvs-backup', good, false);
  assert.equal(result.requests.length, 0);
  const denied = {ok: false, json: async () => ({ok: false, error: 'PIN denied'})};
  result = await run('nvs-backup', denied);
  assert.deepEqual(result.downloads, []);
  assert.deepEqual(result.messages, ['PIN denied']);
  result = await run('nvs-read', denied);
  assert.equal(result.elements.get('nvs-info').textContent, 'PIN denied');
  console.log('NVS UI: information, authenticated download, cancellation and explicit failures passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
