// Execute the actual overview/diagnostics scripts with representative S3 and P4 API data.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

async function page(file, data) {
  const requests = [];
  let sdEnabled = true;
  let logConfig = {level: 'info', sd_trace: false, sd_trace_supported: data.chip === 'esp32p4', trace_seconds: 0};
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {
      style: {}, value: '', textContent: '', innerHTML: '', className: '',
      appendChild() {}, insertAdjacentHTML() {}, childNodes: [],
    });
    return elements.get(id);
  };
  const sandbox = {
    document: {getElementById: element, querySelectorAll: () => [], createElement: () => element(Symbol())},
    window: {addEventListener() {}, prompt: () => ''}, EventSource: class {close() {}},
    fetch: async (url, options) => {
      requests.push({url, options});
      if (url.startsWith('/api/log/sd')) {
        if (options?.method === 'POST') sdEnabled = url.includes('enabled=1');
        return {ok: true, json: async () => ({enabled: sdEnabled})};
      }
      if (url.startsWith('/api/log/config')) {
        if (options?.method === 'POST') {
          const q = new URL('http://localhost' + url).searchParams;
          logConfig = {...logConfig, level: q.get('level'), sd_trace: q.get('sd_trace') === '1'};
          logConfig.trace_seconds = logConfig.sd_trace ? 600 : 0;
        }
        return {ok: true, json: async () => logConfig};
      }
      return {ok: true, json: async () => data};
    }, setInterval() {}, setTimeout() {},
    console,
  };
  vm.createContext(sandbox);
  const script = fs.readFileSync(file, 'utf8').match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
  vm.runInContext(script, sandbox);
  await vm.runInContext('refresh()', sandbox);
  return {element, requests, run: s => vm.runInContext(s, sandbox)};
}

