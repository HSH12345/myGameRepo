'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { createFirebaseVerifier } = require('../firebase-verifier');
const uid = 'naver_v1_' + 'a'.repeat(64);
const claims = { uid, rpg_provider: 'naver', firebase: { sign_in_provider: 'custom' } };
function response() { return { statusCode: 200, set() { return this; }, status(code) { this.statusCode = code; return this; }, json(body) { this.body = body; return this; } }; }
function request(idToken = 'synthetic-token', remoteAddress = '127.0.0.1') { return { body: { idToken }, socket: { remoteAddress } }; }

test('verifier returns only verified UID and requests revocation/disabled checks', async () => {
  let checked;
  const verifier = createFirebaseVerifier({ auth: { async verifyIdToken(token, checkRevoked) { checked = [token, checkRevoked]; return claims; } } });
  const res = response(); await verifier.handle(request(), res); verifier.close();
  assert.equal(res.statusCode, 200);
  assert.deepEqual(res.body, { uid });
  assert.deepEqual(checked, ['synthetic-token', true]);
});
test('verifier rejects remote callers, invalid bodies and non-Naver Firebase accounts', async () => {
  const verifier = createFirebaseVerifier({ auth: { async verifyIdToken() { return { ...claims, rpg_provider: 'other' }; } } });
  for (const [req, status] of [[request('token', '192.0.2.1'), 403], [request(''), 400], [request('x'.repeat(8193)), 400], [request(), 401]]) {
    const res = response(); await verifier.handle(req, res); assert.equal(res.statusCode, status);
  }
  verifier.close();
});
test('verifier redacts SDK failures and distinguishes invalid credentials from service outage', async () => {
  for (const [code, status] of [['auth/id-token-revoked', 401], ['auth/id-token-expired', 401], ['auth/user-disabled', 401], ['auth/internal-error', 503]]) {
    const logs = [];
    const verifier = createFirebaseVerifier({ auth: { async verifyIdToken() { throw Object.assign(new Error('PRIVATE_SENTINEL'), { code }); } }, log: (...args) => logs.push(args) });
    const res = response(); await verifier.handle(request(), res); verifier.close();
    assert.equal(res.statusCode, status);
    assert.doesNotMatch(JSON.stringify([res.body, logs]), /PRIVATE_SENTINEL|synthetic-token/);
  }
});
test('timed-out verifier keeps capacity until the SDK settles; shutdown rejects new work', async () => {
  let resolve;
  const pending = new Promise(done => { resolve = done; });
  const verifier = createFirebaseVerifier({ auth: { verifyIdToken: () => pending }, timeoutMs: 20, maxOperations: 1 });
  const first = response(); await verifier.handle(request(), first); assert.equal(first.statusCode, 503);
  const second = response(); await verifier.handle(request(), second); assert.equal(second.statusCode, 503);
  resolve(claims); await new Promise(done => setImmediate(done)); verifier.close();
  const third = response(); await verifier.handle(request(), third); assert.equal(third.statusCode, 503);
});
