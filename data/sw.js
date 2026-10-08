// sw.js — service worker: network-first, cache as offline fallback.
//
// Network-first ensures a re-flashed firmware/PWA is picked up immediately on
// reload. The cache only kicks in when the ESP is unreachable (offline).
// API calls (/api/*) always bypass the cache.

const CACHE = 'batteryexpert-v1';   // bump on release (forces cache rebuild)
const ASSETS = [
  './',
  './index.html',
  './css/app.css',
  './js/state.js',
  './js/sync.js',
  './js/ui.js',
  './manifest.json',
];

self.addEventListener('install', (e) => {
  e.waitUntil(
    caches.open(CACHE).then((c) => c.addAll(ASSETS)).then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (url.pathname.startsWith('/api/')) return;   // never cache API responses

  // Network-first: fresh content after a re-flash; cache only when offline.
  e.respondWith(
    fetch(e.request)
      .then((resp) => {
        const copy = resp.clone();
        caches.open(CACHE).then((c) => c.put(e.request, copy));
        return resp;
      })
      .catch(() => caches.match(e.request).then((cached) => cached || caches.match('./index.html')))
  );
});
