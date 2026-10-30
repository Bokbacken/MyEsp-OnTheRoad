const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync('main/web/files.html', 'utf8');
assert.match(html, /<option value="skip">Hoppa över<\/option>/);
const script = html.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
const start = script.indexOf('function uploadOne');
const end = script.indexOf("$('pick-btn')", start);
const uploadCode = script.slice(start, end);

async function uploadWith(policy, responses) {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {value: '', hidden: false, textContent: ''});
    return elements.get(id);
  };
  element('conflict-policy').value = policy;
  const requests = [];
  const toasts = [];
  let response = 0;
  class XMLHttpRequest {
    constructor() { this.upload = {}; }
    open(method, url) { this.url = url; }
    setRequestHeader() {}
    send(file) {
      requests.push({url: this.url, file});
      const body = responses[response++];
      this.status = 200;
      this.responseText = JSON.stringify(body);
      this.onload();
    }
  }
  const sandbox = {
    $, document: {}, XMLHttpRequest, URLSearchParams, root: 'sd', path: '/songs',
    join: (dir, name) => dir + '/' + name, qs: obj => new URLSearchParams(obj).toString(),
    size: bytes => bytes + ' B', guarded: fn => fn().catch(err => toasts.push(err.message)),
    toast: msg => toasts.push(msg), load: async () => {},
    setTimeout() {}, clearTimeout() {},
  };
  function $(id) { return element(id); }
  vm.createContext(sandbox);
  vm.runInContext(uploadCode, sandbox);
  await vm.runInContext("upload([{name:'same.mid',size:10,type:'audio/midi'},{name:'new.mid',size:20,type:'audio/midi'}])", sandbox);
  return {requests, toasts};
}

(async () => {
  const result = await uploadWith('skip', [{ok: true, skipped: true}, {ok: true}]);
  assert.equal(result.requests.length, 2, 'batch continues after a skipped conflict');
  assert.match(result.requests[0].url, /skip=1/);
  assert.doesNotMatch(result.requests[0].url, /overwrite=1/);
  assert.deepEqual(result.toasts, ['1 filer uppladdade, 1 hoppades över']);

  const stopped = await uploadWith('stop', [{ok: false, error: 'exists'}]);
  assert.equal(stopped.requests.length, 1, 'default policy preserves stop-on-conflict');
  assert.doesNotMatch(stopped.requests[0].url, /skip=1|overwrite=1/);
  assert.deepEqual(stopped.toasts, ['same.mid: exists']);

  const overwrite = await uploadWith('overwrite', [{ok: true}, {ok: true}]);
  assert.equal(overwrite.requests.length, 2);
  assert.match(overwrite.requests[0].url, /overwrite=1/);

  console.log('File upload conflict policies and skip-batch continuation passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
