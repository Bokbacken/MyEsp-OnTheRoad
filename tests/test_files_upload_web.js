const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync('main/web/files.html', 'utf8');
assert.match(html, /<option value="skip" selected>Hoppa över<\/option>/);
assert.match(html, /id="dir-file" multiple webkitdirectory directory hidden/);
assert.match(html, /class="row storage-row"[\s\S]*id="usage"[\s\S]*class="storage-admin"[\s\S]*id="pin"[\s\S]*id="format-btn"/);
assert.match(html, /class="storage-admin"[^]*id="format-btn"[^]*id="repair-btn"[^]*<\/span>/);
assert.equal((html.match(/id="repair-btn"/g) || []).length, 1);
assert.match(html, /id="nvs-read"/);
assert.match(html, /id="nvs-backup"/);
const script = html.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
assert.match(script, /\$\(\'format-btn\'\)\.disabled = root !== \'sd\'/);
assert.match(script, /\/api\/sd\/format\/status/);
const formatStart = script.indexOf("$('format-btn').onclick");
const formatEnd = script.indexOf("$('repair-btn').onclick", formatStart);
const formatCode = script.slice(formatStart, formatEnd);
const start = script.indexOf('function uploadOne');
const end = script.indexOf("$('pick-btn')", start);
const uploadCode = script.slice(start, end);

async function formatStatusFlow(statuses, expectedToast) {
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {value: 0, hidden: true, textContent: '', removeAttribute() {}});
    return elements.get(id);
  };
  const requests = [];
  const toasts = [];
  const expectedPolls = statuses.length;
  const sandbox = {
    $: element, root: 'sd', path: '/', page: 1,
    confirm: () => true, prompt: () => 'FORMATERA',
    guarded: fn => fn().catch(error => toasts.push(error.message)),
    postForm: async (...args) => { requests.push(args); return {ok: true}; },
    pinHdr: () => ({'X-Admin-Pin': '1234'}),
    fetch: async (_url, options) => {
      requests.push(options);
      return {ok: true, json: async () => statuses.shift()};
    },
    answer: async response => response.json(),
    setTimeout: callback => callback(),
    toast: message => toasts.push(message),
    load: async () => {},
  };
  vm.createContext(sandbox);
  vm.runInContext(formatCode, sandbox);
  await vm.runInContext("$('format-btn').onclick()", sandbox);
  assert.equal(requests[0][0], '/api/sd/format');
  assert.equal(requests[0][1].confirm, 'FORMAT-SD');
  assert.equal(requests.filter(request => request.headers).length, expectedPolls, 'polls status until formatting completes');
  assert.equal(element('prog').hidden, true, 'hides progress after completion');
  assert.deepEqual(toasts, [expectedToast]);
}

async function uploadWith(policy, responses, fileCount = 2, uploadRoot = 'sd', relativePaths = [], uploadSlots = 4) {
  const elements = new Map();
  const labels = [];
  const element = id => {
    if (!elements.has(id)) {
      const entry = {value: '', hidden: false, textContent: ''};
      if (id === 'prog-text') Object.defineProperty(entry, 'textContent', {
        set(value) { labels.push(value); },
        get() { return labels.at(-1) || ''; }
      });
      elements.set(id, entry);
    }
    return elements.get(id);
  };
  element('conflict-policy').value = policy;
  const requests = [];
  const mkdirs = [];
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
    $, document: {}, XMLHttpRequest, URLSearchParams, root: uploadRoot, path: '/songs', uploadSlots,
    join: (dir, name) => dir + '/' + name, qs: obj => new URLSearchParams(obj).toString(),
    size: bytes => bytes + ' B', guarded: fn => fn().catch(err => toasts.push(err.message)),
    toast: msg => toasts.push(msg), load: async () => {},
    sendJson: async (method, url, data) => {
      mkdirs.push({url, body: JSON.stringify(data)});
      return {ok: true};
    },
    fetch: async (url, options) => {
      return {ok: true, status: 200, json: async () => ({ok: true})};
    },
    setTimeout, clearTimeout,
  };
  function $(id) { return element(id); }
  vm.createContext(sandbox);
  vm.runInContext(uploadCode, sandbox);
  const files = Array.from({length: fileCount}, (_, i) => ({
    name: i ? `new${i}.mid` : 'same.mid', size: 10 + i, type: 'audio/midi',
    webkitRelativePath: relativePaths[i] || ''
  }));
  await vm.runInContext('upload(files)', Object.assign(sandbox, {files}));
  return {requests, mkdirs, toasts, maxActive, labels, progress: element('prog').value};
}

