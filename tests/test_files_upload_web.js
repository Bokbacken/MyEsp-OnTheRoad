const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync('main/web/files.html', 'utf8');
assert.match(html, /<option value="skip">Hoppa över<\/option>/);
const script = html.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
const start = script.indexOf('function uploadOne');
const end = script.indexOf("$('pick-btn')", start);
const uploadCode = script.slice(start, end);

async function uploadWith(policy, responses, fileCount = 2, uploadRoot = 'sd') {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {value: '', hidden: false, textContent: ''});
    return elements.get(id);
  };
  element('conflict-policy').value = policy;
  const requests = [];
  const toasts = [];
  let response = 0;
  let active = 0, maxActive = 0;
  class XMLHttpRequest {
    constructor() { this.upload = {}; }
    open(method, url) { this.url = url; }
    setRequestHeader() {}
    send(file) {
      requests.push({url: this.url, file});
      active++;
      maxActive = Math.max(maxActive, active);
      const body = responses[response++];
      setTimeout(() => {
        this.upload.onprogress({lengthComputable: true, loaded: file.size, total: file.size});
        this.status = body.status || 200;
        this.responseText = JSON.stringify(body);
        active--;
        this.onload();
      }, 0);
    }
  }
  const sandbox = {
    $, document: {}, XMLHttpRequest, URLSearchParams, root: uploadRoot, path: '/songs',
    join: (dir, name) => dir + '/' + name, qs: obj => new URLSearchParams(obj).toString(),
    size: bytes => bytes + ' B', guarded: fn => fn().catch(err => toasts.push(err.message)),
    toast: msg => toasts.push(msg), load: async () => {},
    setTimeout, clearTimeout,
  };
  function $(id) { return element(id); }
  vm.createContext(sandbox);
  vm.runInContext(uploadCode, sandbox);
  const files = Array.from({length: fileCount}, (_, i) => ({
    name: i ? `new${i}.mid` : 'same.mid', size: 10 + i, type: 'audio/midi'
  }));
  await vm.runInContext('upload(files)', Object.assign(sandbox, {files}));
  return {requests, toasts, maxActive, progress: element('prog').value};
}

(async () => {
  const result = await uploadWith('skip', Array.from({length: 6}, (_, i) =>
    i === 0 ? {ok: true, skipped: true} : {ok: true}), 6);
  assert.equal(result.requests.length, 6, 'batch continues after a skipped conflict');
  assert.equal(result.maxActive, 4, 'SD upload concurrency is capped at four');
  assert.equal(result.progress, 100, 'aggregate progress completes');
  assert.match(result.requests[0].url, /skip=1/);
  assert.doesNotMatch(result.requests[0].url, /overwrite=1/);
  assert.deepEqual(result.toasts, ['5 filer uppladdade, 1 hoppades över']);

  const stopped = await uploadWith('stop', [
    {ok: false, error: 'exists'}, {ok: true}, {ok: true}, {ok: true},
    {ok: true}, {ok: true}
  ], 6);
  assert.equal(stopped.requests.length, 4, 'failure stops scheduling files beyond the in-flight batch');
  assert.doesNotMatch(stopped.requests[0].url, /skip=1|overwrite=1/);
  assert.deepEqual(stopped.toasts, ['same.mid: exists (3 andra filer hann bli klara)']);

  const overwrite = await uploadWith('overwrite', [{ok: true}, {ok: true}]);
  assert.equal(overwrite.requests.length, 2);
  assert.match(overwrite.requests[0].url, /overwrite=1/);

  const internal = await uploadWith('skip', [{ok: true}, {ok: true}, {ok: true}], 3, 'ffat');
  assert.equal(internal.maxActive, 1, 'internal flash uploads remain serialized');
  assert.equal(internal.requests.length, 3);

  console.log('File upload policies, four-way SD sessions, and serialized flash uploads passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
