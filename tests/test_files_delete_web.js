const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('main/web/files.html', 'utf8');
const script = html.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
const helpers = script.slice(script.indexOf('function beginFileOperation'), script.indexOf('function uploadOne'));
const deletion = script.slice(script.indexOf("$('act-delete').onclick"), script.indexOf("$('act-move').onclick"));
async function run({cancelEarly = false, cancelDuring = false, broken = false, failed = []} = {}) {
  const elements = new Map();
  const $ = id => {
    if (!elements.has(id)) elements.set(id, {value: id === 'pin' ? '1234' : '', hidden: true,
      removeAttribute() {}});
    return elements.get(id);
  };
  const requests = [], messages = [];
  let polls = 0, cancelled = false, reloaded = 0;
  const sandbox = {
    $, picked: new Set(['folder']), entries: [{name: 'folder', dir: true}], path: '/test',
    confirm: () => true, join: (a, b) => a + '/' + b,
    guarded: fn => fn().catch(err => messages.push(err.message)),
    sendJson: async (method, url, data) => {
      requests.push({method, url, data});
      if (cancelEarly) await $('cancel-btn').onclick();
      return {job: '0123456789abcdef'};
    },
    qs: o => new URLSearchParams(o).toString(),
    answer: async r => r.json(),
    fetch: async (url, options) => {
      requests.push({url, options});
      assert.equal(options.headers['X-Admin-Pin'], '1234');
      if (url.includes('/cancel?')) { cancelled = true; return {json: async () => ({ok: true})}; }
      if (broken) throw new Error('network');
      polls++;
      if (cancelDuring && polls === 1) await $('cancel-btn').onclick();
      return {json: async () => ({ok: true, finished: polls === 2, cancelled, removed: 5, done: 1, failed})};
    },
    setTimeout: fn => fn(), toast: message => messages.push(message),
    bulkResult: (status, verb) => messages.push(status.done + ' ' + verb + ' failures=' + status.failed.length),
    load: async () => { reloaded++; },
  };
  vm.createContext(sandbox);
  vm.runInContext(helpers + deletion, sandbox);
  await $('act-delete').onclick();
  assert.equal(reloaded, 1);
  assert($('cancel-btn').hidden);
  return {requests, messages};
}
(async () => {
  const success = await run();
  assert.match(success.messages[0], /1 raderade/);
  for (const options of [{cancelEarly: true}, {cancelDuring: true}]) {
    const result = await run(options);
    assert(result.requests.some(r => r.url.includes('/cancel?')));
    assert.match(result.messages[0], /avbröts.*5.*återställs inte/);
    assert(result.requests.filter(r => r.url.includes('/status?')).length === 2, 'wait for server completion');
  }
  const failure = await run({broken: true});
  assert.match(failure.messages[0], /kan fortfarande pågå/);
  const partial = await run({failed: [{path: '/test/folder', error: 'I/O'}]});
  assert.match(partial.messages[0], /failures=1/);
  assert.match(html, /<dialog id="file-result"/);
  assert.match(script, /if \(!ok\)[\s\S]*showModal\(\)/);
  console.log('Deletion UI: job polling, early/active cancel, partial results and uncertain network status passed');
})().catch(err => { console.error(err); process.exitCode = 1; });
