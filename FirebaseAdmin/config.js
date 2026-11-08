'use strict';
const path = require('node:path');

function loadLocalEnvironment(file = path.join(__dirname, '.env')) {
  try { process.loadEnvFile(file); }
  catch (error) { if (error.code !== 'ENOENT') throw new Error('Could not load the local .env file.'); }
}

function readConfig(env = process.env) {
  if (env.FIREBASE_AUTH_EMULATOR_HOST) throw new Error('Firebase Auth emulator is not supported by this login verifier.');
  const port = Number(env.PORT || 3000);
  if (!Number.isInteger(port) || port < 0 || port > 65535) throw new Error('PORT is invalid.');
  if (!env.FIREBASE_SERVICE_ACCOUNT) throw new Error('FIREBASE_SERVICE_ACCOUNT is required.');
  const names = ['NAVER_CLIENT_ID', 'NAVER_CLIENT_SECRET', 'NAVER_REDIRECT_URI'];
  let naver = null;
  if (names.some(name => Boolean(env[name]))) {
    const missing = names.filter(name => !env[name]);
    if (missing.length) throw new Error(`Missing Naver configuration: ${missing.join(', ')}.`);
    if (!/^[A-Za-z0-9_-]{1,128}$/.test(env.NAVER_CLIENT_ID) || !/^[\x21-\x7e]{1,256}$/.test(env.NAVER_CLIENT_SECRET)) {
      throw new Error('Naver client configuration has an invalid format.');
    }
    let redirect;
    try { redirect = new URL(env.NAVER_REDIRECT_URI); }
    catch { throw new Error('NAVER_REDIRECT_URI is invalid.'); }
    const loopback = ['localhost', '127.0.0.1', '[::1]'].includes(redirect.hostname);
    if ((redirect.protocol !== 'https:' && !(redirect.protocol === 'http:' && loopback)) || redirect.username ||
        redirect.password || redirect.search || redirect.hash || redirect.pathname !== '/auth/naver/callback') {
      throw new Error('NAVER_REDIRECT_URI must use HTTPS (or HTTP loopback) and /auth/naver/callback without a query or fragment.');
    }
    naver = { clientId: env.NAVER_CLIENT_ID, clientSecret: env.NAVER_CLIENT_SECRET, redirectUri: redirect.href,
      origin: redirect.origin, secureCookie: redirect.protocol === 'https:', attemptTtlMs: 300_000,
      readyTtlMs: 60_000, operationTimeoutMs: 10_000, maxAttempts: 256, maxOperations: 8, startsPerMinute: 10 };
  }
  return { port, host: env.HOST || '127.0.0.1', serviceAccountPath: path.resolve(__dirname, env.FIREBASE_SERVICE_ACCOUNT),
    firebaseProjectId: env.FIREBASE_PROJECT_ID || null, naver };
}
module.exports = { loadLocalEnvironment, readConfig };
