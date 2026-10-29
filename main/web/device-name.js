fetch('/api/config', {cache: 'no-store'})
  .then(r => r.ok ? r.json() : null)
  .then(c => {
    if (!c || !c.hostname) return;
    const name = c.hostname;
    const brand = document.querySelector('.otr-project');
    if (brand) {
      const subtitle = brand.querySelector('small');
      brand.textContent = name;
      if (subtitle) brand.appendChild(subtitle);
    }
    const sep = document.title.indexOf(' – ');
    if (sep >= 0) document.title = name + document.title.slice(sep);
    document.querySelectorAll('footer').forEach(el => {
      el.textContent = el.textContent.replace(/MyEsp-OnTheRoad|ONTHEROAD|OnTheRoad/g, name);
    });
    document.querySelectorAll('a[href="/"]').forEach(el => {
      el.textContent = el.textContent.replace(/ONTHEROAD|OnTheRoad/g, name);
    });
  })
  .catch(error => console.warn('Device name could not be loaded', error));