(async () => {
  await formatStatusFlow([{ok: true, formatting: true}, {ok: true, formatted: true}],
    'SD-kortet formaterades som FAT');
  await formatStatusFlow([{ok: true, formatted: false, error: 'Formateringen misslyckades'}],
    'Formateringen misslyckades');
  const result = await uploadWith('skip', Array.from({length: 6}, (_, i) =>
    i === 0 ? {ok: true, skipped: true} : {ok: true}), 6);
  assert.equal(result.requests.length, 6, 'batch continues after a skipped conflict');
  assert.equal(result.maxActive, 4, 'SD file uploads allow up to four simultaneous sessions');
  assert.equal(result.progress, 100, 'aggregate progress completes');
  assert(result.labels.includes('0/6 klara · 4 pågående · 0 B av 75 B skickat'));
  assert(result.labels.includes('6/6 klara · 0 pågående · 75 B av 75 B skickat'));
  assert(result.labels.filter(Boolean).every(label => !label.includes('.mid')),
    'batch status does not flicker between individual filenames');
  assert.match(result.requests[0].url, /skip=1/);
  assert.doesNotMatch(result.requests[0].url, /overwrite=1/);
  assert.deepEqual(result.toasts, ['5 filer uppladdade, 1 hoppades över']);

  const stopped = await uploadWith('stop', [
    {ok: false, error: 'exists'}, {ok: true}, {ok: true}, {ok: true},
    {ok: true}, {ok: true}
  ], 6);
  assert.equal(stopped.requests.length, 4, 'failure stops later scheduling but lets in-flight uploads finish');
  assert.doesNotMatch(stopped.requests[0].url, /skip=1|overwrite=1/);
  assert.deepEqual(stopped.toasts, ['same.mid: exists (3 andra filer hann bli klara)']);

  const overwrite = await uploadWith('overwrite', [{ok: true}, {ok: true}]);
  assert.equal(overwrite.requests.length, 2);
  assert.match(overwrite.requests[0].url, /overwrite=1/);

  const internal = await uploadWith('skip', [{ok: true}, {ok: true}, {ok: true}], 3, 'ffat');
  assert.equal(internal.maxActive, 1, 'internal flash uploads remain serialized');
  assert.equal(internal.requests.length, 3);

  const folder = await uploadWith('skip', [{ok: true}, {ok: true}, {ok: true}], 3, 'sd', [
    'Project/Disc One/song.mid', 'Project/Disc One/Sub/song.mid', 'Project/Disc Two/song.mid'
  ]);
  assert.deepEqual(folder.mkdirs.map(x => JSON.parse(x.body).path),
    ['/songs/Project', '/songs/Project/Disc One', '/songs/Project/Disc Two', '/songs/Project/Disc One/Sub']);
  assert.deepEqual(folder.requests.map(x => new URLSearchParams(x.url.split('?')[1]).get('path')).sort(),
    ['/songs/Project/Disc One/song.mid', '/songs/Project/Disc One/Sub/song.mid', '/songs/Project/Disc Two/song.mid'].sort());

  const lowMemory = await uploadWith('skip', [{ok: true}, {ok: true}, {ok: true}], 3, 'sd', [], 1);
  assert.equal(lowMemory.maxActive, 1, 'device-suggested slots limit parallel SD uploads');

  const busy = await uploadWith('skip', [{ok: true}, {status: 429, ok: false, error: 'busy'}, {ok: true}], 2, 'sd', [], 2);
  assert.equal(busy.requests.length, 3, 'a 429 answer retries the same file');
  assert.equal(busy.requests[1].file.name, busy.requests[2].file.name);
  assert.deepEqual(busy.toasts, ['2 filer uppladdade']);

  const tooDeep = await uploadWith('skip', [{ok: true}], 1, 'sd', ['One/Two/Three/Four/song.mid']);
  assert.equal(tooDeep.requests.length, 0, 'directory uploads beyond three levels are rejected');
  assert.equal(tooDeep.mkdirs.length, 0);
  assert.match(tooDeep.toasts[0], /högst 3 mappnivåer/);

  console.log('File upload policies, concurrent SD/serialized flash uploads, and directory handling passed');
})().catch(error => { console.error(error); process.exitCode = 1; });

const server = fs.readFileSync('main/app_web_files.cpp', 'utf8');
assert.match(server, /UploadAdmission::admit\(active, heap_caps_get_free_size\(MALLOC_CAP_INTERNAL\)\)/);
assert.match(fs.readFileSync('main/upload_admission.h', 'utf8'), /constexpr int MAX_UPLOADS = 4;/);
assert.match(server, /if \(ok && !skipped\) changed\(t\);/);
assert.match(server, /if \(sdIoFailed\) BoardSd::requestRecovery\(\);/);
// SD write failure: recover the card before draining the rest of the HTTP body.
assert.match(server, /if \(sdIoFailed\) BoardSd::requestRecovery\(\);[\s\S]*transfer.finish\(\)[\s\S]*body.drain\(\)/);
assert.match(server, /BoardSd::Transfer transfer\(t.sd\)/);
assert.match(server, /if \(t.sd && !BoardSd::transferHealthy\(\)\) pipeline.error.store\(EIO\)/);
assert.match(server, /if \(skipAtStart\) \{\s*transfer.finish\(\);\s*if \(!body.drain\(\)\)[\s\S]*return sendUploadSkipped\(r\);/);
assert.match(server, /handleUploadRequest\(r, body\);[\s\S]*if \(!body.drain\(\)\)[\s\S]*body.disconnect\(\);[\s\S]*httpd_req_async_handler_complete\(r\)/);