(async () => {
  for (const p4 of [false, true]) {
    const data = {
      fw: 'OnTheRoad', version: 'test', chip: p4 ? 'esp32p4' : 'esp32s3', cpu_mhz: 400,
      heap_free: 300000, heap_min: 280000, heap_size: 600000, psram_free: 31000000, psram_size: 32000000,
      flash_size: p4 ? 33554432 : 16777216,
      ota: {running: 'app1', next: 'app0', slot_size: 5242880},
      storage: {ok: false},
      sd: {ok: true, name: 'TEST', width: 4, bus: '4-bit', freq_khz: 10000, total: 32000000, free: 30000000},
      clock: {epoch: 1793282400, source: 'NTP', certain: true, ntp_age_s: 2},
      rtc: {chip: p4 ? 'ESP32-P4 RTC' : 'DS3231', ok: true, present: true,
        temp_c: p4 ? null : 23.5, osf_at_boot: p4},
      display: {ok: true, model: p4 ? 'ST7701 MIPI-DSI' : 'ST77922 QSPI',
        width: p4 ? 480 : 320, height: p4 ? 800 : 480, bus: p4 ? 'MIPI-DSI 2x500 Mbps' : 'QSPI 40 MHz', fps_x10: 30},
      touch: {present: true, model: p4 ? 'GT911' : 'ST77922', address: p4 ? 0x5D : 0x55,
        fw: p4 ? '0x1060 (GT911)' : '96 (1.2.3.4)', touches: 50},
      wifi: {status: 'connected', connected: true, rssi: -39, bars: 5, hostname: 'otr',
        ...(p4 ? {host_radio: {known: true, host_version: '3.0.10', coprocessor_version: '3.0.10', match: true}} : {})},
      i2c: ['0x18', '0x40', '0x5D'],
      battery: {supported: true, initialized: true, raw_adc: 1215, adc_calibrated: true, adc_mv: 1212,
        battery_mv: 3636, estimated_percent: 18, scale_permille: 3000, full_reference_mv: 4200, errors: 0},
      boot: {failures: 1, items: [{name: 'RTC', state: 'waiting', detail: 'NTP'}, {name: 'FFat', state: 'fail'},
        ...(p4 ? [{name: 'Host-radio (ESP-Hosted)', state: 'warn', detail: '3.0.9 / 3.0.10'}] : [])]},
    };
    const diag = await page('main/web/diagnostics.html', data);
    assert.equal(diag.element('live-text').textContent, 'live');
    assert(diag.element('display-title').textContent.includes(data.display.model));
    assert(diag.element('disp').innerHTML.includes(`${data.display.width} × ${data.display.height}`));
    assert(diag.element('touch').innerHTML.includes(p4 ? 'GT911 (0x5D)' : 'ST77922 (0x55)'));
    assert(!diag.element('sys').innerHTML.includes('NaN'));
    assert.equal(diag.element('sys').innerHTML.includes('23.50 °C'), !p4);
    assert.equal(diag.element('wifi').innerHTML.includes('3.0.10'), p4);
    assert(diag.element('battery').innerHTML.includes('3636 mV'));
    assert(diag.element('raw').textContent.includes('scale_permille'));
    await diag.run('sdLogRefresh()');
    await diag.run('logConfigRefresh()');
    assert.equal(diag.element('log-level').value, 'info');
    assert.equal(diag.element('log-trace-toggle').disabled, !p4);
    diag.run(`window.prompt = () => '12&34'`);
    diag.element('log-level').value = 'debug';
    await diag.element('log-level').onchange();
    assert.equal(diag.element('log-level').value, 'debug');
    assert.match(diag.requests.at(-1).url, /level=debug&sd_trace=0&pin=12%2634/);
    if (p4) {
      await diag.element('log-trace-toggle').onclick();
      assert.equal(diag.element('log-trace-toggle').textContent, 'Stoppa SD-spårning');
      assert.match(diag.requests.at(-1).url, /sd_trace=1/);
    }
    diag.run('window.prompt = () => null');
    diag.element('log-level').value = 'error';
    await diag.element('log-level').onchange();
    assert.equal(diag.element('log-level').value, 'debug');
    assert.equal(diag.element('log-sd-toggle').disabled, false);
    assert.equal(diag.element('log-sd-state').textContent, 'SD-loggning på');
    diag.run(`window.prompt = () => '12&34'`);
    await diag.element('log-sd-toggle').onclick();
    assert.equal(diag.element('log-sd-toggle').textContent, 'Återuppta SD-loggning');
    assert.equal(diag.requests.at(-1).url, '/api/log/sd?enabled=0&pin=12%2634');
    assert.equal(diag.requests.at(-1).options.method, 'POST');
    await diag.element('log-sd-toggle').onclick();
    assert.equal(diag.element('log-sd-toggle').textContent, 'Pausa SD-loggning');
    const beforeCancel = diag.requests.length;
    diag.run('window.prompt = () => null');
    await diag.element('log-sd-toggle').onclick();
    assert.equal(diag.requests.length, beforeCancel);
    diag.run(`const originalFetch = fetch; window.prompt = () => ''; fetch = async () => ({
      ok: false, json: async () => ({error: 'Fel PIN'})
    })`);
    await diag.element('log-sd-toggle').onclick();
    assert.equal(diag.element('log-sd-state').textContent, 'Fel PIN');
    assert.equal(diag.element('log-sd-toggle').disabled, false);
    assert.equal(diag.element('log-sd-toggle').textContent, 'Pausa SD-loggning');
    diag.run(`fetch = async () => ({ok: true, json: async () => ({})})`);
    await diag.element('log-sd-toggle').onclick();
    assert.equal(diag.element('log-sd-state').textContent, 'Ogiltig SD-loggstatus');
    diag.run(`fetch = async () => { throw new Error('Network failed'); }`);
    await diag.element('log-sd-toggle').onclick();
    assert.equal(diag.element('log-sd-state').textContent, 'Network failed');
    diag.run('fetch = originalFetch');
    diag.run(`fetch = async () => ({ok: false, json: async () => ({error: 'Fel PIN'})})`);
    diag.element('log-level').value = 'error';
    await diag.element('log-level').onchange();
    assert.equal(diag.element('log-level').value, 'debug');
    assert.equal(diag.element('log-config-state').textContent, 'Fel PIN');
    diag.run('fetch = originalFetch');
    data.display.sleeping = true;
    data.display.brightness = 55;
    await diag.run('refresh()');
    assert(diag.element('disp').innerHTML.includes('Släckt · vald ljusstyrka 55 %'));
    const overview = await page('main/web/index.html', data);
    assert.equal(overview.element('host-radio').textContent.includes('3.0.10'), p4);
    assert(overview.element('boot').innerHTML.includes('väntade på tid vid start'));
    assert.equal(overview.element('boot').innerHTML.includes('⚠ varning'), p4);
    assert(overview.element('boot-sum').textContent.includes('1 kontroll(er) misslyckades'));
    assert(overview.element('ffat-info').textContent.includes('separat från SD-kortet'));
    assert(overview.element('sd-info').textContent.includes('Monterat'));
    data.boot.failures = 0;
    overview.run(`showBoot(${JSON.stringify(data.boot)})`);
    assert(!overview.element('boot-sum').textContent.includes('Alla kontroller'));
  }
  // Run the real settings fill/submit path: old firmware defaults off, new values round-trip.
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {
      value: '', style: {}, events: {}, hidden: false,
      appendChild(child) { this.options.push(child); },
      replaceChildren() { this.options = []; },
      addEventListener(name, fn) { this.events[name] = fn; },
      options: id === 'text_size' ? ['0', '1', '2', '3'].map(value => ({value})) : [],
    });
    return elements.get(id);
  };
  let submitted;
  const sandbox = {
    document: {getElementById: element, createElement: () => element(Symbol())},
    window: {},
    URLSearchParams, console, clearTimeout() {}, setTimeout() {},
    fetch: async (url, options) => {
      assert.equal(url, '/api/config');
      submitted = options.body;
      return {ok: true, json: async () => ({config: {hostname: 'otr', screen_off_seconds: 120}})};
    },
  };
  vm.createContext(sandbox);
  const settings = fs.readFileSync('main/web/settings.html', 'utf8')
    .match(/<script\b(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script\s*>/i)[1]
    .replace(/load\(\);\s*loadNets\(\);\s*status\(\);\s*setInterval\(status, 4000\);/, '');
  vm.runInContext(settings, sandbox);
  vm.runInContext("fill({hostname:'otr',has_rgb_led:true,has_host_radio:false})", sandbox);
  assert.equal(element('led-controls').hidden, false);
  assert.equal(element('screen_off_seconds').value, 0);
  assert.equal(element('eco_sleep').checked, false);
  assert.equal(element('power_log').checked, false);
  assert.equal(element('text_size').value, 1);
  assert(element('text_size').options[3].disabled, 'S3 firmware without text_size_max hides extra large');
  vm.runInContext("fill({hostname:'otr',screen_off_seconds:120,eco_sleep:true,power_log:true,text_size:3,text_size_max:3,has_rgb_led:false,has_host_radio:true})", sandbox);
  assert.equal(element('screen_off_seconds').value, 120);
  assert.equal(element('text_size').value, 3);
  assert(!element('text_size').options[3].disabled, 'P4 offers extra large');
  assert.equal(element('led-controls').hidden, true, 'P4 has no RGB LED controls');
  element('tz').value = 'CET-1';
  await element('cfg-form').events.submit({preventDefault() {}});
  assert.equal(submitted.get('screen_off_seconds'), '120');
  assert.equal(submitted.get('eco_sleep'), '1');
  assert.equal(submitted.get('power_log'), '1');
  assert.equal(submitted.get('text_size'), '3');
  assert.equal(element('toast').textContent, 'Inställningar sparade');

  for (const file of fs.readdirSync('main/web').filter(name => name.endsWith('.html'))) {
    assert.match(fs.readFileSync(`main/web/${file}`, 'utf8'), /rel="icon"[^>]+href="\/favicon\.svg"/);
  }
  assert.match(fs.readFileSync('main/web_assets.h', 'utf8'), /"\/favicon\.ico", "image\/svg\+xml"/);

  const assetHeader = fs.readFileSync('main/web_assets.h', 'utf8');
  const assetTable = assetHeader.match(/WEB_ASSETS\[\]\s*=\s*\{([\s\S]*?)\n\};/)[1];
  const assetCount = [...assetTable.matchAll(/^\s*\{"\//gm)].length;
  const appWeb = fs.readFileSync('main/app_web.cpp', 'utf8');
  const reserve = Number(appWeb.match(/c\.maxUris\s*=\s*sizeof\(WEB_ASSETS\)\s*\/\s*sizeof\(WEB_ASSETS\[0\]\)\s*\+\s*(\d+);/)[1]);
  const routeCount = fs.readdirSync('main').filter(file => file.endsWith('.cpp'))
    .reduce((count, file) => count + (fs.readFileSync(`main/${file}`, 'utf8').match(/\b(?:MyWeb::on|httpd_register_uri_handler)\s*\(/g) || []).length, 0);
  assert(assetCount + reserve >= assetCount + routeCount + 4,
    'HTTP URI handler capacity includes every generated asset and registered API/WebSocket route with headroom');

  const diagHtml = fs.readFileSync('main/web/diagnostics.html', 'utf8');
  assert.match(diagHtml, /\/api\/diag\/crash\?kind=' \+ kind/);
  assert.match(diagHtml, /confirm\('Krascha enheten nu/);
  assert.match(appWeb, /MyWeb::on\("\/api\/diag\/crash", HTTP_POST, handleDiagCrash\)/);
  assert.match(appWeb, /handleDiagCrash[\s\S]*?adminAllowed\(\)/);

  console.log('S3/P4 diagnostics and settings: board identity, RTC, SD/FFat, battery, screen power, text size and RGB availability passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
