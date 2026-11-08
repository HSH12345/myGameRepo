'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { readConfig } = require('../config');
const { createNaverProvider } = require('../naver-provider');
const environment = { FIREBASE_SERVICE_ACCOUNT: 'unused.json', NAVER_CLIENT_ID: 'test-client',
  NAVER_CLIENT_SECRET: 'test-secret', NAVER_REDIRECT_URI: 'http://localhost:3000/auth/naver/callback' };

test('configuration permits explicit loopback HTTP/HTTPS, disables absent Naver, rejects partial/unsafe values', () => {
  assert.equal(readConfig(environment).host, '127.0.0.1');
  assert.equal(readConfig({ FIREBASE_SERVICE_ACCOUNT: 'unused.json' }).naver, null);
  for (const url of ['https://auth.example/auth/naver/callback', 'http://127.0.0.1:3000/auth/naver/callback', 'http://[::1]:3000/auth/naver/callback']) {
    assert.equal(readConfig({ ...environment, NAVER_REDIRECT_URI: url }).naver.redirectUri, url);
  }
  for (const url of ['http://auth.example/auth/naver/callback', 'http://localhost.attacker.example/auth/naver/callback',
    'https://user:password@auth.example/auth/naver/callback', 'https://auth.example/other',
    'https://auth.example/auth/naver/callback?token=hidden', 'https://auth.example/auth/naver/callback#fragment']) {
    assert.throws(() => readConfig({ ...environment, NAVER_REDIRECT_URI: url }), /NAVER_REDIRECT_URI/);
  }
  assert.throws(() => readConfig({ ...environment, NAVER_CLIENT_SECRET: '' }), /NAVER_CLIENT_SECRET/);
  assert.throws(() => readConfig({ ...environment, PORT: 'not-a-port' }), /PORT/);
  assert.throws(() => readConfig({ ...environment, FIREBASE_SERVICE_ACCOUNT: '' }), /FIREBASE_SERVICE_ACCOUNT/);
});

test('env file loader preserves process overrides and is independent of working directory', t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'rpg-env-test-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const envFile = path.join(directory, '.env');
  fs.writeFileSync(envFile, 'RPG_TEST_EXISTING=file-value\nRPG_TEST_NEW=new-value\n');
  const script = `require(${JSON.stringify(require.resolve('../config'))}).loadLocalEnvironment(${JSON.stringify(envFile)}); if(process.env.RPG_TEST_EXISTING!=='process-value'||process.env.RPG_TEST_NEW!=='new-value')process.exit(1);`;
  const child = spawnSync(process.execPath, ['-e', script], { cwd: os.tmpdir(), env: { ...process.env, RPG_TEST_EXISTING: 'process-value' } });
  assert.equal(child.status, 0);
});

test('provider uses HTTPS POST body for client secret/code and Authorization header for access token', async () => {
  const calls = [];
  const provider = createNaverProvider(readConfig(environment).naver, async (url, options) => {
    calls.push({ url, options });
    return Response.json(calls.length === 1 ? { access_token: 'synthetic-access-token', token_type: 'bearer' } :
      { resultcode: '00', response: { id: 'test-naver-subject', email: 'ignored@example.com' } });
  });
  const signal = new AbortController().signal;
  assert.equal(await provider('test-code', 'test-state', signal), 'test-naver-subject');
  assert.equal(calls[0].url, 'https://nid.naver.com/oauth2.0/token');
  assert.equal(calls[0].options.method, 'POST');
  assert.equal(calls[0].options.body.get('client_secret'), 'test-secret');
  assert.equal(calls[0].options.body.get('grant_type'), 'authorization_code');
  assert.equal(calls[1].options.headers.Authorization, 'Bearer synthetic-access-token');
  assert.equal(calls[0].options.redirect, 'error');
  assert.equal(calls[1].options.signal, signal);
  assert.doesNotMatch(calls[0].url + calls[1].url, /test-secret|test-code|synthetic-access-token/);
});

test('provider rejects HTTP errors, token errors, malformed/oversized JSON and invalid profiles', async () => {
  const token = { access_token: 'synthetic-access-token', token_type: 'bearer' };
  const cases = [
    [() => new Response('private-provider-error', { status: 401 })],
    [() => Response.json({ error: 'invalid_request', error_description: 'private-provider-error' })],
    [() => Response.json({ access_token: 'bad\r\ntoken', token_type: 'bearer' })],
    [() => new Response('not-json')],
    [() => new Response('x'.repeat(17 * 1024))],
    [() => Response.json(token), () => Response.json({ resultcode: '024', response: { id: 'test-subject' } })],
    [() => Response.json(token), () => Response.json({ resultcode: '00', response: { id: '' } })],
    [() => Response.json(token), () => Response.json({ resultcode: '00', response: { id: 'x'.repeat(65) } })]
  ];
  for (const responses of cases) {
    const provider = createNaverProvider(readConfig(environment).naver, async () => responses.shift()());
    await assert.rejects(provider('test-code', 'test-state', new AbortController().signal), error =>
      error.code.startsWith('naver_') && !error.message.includes('private-provider-error'));
  }
});

test('aborted/failed provider calls produce bounded safe error codes', async () => {
  const controller = new AbortController(); controller.abort();
  const provider = createNaverProvider(readConfig(environment).naver, async () => { throw new Error('PRIVATE_SENTINEL'); });
  await assert.rejects(provider('test-code', 'test-state', controller.signal), { code: 'auth_timeout' });
  await assert.rejects(provider('test-code', 'test-state', new AbortController().signal), { code: 'naver_unavailable' });
});
