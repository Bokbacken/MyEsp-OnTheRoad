// Execute the actual overview/diagnostics scripts with representative S3 and P4 API data.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

async function page(file, data) {
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
    window: {addEventListener() {}}, EventSource: class {close() {}},
    fetch: async () => ({json: async () => data}), setInterval() {}, setTimeout() {},
    console,
  };
  vm.createContext(sandbox);
  const script = fs.readFileSync(file, 'utf8').match(/<script\b[^>]*>([\s\S]*?)<\/script\s*>/i)[1];
  vm.runInContext(script, sandbox);
  await vm.runInContext('refresh()', sandbox);
  return {element, run: s => vm.runInContext(s, sandbox)};
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
      wifi: {status: 'connected', connected: true, rssi: -39, bars: 5, hostname: 'otr'},
      i2c: ['0x18', '0x40', '0x5D'],
      battery: {supported: true, initialized: true, raw_adc: 1215, adc_calibrated: true, adc_mv: 1212,
        battery_mv: 3636, estimated_percent: 18, scale_permille: 3000, full_reference_mv: 4200, errors: 0},
      boot: {failures: 1, items: [{name: 'RTC', state: 'waiting', detail: 'NTP'}, {name: 'FFat', state: 'fail'}]},
    };
    const diag = await page('main/web/diagnostics.html', data);
    assert.equal(diag.element('live-text').textContent, 'live');
    assert(diag.element('display-title').textContent.includes(data.display.model));
    assert(diag.element('disp').innerHTML.includes(`${data.display.width} × ${data.display.height}`));
    assert(diag.element('touch').innerHTML.includes(p4 ? 'GT911 (0x5D)' : 'ST77922 (0x55)'));
    assert(!diag.element('sys').innerHTML.includes('NaN'));
    assert.equal(diag.element('sys').innerHTML.includes('23.50 °C'), !p4);
    assert(diag.element('battery').innerHTML.includes('3636 mV'));
    assert(diag.element('raw').textContent.includes('scale_permille'));
    const overview = await page('main/web/index.html', data);
    assert(overview.element('boot').innerHTML.includes('väntade på tid vid start'));
    assert(overview.element('boot-sum').textContent.includes('1 kontroll(er) misslyckades'));
    assert(overview.element('ffat-info').textContent.includes('separat från SD-kortet'));
    assert(overview.element('sd-info').textContent.includes('Monterat'));
    data.boot.failures = 0;
    overview.run(`showBoot(${JSON.stringify(data.boot)})`);
    assert(!overview.element('boot-sum').textContent.includes('Alla kontroller'));
  }
  console.log('S3/P4 overview and diagnostics: board identity, RTC wait, separate SD/FFat and battery passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
