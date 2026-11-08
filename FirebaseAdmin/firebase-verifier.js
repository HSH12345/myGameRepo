'use strict';

const rejected = new Set(['auth/argument-error', 'auth/invalid-id-token', 'auth/id-token-expired',
  'auth/id-token-revoked', 'auth/user-disabled', 'auth/user-not-found']);

function createFirebaseVerifier({ auth, log = () => {}, timeoutMs = 8000, maxOperations = 8 }) {
  let inFlight = 0;
  let closed = false;
  const cancellations = new Set();
  async function handle(req, res) {
    if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress)) {
      return res.status(403).json({ error: 'loopback_only' });
    }
    res.set({ 'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer' });
    const token = req.body?.idToken;
    if (typeof token !== 'string' || !token || Buffer.byteLength(token, 'utf8') > 8192) return res.status(400).json({ error: 'invalid_request' });
    if (closed || inFlight >= maxOperations) return res.status(503).json({ error: 'auth_busy' });
    ++inFlight;
    let cancel;
    let timer;
    const deadline = new Promise((_resolve, reject) => {
      cancel = () => reject(Object.assign(new Error('unavailable'), { code: 'local/unavailable' }));
      cancellations.add(cancel);
      timer = setTimeout(cancel, timeoutMs);
    });
    // verifyIdToken(checkRevoked=true) checks signature/project/expiry plus the
    // user's revocation/disabled state. Keep capacity reserved on a slow SDK.
    const verification = Promise.resolve().then(() => auth.verifyIdToken(token, true)).finally(() => {
      --inFlight; clearTimeout(timer); cancellations.delete(cancel);
    });
    try {
      const verified = await Promise.race([verification, deadline]);
      if (closed) return res.status(503).json({ error: 'auth_unavailable' });
      if (!/^naver_v1_[a-f0-9]{64}$/.test(verified.uid) || verified.rpg_provider !== 'naver' || verified.firebase?.sign_in_provider !== 'custom') {
        return res.status(401).json({ error: 'auth_rejected' });
      }
      res.json({ uid: verified.uid });
    } catch (error) {
      const status = rejected.has(error.code) ? 401 : 503;
      log('Warning', 'Auth', 'Firebase ID token verification did not complete.', { status });
      res.status(status).json({ error: status === 401 ? 'auth_rejected' : 'auth_unavailable' });
    }
  }
  return { handle, close() { closed = true; for (const cancel of cancellations) cancel(); } };
}
module.exports = { createFirebaseVerifier };
