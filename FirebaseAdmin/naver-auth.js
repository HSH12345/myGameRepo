'use strict';
const express = require('express');
const { randomBytes, createHash, timingSafeEqual } = require('node:crypto');
const { AuthError, firebaseUid } = require('./naver-provider');
const credentialPattern = /^[A-Za-z0-9_-]{43}$/;
const digest = value => createHash('sha256').update(value).digest('hex');
const random = () => randomBytes(32).toString('base64url');

function createNaverAuth({ config, identify, mintCustomToken, log = () => {}, now = Date.now }) {
  const router = express.Router();
  const attempts = new Map();
  const states = new Map();
  const rateLimits = new Map();
  const operations = new Set();
  let inFlight = 0;
  let closed = false;
  function prune() {
    const time = now();
    for (const [id, attempt] of attempts) {
      if (attempt.expiresAt <= time) { states.delete(attempt.stateHash); attempts.delete(id); }
    }
    for (const [ip, limit] of rateLimits) if (limit.until <= time) rateLimits.delete(ip);
  }
  const sweep = setInterval(prune, 30_000);
  sweep.unref();
  router.use((_req, res, next) => {
    res.set({ 'Cache-Control': 'no-store', Pragma: 'no-cache', 'Referrer-Policy': 'no-referrer',
      'X-Content-Type-Options': 'nosniff', 'Cross-Origin-Resource-Policy': 'same-origin',
      'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'" });
    prune();
    if (closed) return res.status(503).json({ error: 'auth_unavailable' });
    next();
  });
  router.use((req, res, next) => {
    if (req.method === 'POST' && req.get('Origin') && req.get('Origin') !== config.origin) return res.status(403).json({ error: 'origin_rejected' });
    next();
  });
  function cookieName(id) { return `rpg_naver_${id}`; }
  const cookieOptions = { httpOnly: true, secure: config.secureCookie, sameSite: 'lax', path: '/auth/naver/callback' };
  function clearCookie(res, id) { res.clearCookie(cookieName(id), cookieOptions); }
  function browserMessage(res, status, message) {
    return res.status(status).type('html').send(`<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>RPG 네이버 로그인</title><body style="font-family:sans-serif;padding:3rem;line-height:1.8"><h1>RPG 네이버 로그인</h1><p>${message}</p><p>이 창을 닫고 게임으로 돌아가세요.</p></body></html>`);
  }
  function isCurrent(attempt, phase) {
    return !closed && attempts.get(attempt.id) === attempt && attempt.expiresAt > now() && attempt.phase === phase;
  }
  function fail(attempt, code, phase = 'failed') {
    attempt.phase = phase;
    attempt.error = code;
    states.delete(attempt.stateHash);
    log('Warning', 'Auth', 'Naver login did not complete.', { code });
  }
  function authenticate(req) {
    const id = req.body?.requestId;
    const header = req.get('Authorization') || '';
    const secret = header.startsWith('Bearer ') ? header.slice(7) : '';
    if (typeof id !== 'string' || !credentialPattern.test(id) || !credentialPattern.test(secret)) return null;
    const attempt = attempts.get(id);
    if (!attempt || !timingSafeEqual(Buffer.from(attempt.secretHash, 'hex'), Buffer.from(digest(secret), 'hex'))) return null;
    return attempt;
  }
  function operationAvailable() { return inFlight < config.maxOperations; }
  async function runOperation(task) {
    if (!operationAvailable() || closed) throw new AuthError('auth_busy', 503);
    ++inFlight;
    const controller = new AbortController();
    operations.add(controller);
    const timeout = setTimeout(() => controller.abort(), config.operationTimeoutMs);
    let onAbort;
    // A timed-out signing promise retains its slot until it actually settles.
    const work = Promise.resolve().then(() => task(controller.signal)).finally(() => {
      --inFlight;
      clearTimeout(timeout);
      operations.delete(controller);
    });
    const aborted = new Promise((_resolve, reject) => {
      onAbort = () => reject(new AuthError(closed ? 'auth_unavailable' : 'auth_timeout', closed ? 503 : 504));
      controller.signal.addEventListener('abort', onAbort, { once: true });
    });
    try { return await Promise.race([work, aborted]); }
    finally { controller.signal.removeEventListener('abort', onAbort); }
  }

  router.post('/start', (req, res) => {
    const ip = req.socket.remoteAddress || 'unknown';
    let rate = rateLimits.get(ip);
    if (!rate) {
      if (rateLimits.size >= config.maxAttempts) return res.status(503).json({ error: 'auth_busy' });
      rate = { count: 0, until: now() + 60_000 };
      rateLimits.set(ip, rate);
    }
    if (++rate.count > config.startsPerMinute) {
      res.set('Retry-After', String(Math.max(1, Math.ceil((rate.until - now()) / 1000))));
      return res.status(429).json({ error: 'rate_limited' });
    }
    if (attempts.size >= config.maxAttempts) return res.status(503).json({ error: 'auth_busy' });
    const id = random();
    const exchangeSecret = random();
    const expiresAt = now() + config.attemptTtlMs;
    attempts.set(id, { id, secretHash: digest(exchangeSecret), phase: 'created', expiresAt });
    const login = new URL('/auth/naver/login', config.origin);
    login.searchParams.set('requestId', id);
    res.status(201).json({ requestId: id, exchangeSecret, loginUrl: login.href,
      expiresAt: new Date(expiresAt).toISOString(), pollIntervalMs: 1500 });
  });
  router.get('/login', (req, res) => {
    const id = req.query.requestId;
    const attempt = typeof id === 'string' && credentialPattern.test(id) ? attempts.get(id) : null;
    if (!attempt) return browserMessage(res, 400, '로그인 요청이 없거나 만료되었습니다. 게임에서 다시 시작하세요.');
    if (attempt.phase !== 'created') return browserMessage(res, 409, '이미 시작된 로그인 요청입니다. 게임에서 상태를 확인하세요.');
    const state = random();
    const binding = random();
    attempt.stateHash = digest(state);
    attempt.browserHash = digest(binding);
    attempt.phase = 'authorizing';
    states.set(attempt.stateHash, id);
    res.cookie(cookieName(id), binding, { ...cookieOptions, maxAge: Math.max(1, attempt.expiresAt - now()) });
    const authorization = new URL('https://nid.naver.com/oauth2.0/authorize');
    authorization.search = new URLSearchParams({ response_type: 'code', client_id: config.clientId,
      redirect_uri: config.redirectUri, state }).toString();
    res.redirect(303, authorization.href);
  });
  router.get('/callback', async (req, res) => {
    const state = req.query.state;
    if (typeof state !== 'string' || !credentialPattern.test(state)) return browserMessage(res, 400, '유효하지 않은 로그인 응답입니다.');
    const attempt = attempts.get(states.get(digest(state)));
    if (!attempt || attempt.phase !== 'authorizing') return browserMessage(res, 400, '로그인 요청이 만료되었거나 이미 처리되었습니다.');
    const values = (req.get('Cookie') || '').split(';').map(part => part.trim()).filter(part => part.startsWith(cookieName(attempt.id) + '='));
    const binding = values.length === 1 ? values[0].slice(cookieName(attempt.id).length + 1) : '';
    if (!credentialPattern.test(binding) || digest(binding) !== attempt.browserHash) return browserMessage(res, 403, '로그인을 시작한 브라우저에서 다시 시도하세요.');
    const { code, error } = req.query;
    if (error !== undefined) {
      if (typeof error !== 'string' || code !== undefined) return browserMessage(res, 400, '유효하지 않은 로그인 응답입니다.');
      fail(attempt, error === 'access_denied' ? 'login_cancelled' : 'naver_authorization_failed', error === 'access_denied' ? 'cancelled' : 'failed');
      clearCookie(res, attempt.id);
      return browserMessage(res, 200, '로그인이 취소되었거나 네이버 인증에 실패했습니다.');
    }
    if (typeof code !== 'string' || code.length < 1 || code.length > 2048 || /[^\x21-\x7e]/.test(code)) return browserMessage(res, 400, '유효하지 않은 인증 코드입니다.');
    if (!operationAvailable()) return browserMessage(res, 503, '인증 요청이 많습니다. 잠시 후 이 페이지를 새로고침하세요.');
    // Claim state synchronously before any network await.
    attempt.phase = 'identifying';
    states.delete(attempt.stateHash);
    clearCookie(res, attempt.id);
    try {
      const subject = await runOperation(signal => identify(code, state, signal));
      if (!isCurrent(attempt, 'identifying')) return browserMessage(res, 410, '요청이 취소되었거나 만료되었습니다. 다시 로그인하세요.');
      attempt.uid = firebaseUid(config.clientId, subject);
      attempt.phase = 'ready';
      attempt.expiresAt = Math.min(attempt.expiresAt, now() + config.readyTtlMs);
      return browserMessage(res, 200, '네이버 인증이 완료되었습니다.');
    } catch (error) {
      const code = error instanceof AuthError ? error.code : 'naver_unavailable';
      if (isCurrent(attempt, 'identifying')) fail(attempt, code);
      return browserMessage(res, error instanceof AuthError ? error.status : 502, '인증을 완료하지 못했습니다. 게임에서 다시 로그인하세요.');
    }
  });
  router.post('/status', (req, res) => {
    const attempt = authenticate(req);
    if (!attempt) return res.status(404).json({ error: 'invalid_request' });
    const status = ['created', 'authorizing', 'identifying'].includes(attempt.phase) ? 'pending' : attempt.phase;
    res.json({ status, ...(attempt.error ? { error: attempt.error } : {}), expiresAt: new Date(attempt.expiresAt).toISOString(), pollIntervalMs: 1500 });
  });
  router.post('/cancel', (req, res) => {
    const attempt = authenticate(req);
    if (!attempt) return res.status(404).json({ error: 'invalid_request' });
    if (attempt.phase === 'consumed') return res.status(409).json({ error: 'already_consumed' });
    fail(attempt, 'login_cancelled', 'cancelled');
    res.json({ status: 'cancelled' });
  });
  router.post('/exchange', async (req, res) => {
    const attempt = authenticate(req);
    if (!attempt) return res.status(404).json({ error: 'invalid_request' });
    if (attempt.phase !== 'ready') return res.status(409).json({ error: 'not_ready', status: attempt.phase });
    if (!operationAvailable()) return res.status(503).json({ error: 'auth_busy' });
    attempt.phase = 'exchanging';
    try {
      const token = await runOperation(() => mintCustomToken(attempt.uid, { rpg_provider: 'naver' }));
      if (!isCurrent(attempt, 'exchanging')) return res.status(410).json({ error: 'request_expired_or_cancelled' });
      if (typeof token !== 'string' || !token || token.length > 16 * 1024) throw new AuthError('token_creation_failed');
      // At-most-once delivery. A lost exchange response requires a new login.
      attempt.phase = 'consumed';
      res.json({ requestId: attempt.id, uid: attempt.uid, customToken: token });
      log('Info', 'Auth', 'Naver authentication token exchanged.');
    } catch (error) {
      const code = error instanceof AuthError ? error.code : 'token_creation_failed';
      if (isCurrent(attempt, 'exchanging')) fail(attempt, code);
      res.status(error instanceof AuthError ? error.status : 502).json({ error: code });
    }
  });
  return { router, close() {
    closed = true;
    clearInterval(sweep);
    for (const controller of operations) controller.abort();
    attempts.clear(); states.clear(); rateLimits.clear();
  } };
}
module.exports = { createNaverAuth };
