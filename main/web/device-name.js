(() => {
  const links = [
    ['/', 'Översikt', 'Overview'], ['/game', 'Live', 'Live'], ['/play', 'Spela', 'Play'],
    ['/adventure', 'Äventyr', 'Adventure'], ['/adventure-editor', 'Editor', 'Editor'],
    ['/karaoke', 'Karaoke', 'Karaoke'], ['/karaoke/songs', 'Låtar', 'Songs'],
    ['/players', 'Spelare', 'Players'], ['/settings', 'Inställningar', 'Settings'],
    ['/diag', 'Diagnostik', 'Diagnostics'], ['/files', 'Filer', 'Files'], ['/update', 'Uppdatera', 'Update']
  ];
  const aliases = {'/index.html': '/', '/diagnostics.html': '/diag', '/songs_picker.html': '/karaoke/songs'};
  const current = aliases[location.pathname] || location.pathname.replace(/\.html$/, '') || '/';
  let subtitle = document.title.split(' – ').slice(1).join(' – ');
  let deviceName = 'OnTheRoad';
  let header = document.querySelector('header.otr-brand');
  if (!header) {
    header = document.createElement('header');
    header.className = 'otr-brand';
    const head = document.createElement('div');
    head.className = 'otr-head';
    const logo = document.createElement('div');
    logo.className = 'otr-logo';
    logo.textContent = '⚄';
    const brand = document.createElement('div');
    brand.className = 'otr-project';
    brand.textContent = 'OnTheRoad';
    const small = document.createElement('small');
    small.textContent = subtitle;
    brand.appendChild(small);
    head.append(logo, brand);
    header.appendChild(head);
    document.body.prepend(header);
  }
  let nav = document.querySelector('nav.otr-nav');
  if (!nav) nav = document.createElement('nav');
  nav.className = 'otr-nav';
  nav.setAttribute('aria-label', 'Huvudmeny');
  nav.replaceChildren();
  for (const [href, label] of links) {
    const a = document.createElement('a');
    a.href = href;
    a.textContent = label;
    if (current === href) {
      a.className = 'active';
      a.setAttribute('aria-current', 'page');
    }
    nav.appendChild(a);
  }
  const chrome = document.createElement('div');
  chrome.className = 'otr-chrome';
  header.before(chrome);
  chrome.append(header, nav);
  const status = document.createElement('div');
  status.className = 'otr-wifi otr-device-status';
  const dot = document.createElement('span');
  dot.className = 'dot';
  const statusText = document.createElement('span');
  statusText.textContent = 'Ansluter…';
  status.append(dot, statusText);
  header.querySelector('.otr-head').appendChild(status);

  const templates = new WeakMap();
  let english = false;
  function apply(config) {
    const name = config.hostname || config.wifi?.hostname;
    if (!name) return;
    deviceName = name;
    if (config.lang) english = config.lang === 'en';
    links.forEach((link, i) => { nav.children[i].textContent = link[english ? 2 : 1]; });
    nav.setAttribute('aria-label', english ? 'Main navigation' : 'Huvudmeny');
    const brand = header.querySelector('.otr-project');
    const small = brand.querySelector('small');
    brand.textContent = name;
    if (small) brand.appendChild(small);
    document.title = name + (subtitle ? ' – ' + subtitle : '');
    document.querySelectorAll('footer, a[href="/"]').forEach(el => {
      let cached = templates.get(el);
      if (!cached || el.textContent !== cached.last)
        cached = {original: el.textContent, last: el.textContent};
      if (/MyEsp-OnTheRoad|ONTHEROAD|OnTheRoad/.test(cached.original)) {
        el.textContent = cached.original.replace(/MyEsp-OnTheRoad|ONTHEROAD|OnTheRoad/g, name);
        cached.last = el.textContent;
      }
      templates.set(el, cached);
    });
    if (config.wifi) {
      const wifi = config.wifi;
      dot.className = 'dot ' + (wifi.connected ? 'ok' : wifi.status === 'error' ? 'bad' : 'warn');
      statusText.textContent = wifi.connected ? wifi.ssid + ' · ' + wifi.rssi + ' dBm' :
        wifi.status === 'off' ? (english ? 'Wi-Fi off' : 'Wi-Fi av') :
          (english ? 'Not connected' : 'Inte ansluten');
    }
  }
  let pending = false;
  async function refresh() {
    if (pending || document.hidden) return;
    pending = true;
    try {
      const response = await fetch('/api/info', {cache: 'no-store'});
      if (!response.ok) throw new Error('HTTP ' + response.status);
      apply(await response.json());
    } catch (error) {
      dot.className = 'dot bad';
      statusText.textContent = english ? 'Connection lost' : 'Ingen kontakt';
      console.warn('Device status could not be loaded', error);
    } finally {
      pending = false;
    }
  }
  window.OtrDevice = {
    apply, refresh,
    setTitle(title) {
      subtitle = title;
      document.title = deviceName + (title ? ' – ' + title : '');
    }
  };
  window.addEventListener('pageshow', refresh);
  document.addEventListener('visibilitychange', refresh);
  setInterval(refresh, 10000);
  refresh();
})();
