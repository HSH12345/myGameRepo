'use strict';
const express = require('express');
const { randomUUID } = require('node:crypto');
const { createNaverAuth } = require('./naver-auth');
const { createNaverProvider } = require('./naver-provider');
const { createFirebaseVerifier } = require('./firebase-verifier');

function createApplication({ config, auth, log = () => {}, shutdown = () => {}, identify, now }) {
  const app = express();
  app.disable('x-powered-by');
  app.set('query parser', 'simple');
  app.use((_req, res, next) => { res.set('Cache-Control', 'no-store'); next(); });
  app.use(express.json({ limit: '16kb', strict: true }));
  const verifier = createFirebaseVerifier({ auth, log });
  app.post('/auth/firebase/verify', verifier.handle);
  const naver = config.naver ? createNaverAuth({ config: config.naver,
    identify: identify || createNaverProvider(config.naver),
    mintCustomToken: (uid, claims) => auth.createCustomToken(uid, claims), log, now }) : null;
  if (naver) app.use('/auth/naver', naver.router);
  else app.use('/auth/naver', (_req, res) => res.status(503).json({ error: 'naver_not_configured' }));
  app.post('/login-token/guest', async (_req, res) => {
    const uid = 'guest_' + randomUUID();
    try {
      const token = await auth.createCustomToken(uid);
      log('Info', 'Auth', 'Firebase guest custom token created.', { uid });
      res.json({ uid, token });
    } catch {
      log('Error', 'Auth', 'Firebase guest custom token creation failed.');
      res.status(500).json({ error: 'token_creation_failed' });
    }
  });
  app.get('/health', (_req, res) => res.json({ status: 'ok', service: 'FirebaseAdmin', naverEnabled: Boolean(naver) }));
  app.post('/admin/shutdown', (req, res) => {
    if (!['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress)) return res.status(403).json({ error: 'loopback_only' });
    res.status(202).json({ status: 'shutting_down' });
    setImmediate(shutdown);
  });
  app.use((_req, res) => res.status(404).json({ error: 'not_found' }));
  app.use((error, _req, res, _next) => {
    const status = error.type === 'entity.too.large' ? 413 : 400;
    res.status(status).json({ error: status === 413 ? 'request_too_large' : 'invalid_request' });
  });
  return { app, close: () => { naver?.close(); verifier.close(); } };
}
module.exports = { createApplication };
