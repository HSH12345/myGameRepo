'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const { randomBytes } = require('node:crypto');
const { setTimeout: delay } = require('node:timers/promises');
const { createApplication } = require('../app');
const { readConfig } = require('../config');
const { firebaseUid } = require('../naver-provider');

const fakeConfig = () => readConfig({ FIREBASE_SERVICE_ACCOUNT: 'unused.json', NAVER_CLIENT_ID: 'test-client',
  NAVER_CLIENT_SECRET: 'test-secret', NAVER_REDIRECT_URI: 'http://localhost:3000/auth/naver/callback' });
function deferred() { let resolve; const promise = new Promise(done => { resolve = done; }); return { promise, resolve }; }
async function waitFor(predicate) {
  for (let i = 0; i < 200; i++) { if (predicate()) return; await delay(5); }
  throw new Error('Expected asynchronous operation did not start.');
}
async function fixture(t, options = {}) {
  const config = fakeConfig();
  Object.assign(config.naver, options.limits);
  if (options.disabled) config.naver = null;
  const calls = { identify: [], mint: [], logs: [] };
  let time = Date.now();
  const application = createApplication({ config, now: () => time,
    identify: async (...args) => { calls.identify.push(args); return options.identify ? options.identify(...args) : 'test-naver-subject'; },
    auth: { createCustomToken: async (...args) => { calls.mint.push(args); return options.mint ? options.mint(...args) : 'synthetic-custom-token'; } },
    log: (...args) => calls.logs.push(args)
  });
  const server = application.app.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const base = `http://127.0.0.1:${server.address().port}`;
  t.after(async () => { application.close(); server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); });
  async function post(route, attempt, override = {}) {
    return fetch(base + '/auth/naver/' + route, { method: 'POST', headers: { 'Content-Type': 'application/json',
      ...(attempt ? { Authorization: `Bearer ${attempt.exchangeSecret}` } : {}), ...override.headers },
      body: JSON.stringify(attempt ? { requestId: attempt.requestId } : {}), ...override });
  }
  async function start() {
    const response = await post('start');
    assert.equal(response.status, 201);
    return response.json();
  }
  async function open(attempt) {
    const login = new URL(attempt.loginUrl);
    const response = await fetch(base + login.pathname + login.search, { redirect: 'manual' });
    assert.equal(response.status, 303);
    const authorization = new URL(response.headers.get('location'));
    const setCookie = response.headers.get('set-cookie');
    assert.match(setCookie, /HttpOnly/);
    assert.match(setCookie, /SameSite=Lax/);
    return { state: authorization.searchParams.get('state'), cookie: setCookie.split(';')[0], authorization };
  }
  async function callback(browser, query = { code: 'test-code' }, cookie = browser.cookie) {
    return fetch(base + '/auth/naver/callback?' + new URLSearchParams({ state: browser.state, ...query }), {
      headers: cookie ? { Cookie: cookie } : {}, redirect: 'manual'
    });
  }
  async function ready() { const attempt = await start(); const browser = await open(attempt); assert.equal((await callback(browser)).status, 200); return attempt; }
  return { config, calls, application, base, post, start, open, callback, ready, advance: milliseconds => { time += milliseconds; } };
}

test('full handoff: browser has no Firebase token; only owner can exchange once', async t => {
  const f = await fixture(t);
  const a = await f.start();
  assert.equal(new URL(a.loginUrl).searchParams.has('exchangeSecret'), false);
  assert.equal((await f.post('exchange', a)).status, 409);
  const browser = await f.open(a);
  assert.equal(browser.authorization.origin, 'https://nid.naver.com');
  assert.equal(browser.authorization.searchParams.get('redirect_uri'), f.config.naver.redirectUri);
  const callback = await f.callback(browser);
  const html = await callback.text();
  assert.equal(callback.status, 200);
  assert.match(callback.headers.get('cache-control'), /no-store/);
  assert.equal(callback.headers.get('referrer-policy'), 'no-referrer');
  assert.doesNotMatch(html, /synthetic-custom-token|test-naver-subject|naver_v1_/);
  assert.equal(f.calls.mint.length, 0);
  assert.equal((await (await f.post('status', a)).json()).status, 'ready');
  const stolen = { ...a, exchangeSecret: randomBytes(32).toString('base64url') };
  assert.equal((await f.post('exchange', stolen)).status, 404);
  const exchange = await f.post('exchange', a);
  const body = await exchange.json();
  assert.equal(exchange.status, 200);
  assert.equal(body.customToken, 'synthetic-custom-token');
  assert.equal(body.uid, firebaseUid('test-client', 'test-naver-subject'));
  assert.deepEqual(f.calls.mint[0], [body.uid, { rpg_provider: 'naver' }]);
  assert.equal((await f.post('exchange', a)).status, 409);
  assert.equal((await (await f.post('status', a)).json()).status, 'consumed');
  assert.equal(f.calls.mint.length, 1);
  assert.doesNotMatch(JSON.stringify(f.calls.logs), /synthetic-custom-token|test-code|test-secret/);
});

