// Exercise the script shipped by /adventure, without browser-only test dependencies.
// Run: node tests/test_adv_live_web.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ops = [];
const context = new Proxy({}, {
  get: (o, key) => key in o ? o[key] : (...args) => ops.push([key, o.fillStyle, ...args]),
  set: (o, key, value) => (o[key] = value, true)
});
const elements = new Map();
const element = id => {
  if (!elements.has(id)) elements.set(id, {
    style: {}, value: 'fit', clientWidth: 700, hidden: false,
    getContext: () => context, getBoundingClientRect: () => ({top: 0})
  });
  return elements.get(id);
};
const sandbox = {
  console, document: {
    getElementById: element, createElement: () => element(Symbol()),
    addEventListener() {}
  },
  Image: class { constructor() { this.complete = false; } },
  window: {matchMedia: () => ({matches: true}), addEventListener() {}, innerHeight: 700},
  localStorage: {getItem: () => null}, performance: {now: () => 1000},
  requestAnimationFrame() {}, setTimeout() {}, fetch: () => new Promise(() => {})
};
vm.createContext(sandbox);
const html = fs.readFileSync('main/web/adventure.html', 'utf8');
const script = html.match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i);
assert(script, 'adventure page contains a script');
vm.runInContext(script[1], sandbox);
const run = source => vm.runInContext(source, sandbox);
run(`map = {w: 4, h: 2, terrain: ['a~?#', 'aaaa'], obj: ['....', '.f..'], hint: [], fog: false};`);
run(`for (let code = 97; code <= 127; ++code) {
  map.terrain[1] = String.fromCharCode(code) + 'aaa';
  if (!visibleCell(0, 1)) throw new Error('Terrain encoding collided with a hidden-cell marker: ' + code);
} map.terrain[1] = 'aaaa';`);
ops.length = 0;
run(`terrain(ctx, '~', 1, 0)`);
assert(ops.some(o => o[1] === '#ab47bc'), 'purple plate retains its terrain art');
assert(!ops.some(o => o[1] === '#06060c'), 'purple plate is not darkness');
ops.length = 0;
run(`terrain(ctx, '#', 3, 0)`);
assert(ops.some(o => o[1] === '#06060c'), 'darkness uses its distinct out-of-range marker');
ops.length = 0;
run(`drawPortalMarkers(ctx, [{x:0,y:0},{x:1,y:1},{x:2,y:0},{x:3,y:0}]);`);
assert.equal(ops.filter(o => o[0] === 'stroke' && o[1] === '#25e0d0').length, 2,
  'visible portal sources get ring markers while fog/dark sources stay hidden');
ops.length = 0;
run(`object(ctx, 'f')`);
assert(ops.some(o => o[1] === '#37cdd7'), 'suction pickup has its own art');
ops.length = 0;
run(`drawEnemy(ctx, {x: 0, y: 0, kind: 'patrol'}); drawEnemy(ctx, {x: 1, y: 0, kind: 'chaser'});`);
assert(ops.some(o => o[1] === '#a564eb') && ops.some(o => o[1] === '#eb414b'));
ops.length = 0;
run(`drawEnemy(ctx, {x: 2, y: 0, kind: 'patrol'}); drawEnemy(ctx, {x: 3, y: 0, kind: 'chaser'});`);
assert.equal(ops.length, 0, 'fog and darkness suppress enemy overlays');
run(`renderHud({energy:100,energy_max:100,lives:3,keys:0,gems:0,gems_total:0,
  suction:true,skates:true,flashlight:2,pogo:3,items:[],lang:1});`);
assert(element('items').innerHTML.includes('Suction · Pull on device'));
assert(element('items').innerHTML.includes('Skates'));
run(`renderHud({energy:100,energy_max:100,lives:3,keys:0,gems:0,gems_total:0,
  suction:false,items:[],lang:0});`);
assert(!element('items').innerHTML.includes('Sugkopp'));
ops.length = 0;
run(`drawResources(ctx, {resource_folder:'z_resource_test',
  resource_types:[{name:'Stone',image:'stone.png'}],
  resources:[{x:0,y:0,type:0},{x:2,y:0,type:0}]});`);
assert(ops.some(o => o[1] === '#eb9b2d'), 'resource has a visible bag before PNG loads');
assert.equal(run(`resourceImages.size`), 1, 'one image request per resource type');
assert(run(`resourceImages.get('stone.png').src`).includes('z_resource_test%2Fbitmaps%2Fstone.png'));
ops.length = 0;
run(`const loadedStone=resourceImages.get('stone.png'); loadedStone.complete=true;
  loadedStone.naturalWidth=loadedStone.naturalHeight=32;
  drawResources(ctx, {resource_folder:'z_resource_test', resource_types:[{image:'stone.png'}],
    resources:[{x:0,y:0,type:0},{x:2,y:0,type:0}]});`);
assert.equal(ops.filter(o => o[0] === 'drawImage').length, 1, 'PNG visible, hidden resource suppressed');
run(`renderHud({energy:8,energy_max:8,lives:3,items:[{name:'<Stone>',count:2,cap:2}],lang:1});`);
assert(element('items').innerHTML.includes('&lt;Stone&gt;'), 'inventory escapes authored names');
console.log('Adventure live production web: suction art/HUD, distinct enemy overlays and fog/darkness OK');
