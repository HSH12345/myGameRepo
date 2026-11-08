'use strict';
const fs = require('node:fs');
const admin = require('firebase-admin');
const { loadLocalEnvironment, readConfig } = require('./config');
const { createApplication } = require('./app');

// Script-relative .env works regardless of ServerManager's current directory.
// Existing process environment takes precedence.
loadLocalEnvironment();
const config = readConfig();
for (const stream of [process.stdout, process.stderr]) {
  stream.on('error', error => { if (error.code !== 'EPIPE') throw error; });
}
function log(level, category, message, data) {
  const record = { timestamp: new Date().toISOString(), level, category, message, processId: process.pid };
  if (data !== undefined) record.data = data;
  process.stdout.write(`@rpglog ${JSON.stringify(record)}\n`);
}
let serviceAccount;
try { serviceAccount = JSON.parse(fs.readFileSync(config.serviceAccountPath, 'utf8')); }
catch { throw new Error('Could not read Firebase service-account configuration.'); }
if (!serviceAccount.project_id || (config.firebaseProjectId && config.firebaseProjectId !== serviceAccount.project_id)) {
  throw new Error('Firebase project configuration does not match the service account.');
}
admin.initializeApp({ credential: admin.credential.cert(serviceAccount), projectId: serviceAccount.project_id });
const application = createApplication({ config, auth: admin.auth(), log, shutdown: () => shutdown('manager-http') });
const server = application.app.listen(config.port, config.host, () => {
  log('Info', 'Lifecycle', 'FirebaseAdmin web server is ready.', {
    host: config.host, port: server.address().port, naverEnabled: Boolean(config.naver)
  });
});
server.on('error', () => {
  log('Critical', 'Lifecycle', 'FirebaseAdmin listener failed.');
  application.close();
  process.exit(1);
});
server.requestTimeout = 15000;
server.headersTimeout = 10000;
let isShuttingDown = false;
function shutdown(signal) {
  if (isShuttingDown) return;
  isShuttingDown = true;
  application.close();
  log('Info', 'Lifecycle', 'FirebaseAdmin shutdown requested.', { signal });
  server.close(() => {
    log('Info', 'Lifecycle', 'FirebaseAdmin shutdown completed.');
    process.exit(0);
  });
  server.closeIdleConnections();
  setTimeout(() => process.exit(1), 5000).unref();
}
process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
process.stdin.setEncoding('utf8');
process.stdin.on('data', data => {
  for (const rawCommand of data.split(/\r?\n/)) {
    const command = rawCommand.trim().toLowerCase();
    if (!command) continue;
    if (['-shutdown', 'shutdown', 'exit'].includes(command)) shutdown('stdin');
    else if (command === 'help') log('Info', 'Command', 'Commands: help, status, ping, shutdown (-shutdown), exit');
    else if (command === 'status') log('Info', 'Command', `FirebaseAdmin status: RUNNING, port=${server.address()?.port}`);
    else if (command === 'ping') log('Info', 'Command', 'pong');
    else log('Warning', 'Command', 'Unknown command. Type help.');
  }
});
process.on('uncaughtException', () => {
  log('Critical', 'Runtime', 'Uncaught exception.');
  process.exit(1);
});
