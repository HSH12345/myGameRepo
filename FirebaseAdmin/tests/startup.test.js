'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { generateKeyPairSync, verify } = require('node:crypto');
const { spawn } = require('node:child_process');
const { once } = require('node:events');

test('entry point boots outside project cwd, signs guest token with test key, and shuts down gracefully', async t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'rpg-startup-test-'));
  const keys = generateKeyPairSync('rsa', { modulusLength: 2048 });
  const keyFile = path.join(directory, 'test-service-account.json');
  fs.writeFileSync(keyFile, JSON.stringify({ type: 'service_account', project_id: 'rpg-test-project',
    private_key: keys.privateKey.export({ format: 'pem', type: 'pkcs8' }),
    client_email: 'test@rpg-test-project.iam.gserviceaccount.com' }));
  const environment = { ...process.env, PORT: '0', HOST: '127.0.0.1', FIREBASE_SERVICE_ACCOUNT: keyFile,
      FIREBASE_PROJECT_ID: 'rpg-test-project', NAVER_CLIENT_ID: 'test-client', NAVER_CLIENT_SECRET: 'test-secret',
      NAVER_REDIRECT_URI: 'http://localhost:3000/auth/naver/callback' };
  // The real app is not another node:test worker: retain its normal log stream.
  delete environment.NODE_TEST_CONTEXT;
  const child = spawn(process.execPath, [path.resolve(__dirname, '../index.js')], {
    cwd: os.tmpdir(), windowsHide: true, env: environment, stdio: ['pipe', 'pipe', 'pipe']
  });
  const exited = once(child, 'exit');
  let output = ''; let errors = '';
  child.stderr.on('data', chunk => { errors += chunk.toString(); });
  t.after(async () => {
    if (child.exitCode === null) child.kill();
    await exited;
    assert.ok(path.resolve(directory).startsWith(path.resolve(os.tmpdir()) + path.sep));
    fs.rmSync(directory, { recursive: true, force: true });
  });
  const ready = new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('Entry point did not become ready.')), 5000);
    timeout.unref();
    child.stdout.on('data', chunk => {
      output += chunk.toString();
      for (const line of output.split('\n')) {
        if (!line.startsWith('@rpglog ')) continue;
        try {
          const record = JSON.parse(line.slice(8));
          if (record.message === 'FirebaseAdmin web server is ready.') { clearTimeout(timeout); resolve(record.data); }
        } catch { /* Wait for the rest of a split log line. */ }
      }
    });
    child.once('exit', () => { clearTimeout(timeout); reject(new Error('Entry point exited before readiness.')); });
  });
  const { port, naverEnabled } = await ready;
  assert.equal(naverEnabled, true);
  const base = `http://127.0.0.1:${port}`;
  assert.equal((await (await fetch(base + '/health')).json()).status, 'ok');
  const response = await fetch(base + '/login-token/guest', { method: 'POST' });
  assert.equal(response.status, 200);
  const { uid, token } = await response.json();
  const parts = token.split('.');
  assert.equal(parts.length, 3);
  assert.equal(verify('RSA-SHA256', Buffer.from(parts[0] + '.' + parts[1]), keys.publicKey, Buffer.from(parts[2], 'base64url')), true);
  const claims = JSON.parse(Buffer.from(parts[1], 'base64url'));
  assert.equal(claims.uid, uid);
  assert.match(uid, /^guest_/);
  assert.equal(claims.iss, 'test@rpg-test-project.iam.gserviceaccount.com');
  const start = await fetch(base + '/auth/naver/start', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}' });
  assert.equal(start.status, 201);
  const attempt = await start.json();
  child.stdin.write('-shutdown\n');
  const [code] = await exited;
  assert.equal(code, 0);
  for (const secret of [token, attempt.exchangeSecret, 'test-secret']) {
    assert.equal(output.includes(secret), false);
    assert.equal(errors.includes(secret), false);
  }
});
