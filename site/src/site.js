// Screenshot placeholders, without inline handlers (the site's CSP forbids them).
// An image marked data-slot removes the "Screenshot coming" box before it once it
// loads, or removes itself if it fails; data-fallback swaps in another image.
for (const img of document.querySelectorAll('img[data-slot], img[data-fallback]')) {
  const ok = () => { if (img.hasAttribute('data-slot')) img.previousElementSibling?.remove(); };
  const fail = () => {
    const alt = img.dataset.fallback;
    if (alt) { img.removeAttribute('data-fallback'); img.src = alt; }
    else if (img.hasAttribute('data-slot')) img.remove();
  };
  if (img.complete) (img.naturalWidth ? ok : fail)();
  else { img.addEventListener('load', ok, { once: true }); img.addEventListener('error', fail); }
}

// Scroll-spy: the menu link of the section in view gets aria-current="location"
// (styled like a hovered link). A section is "in view" once its top passes a line
// 35% down the window; above the first section (the hero) nothing is marked.
const spyLinks = [...document.querySelectorAll('.top nav a[href^="#"]')];
const spySections = [...document.querySelectorAll('main section[id]')];
if (spyLinks.length && spySections.length && 'IntersectionObserver' in window) {
  const linkFor = new Map(spyLinks.map((a) => [a.getAttribute('href').slice(1), a]));
  const nav = document.querySelector('.top nav');
  let lockUntil = 0;   // after a click, hold the clicked link while the page scrolls there
  const mark = (id) => {
    for (const a of spyLinks) {
      if (a === linkFor.get(id)) {
        if (a.getAttribute('aria-current') === 'location') continue;
        a.setAttribute('aria-current', 'location');
        // keep it visible in the menu, which scrolls sideways on a phone
        const l = a.offsetLeft - nav.offsetLeft, r = l + a.offsetWidth;
        if (l < nav.scrollLeft || r > nav.scrollLeft + nav.clientWidth) nav.scrollLeft = l - 16;
      } else a.removeAttribute('aria-current');
    }
  };
  const update = () => {
    if (Date.now() < lockUntil) return;
    const line = window.innerHeight * 0.35;
    let current = null;
    for (const s of spySections) if (s.getBoundingClientRect().top <= line) current = s.id;
    mark(current);
  };
  const io = new IntersectionObserver(update, { rootMargin: '0px 0px -65% 0px' });
  for (const s of spySections) io.observe(s);
  const unlock = () => { lockUntil = 0; update(); };
  for (const a of spyLinks) a.addEventListener('click', () => {
    mark(a.getAttribute('href').slice(1));
    lockUntil = Date.now() + 1500;
    setTimeout(unlock, 1600);
  });
  window.addEventListener('scrollend', () => { if (lockUntil) unlock(); });
  const hash = decodeURIComponent(location.hash.slice(1));
  if (linkFor.has(hash)) mark(hash);
  update();
}
