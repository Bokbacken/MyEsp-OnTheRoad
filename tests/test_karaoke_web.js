const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('main/web/karaoke.html', 'utf8');
assert.match(html, /body \{ max-width: none; \}/);
assert.match(html, /min-height: 55vh/);
assert.match(html, /font-size: clamp\(1.5rem, 6vw, 6rem\)/);
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map(), timers = [];
function element() {
  return {style: {}, children: [], textContent: '', dataset: {}, scrollHeight: 40,
    classList: {toggle() {}},
    appendChild(node) { this.children.push(node); this.textContent += node.textContent; }};
}
const sandbox = {
  document: {
    getElementById: id => { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); },
    documentElement: {}, querySelectorAll: () => [],
    createElement: () => element(), createTextNode: text => ({textContent: text})
  },
  getComputedStyle: () => ({fontSize: '48px'}),
  window: {addEventListener() {}},
  navigator: {language: 'sv'}, localStorage: {getItem: () => null},
  location: {protocol: 'http:', host: 'localhost'},
  WebSocket: class { send() {} },
  setTimeout: callback => { timers.push(callback); return timers.length; },
  clearTimeout() {}
};
const context = vm.createContext(sandbox);
vm.runInContext(script, context);
vm.runInContext(`
  applyEvent({type:'bookstart',plain:true});
  applyEvent({type:'book',text:'Första raden\\n\\n'});
  applyEvent({type:'book',text:'Nästa rad'});
`, context);
assert.equal(elements.get('plainLyrics').textContent, 'Första raden\n\nNästa rad');
assert.equal(elements.get('cur').hidden, true);
assert.equal(elements.get('plainLyrics').hidden, false);
assert.equal(elements.get('plainLyrics').style.overflowY, 'auto');
vm.runInContext("applyEvent({type:'newrow',row:0,syls:['Stale event']});", context);
assert.equal(elements.get('plainLyrics').hidden, false);
vm.runInContext(`
  applyEvent({type:'bookstart',plain:false});
  ctx = {currentTime:0}; nextTime = 1;
  scheduleEvent({type:'newrow',row:0,syls:['Old song']});
  connect();
  ws.onmessage({data:JSON.stringify({type:'state',open:true,song:'New song'})});
  scheduleEvent({type:'bookstart',plain:true});
  scheduleEvent({type:'book',text:'New sheet\\nsecond line'});
`, context);
timers.forEach(callback => callback());
assert.equal(elements.get('plainLyrics').textContent, 'New sheet\nsecond line');
assert.equal(elements.get('plainLyrics').hidden, false);
vm.runInContext(`
  applyEvent({type:'bookstart',plain:false});
  applyEvent({type:'newrow',row:1,syls:['Timed LRC row'],next:'Next row'});
  applyEvent({type:'lyric',row:1,k:0,text:'Timed LRC row'});
`, context);
assert.equal(elements.get('plainLyrics').hidden, true);
assert.equal(elements.get('cur').hidden, false);
assert.equal(elements.get('cur').children.at(-1).className, 'now');
console.log('Web karaoke: multiline sheets, timed rows and stale-song event cancellation passed');
