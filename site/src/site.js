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
