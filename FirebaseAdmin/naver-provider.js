'use strict';
const { createHash } = require('node:crypto');

class AuthError extends Error {
  constructor(code, status = 502) { super(code); this.code = code; this.status = status; }
}
async function readJson(response) {
  if (!response.ok) { await response.body?.cancel(); throw new AuthError('naver_unavailable'); }
  if (!response.body) throw new AuthError('naver_invalid_response');
  const reader = response.body.getReader();
  const chunks = [];
  let bytes = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      bytes += value.length;
      if (bytes > 16 * 1024) throw new AuthError('naver_invalid_response');
      chunks.push(Buffer.from(value));
    }
    try { return JSON.parse(Buffer.concat(chunks).toString('utf8')); }
    catch { throw new AuthError('naver_invalid_response'); }
  } finally { await reader.cancel().catch(() => {}); reader.releaseLock(); }
}
function createNaverProvider(config, fetchImpl = globalThis.fetch) {
  return async function identify(code, state, signal) {
    try {
      const token = await readJson(await fetchImpl('https://nid.naver.com/oauth2.0/token', {
        method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded', Accept: 'application/json' },
        body: new URLSearchParams({ grant_type: 'authorization_code', client_id: config.clientId,
          client_secret: config.clientSecret, code, state }), redirect: 'error', signal
      }));
      if (token.error || typeof token.access_token !== 'string' || !token.access_token || token.access_token.length > 8192 ||
          /[\r\n]/.test(token.access_token) || String(token.token_type).toLowerCase() !== 'bearer') throw new AuthError('naver_token_rejected');
      const profile = await readJson(await fetchImpl('https://openapi.naver.com/v1/nid/me', {
        headers: { Authorization: `Bearer ${token.access_token}`, Accept: 'application/json' }, redirect: 'error', signal
      }));
      if (profile.resultcode !== '00' || typeof profile.response?.id !== 'string' ||
          !/^[A-Za-z0-9_+/=-]{1,64}$/.test(profile.response.id)) throw new AuthError('naver_profile_rejected');
      return profile.response.id;
    } catch (error) {
      if (error instanceof AuthError) throw error;
      throw new AuthError(signal.aborted ? 'auth_timeout' : 'naver_unavailable', signal.aborted ? 504 : 502);
    }
  };
}
// Versioned deterministic namespace. Use the same Naver app on both platforms.
function firebaseUid(clientId, subject) {
  return 'naver_v1_' + createHash('sha256').update(clientId).update('\0').update(subject).digest('hex');
}
module.exports = { AuthError, createNaverProvider, firebaseUid };