test('same subject maps to same Firebase UID across requests; different subject/app stays separate', async t => {
  const f = await fixture(t);
  const first = await (await f.post('exchange', await f.ready())).json();
  const second = await (await f.post('exchange', await f.ready())).json();
  assert.equal(first.uid, second.uid);
  assert.notEqual(first.uid, firebaseUid('test-client', 'different-subject'));
  assert.notEqual(first.uid, firebaseUid('other-app', 'test-naver-subject'));
  assert.ok(first.uid.length <= 128);
});

test('wrong state, missing/wrong browser cookie and duplicate query are rejected without consuming valid state', async t => {
  const f = await fixture(t);
  const a = await f.start(); const b = await f.open(a);
  assert.equal((await f.callback({ ...b, state: randomBytes(32).toString('base64url') })).status, 400);
  assert.equal((await f.callback(b, { code: 'test-code' }, '')).status, 403);
  assert.equal((await f.callback(b, { code: 'test-code' }, b.cookie.replace(/=.+$/, '=' + 'x'.repeat(43)))).status, 403);
  const duplicate = await fetch(f.base + '/auth/naver/callback?' + new URLSearchParams({ state: b.state, code: 'one' }) + '&code=two', { headers: { Cookie: b.cookie } });
  assert.equal(duplicate.status, 400);
  assert.equal((await f.callback(b)).status, 200);
  assert.equal((await f.callback(b)).status, 400);
  assert.equal(f.calls.identify.length, 1);
});

test('authorization denial is terminal and provider descriptions never reach output/logs', async t => {
  const f = await fixture(t); const a = await f.start(); const b = await f.open(a);
  const response = await f.callback(b, { error: 'access_denied', error_description: 'PRIVATE_SENTINEL' });
  assert.equal(response.status, 200);
  assert.doesNotMatch(await response.text(), /PRIVATE_SENTINEL/);
  assert.equal((await (await f.post('status', a)).json()).status, 'cancelled');
  assert.equal((await f.post('exchange', a)).status, 409);
  assert.equal(f.calls.identify.length, 0);
  assert.doesNotMatch(JSON.stringify(f.calls.logs), /PRIVATE_SENTINEL/);
});

test('expiry frees bounded request capacity; ready result also expires after 60 seconds', async t => {
  const f = await fixture(t, { limits: { maxAttempts: 1 } });
  const old = await f.start();
  assert.equal((await f.post('start')).status, 503);
  f.advance(300_001);
  assert.equal((await f.post('status', old)).status, 404);
  const a = await f.ready();
  f.advance(60_001);
  assert.equal((await f.post('exchange', a)).status, 404);
  assert.equal(f.calls.mint.length, 0);
});

test('rate limit and cross-origin POST reject abuse', async t => {
  const f = await fixture(t, { limits: { startsPerMinute: 1 } });
  assert.equal((await f.post('start', null, { headers: { Origin: 'https://untrusted.example' } })).status, 403);
  await f.start();
  const response = await f.post('start');
  assert.equal(response.status, 429);
  assert.ok(Number(response.headers.get('retry-after')) > 0);
  f.advance(60_001);
  await f.start();
});

test('simultaneous callbacks claim state once', async t => {
  const pending = deferred(); const f = await fixture(t, { identify: () => pending.promise });
  const a = await f.start(); const b = await f.open(a);
  const first = f.callback(b); await waitFor(() => f.calls.identify.length === 1);
  assert.equal((await f.callback(b)).status, 400);
  pending.resolve('test-naver-subject');
  assert.equal((await first).status, 200);
  assert.equal(f.calls.identify.length, 1);
});

test('simultaneous exchange claims signing once', async t => {
  const pending = deferred(); const f = await fixture(t, { mint: () => pending.promise });
  const a = await f.ready(); const first = f.post('exchange', a);
  await waitFor(() => f.calls.mint.length === 1);
  assert.equal((await f.post('exchange', a)).status, 409);
  pending.resolve('synthetic-custom-token');
  assert.equal((await first).status, 200);
  assert.equal(f.calls.mint.length, 1);
});

test('callback saturation keeps the waiting state retryable', async t => {
  const pending = deferred();
  const f = await fixture(t, { limits: { maxOperations: 1 }, identify: () => f.calls.identify.length === 1 ? pending.promise : 'test-naver-subject' });
  const a = await f.start(); const b = await f.start();
  const firstBrowser = await f.open(a); const secondBrowser = await f.open(b);
  const first = f.callback(firstBrowser); await waitFor(() => f.calls.identify.length === 1);
  assert.equal((await f.callback(secondBrowser)).status, 503);
  pending.resolve('test-naver-subject');
  assert.equal((await first).status, 200);
  assert.equal((await f.callback(secondBrowser)).status, 200);
});

