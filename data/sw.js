// sw.js — service worker: cache-first app shell.
//
// Caches the static assets (HTML/CSS/JS/manifest) on install so the PWA opens
// instantly and works offline. API calls (/api/*) always bypass the cache.

const CACHE = 'batteryexpert-v1';
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

  e.respondWith(
    caches.match(e.request).then((cached) => {
      if (cached) return cached;
      return fetch(e.request).then((resp) => {
        const copy = resp.clone();
        caches.open(CACHE).then((c) => c.put(e.request, copy));
        return resp;
      });
    })
  );
});