test('network timeout aborts identification and later results cannot authenticate', async t => {
  let aborted = false;
  const f = await fixture(t, { limits: { operationTimeoutMs: 100 }, identify: (_code, _state, signal) => new Promise((_resolve, reject) => {
    signal.addEventListener('abort', () => { aborted = true; reject(new Error('aborted')); }, { once: true });
  }) });
  const a = await f.start(); const response = await f.callback(await f.open(a));
  assert.equal(response.status, 504);
  assert.equal(aborted, true);
  assert.equal((await (await f.post('status', a)).json()).error, 'auth_timeout');
  assert.equal((await f.post('exchange', a)).status, 409);
});

test('cancelling in-flight identification cannot resurrect a ready request', async t => {
  const pending = deferred(); const f = await fixture(t, { identify: () => pending.promise });
  const a = await f.start(); const b = await f.open(a);
  const callback = f.callback(b); await waitFor(() => f.calls.identify.length === 1);
  assert.equal((await f.post('cancel', a)).status, 200);
  pending.resolve('test-naver-subject');
  assert.equal((await callback).status, 410);
  assert.equal((await (await f.post('status', a)).json()).status, 'cancelled');
  assert.equal((await f.post('exchange', a)).status, 409);
});

test('cancel and expiry during signing suppress token delivery', async t => {
  for (const action of ['cancel', 'expire']) {
    const pending = deferred(); const f = await fixture(t, { mint: () => pending.promise });
    const a = await f.ready(); const exchange = f.post('exchange', a);
    await waitFor(() => f.calls.mint.length === 1);
    if (action === 'cancel') await f.post('cancel', a); else f.advance(60_001);
    pending.resolve('synthetic-custom-token');
    const response = await exchange;
    assert.equal(response.status, 410);
    assert.doesNotMatch(await response.text(), /synthetic-custom-token/);
  }
});

test('timed-out signing holds operation capacity until underlying promise settles', async t => {
  const pending = deferred();
  const f = await fixture(t, { limits: { maxOperations: 1, operationTimeoutMs: 100 }, mint: () => f.calls.mint.length === 1 ? pending.promise : 'synthetic-custom-token' });
  const a = await f.ready(); const b = await f.ready();
  const response = await f.post('exchange', a);
  assert.equal(response.status, 504);
  assert.equal((await f.post('exchange', b)).status, 503);
  pending.resolve('discarded-late-token'); await delay(10);
  assert.equal((await f.post('exchange', b)).status, 200);
  assert.equal((await (await f.post('status', a)).json()).status, 'failed');
});

test('provider/signing failures are opaque and terminal', async t => {
  const badProvider = await fixture(t, { identify: () => { throw new Error('PRIVATE_SENTINEL'); } });
  const a = await badProvider.start();
  const response = await badProvider.callback(await badProvider.open(a));
  assert.equal(response.status, 502);
  assert.doesNotMatch(await response.text(), /PRIVATE_SENTINEL/);
  assert.equal((await (await badProvider.post('status', a)).json()).status, 'failed');
  const badSigner = await fixture(t, { mint: () => { throw new Error('PRIVATE_SENTINEL'); } });
  const b = await badSigner.ready();
  const exchange = await badSigner.post('exchange', b);
  assert.equal(exchange.status, 502);
  assert.deepEqual(await exchange.json(), { error: 'token_creation_failed' });
  assert.doesNotMatch(JSON.stringify([...badProvider.calls.logs, ...badSigner.calls.logs]), /PRIVATE_SENTINEL/);
});

test('shutdown aborts pending network work and never publishes a token', async t => {
  const f = await fixture(t, { identify: (_code, _state, signal) => new Promise((_resolve, reject) => {
    signal.addEventListener('abort', () => reject(new Error('aborted')), { once: true });
  }) });
  const a = await f.start(); const b = await f.open(a);
  const callback = f.callback(b); await waitFor(() => f.calls.identify.length === 1);
  f.application.close();
  assert.equal((await callback).status, 503);
  assert.equal(f.calls.mint.length, 0);
  assert.equal((await f.post('start')).status, 503);
});

test('malformed and oversized JSON rejected; legacy guest schema and health remain', async t => {
  const f = await fixture(t, { disabled: true });
  assert.equal((await f.post('start')).status, 503);
  const health = await (await fetch(f.base + '/health')).json();
  assert.equal(health.naverEnabled, false);
  const guest = await (await fetch(f.base + '/login-token/guest', { method: 'POST' })).json();
  assert.match(guest.uid, /^guest_/);
  assert.equal(guest.token, 'synthetic-custom-token');
  assert.deepEqual(Object.keys(guest).sort(), ['token', 'uid']);
  for (const [body, expected] of [['{', 400], [JSON.stringify({ value: 'x'.repeat(17 * 1024) }), 413]]) {
    const response = await fetch(f.base + '/auth/naver/start', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
    assert.equal(response.status, expected);
    assert.doesNotMatch(await response.text(), /SyntaxError|PRIVATE/);
  }
});
