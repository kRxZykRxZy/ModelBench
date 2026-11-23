'use strict';

const { spawn } = require('child_process');
const http = require('http');
const path = require('path');

const repoRoot = path.resolve(__dirname, '..', '..');
const serverEntry = path.join(repoRoot, 'server', 'src', 'index.js');
const PORT = 3099;
const BASE = `http://127.0.0.1:${PORT}`;

function request(method, urlPath, body, headers) {
  return new Promise((resolve, reject) => {
    const data = body ? JSON.stringify(body) : null;
    const opts = {
      method,
      hostname: '127.0.0.1',
      port: PORT,
      path: urlPath,
      headers: Object.assign(
        { 'Content-Type': 'application/json' },
        headers || {}
      )
    };
    if (data) opts.headers['Content-Length'] = Buffer.byteLength(data);
    const req = http.request(opts, (res) => {
      let chunks = '';
      res.on('data', (c) => (chunks += c));
      res.on('end', () => {
        let json = null;
        try { json = chunks ? JSON.parse(chunks) : null; } catch (e) { json = { _raw: chunks }; }
        resolve({ status: res.statusCode, body: json });
      });
    });
    req.on('error', reject);
    if (data) req.write(data);
    req.end();
  });
}

function waitForHealth(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  return new Promise((resolve, reject) => {
    const attempt = () => {
      request('GET', '/health').then((r) => {
        if (r.status === 200) resolve();
        else if (Date.now() > deadline) reject(new Error('health not ready'));
        else setTimeout(attempt, 250);
      }).catch(() => {
        if (Date.now() > deadline) reject(new Error('health not ready'));
        else setTimeout(attempt, 250);
      });
    };
    attempt();
  });
}

(async () => {
  const env = Object.assign({}, process.env, {
    PORT: String(PORT),
    NODE_ENV: 'test'
  });
  const server = spawn('node', [serverEntry], { cwd: repoRoot, env, stdio: ['ignore', 'pipe', 'pipe'] });
  let serverError = '';
  server.stderr.on('data', (c) => { serverError += c; });

  try {
    await waitForHealth(15000);

    const signup = await request('POST', '/auth/signup', {
      email: 'credits-test@example.com',
      password: 'password123'
    });
    if (signup.status !== 200 || !signup.body.session) {
      throw new Error('signup failed: ' + JSON.stringify(signup.body));
    }
    const token = signup.body.session.access_token;
    const auth = { Authorization: 'Bearer ' + token };

    const before = await request('GET', '/user/credits', null, auth);
    if (before.status !== 200) throw new Error('credits fetch failed');
    const balanceBefore = Number(before.body.balance ?? before.body.credits_balance ?? 0);

    const chat = await request('POST', '/ai/chat', {
      model: 'deepseek/deepseek-v4.1-flash',
      messages: [{ role: 'user', content: 'hello credits' }],
      stream: false
    }, auth);
    if (chat.status !== 200) {
      throw new Error('chat failed: ' + JSON.stringify(chat.body));
    }
    const charged = Number(chat.body.modelbench && chat.body.modelbench.credits_charged);
    const balanceAfter = Number(chat.body.modelbench && chat.body.modelbench.balance);

    if (!(charged > 0)) throw new Error('expected credits to be charged');
    if (!(balanceAfter < balanceBefore)) throw new Error('balance did not decrease');

    const after = await request('GET', '/user/credits', null, auth);
    if (after.status !== 200) throw new Error('credits fetch after failed');
    const balanceCheck = Number(after.body.balance ?? after.body.credits_balance ?? 0);
    if (Math.abs(balanceCheck - balanceAfter) > 1e-9) {
      throw new Error('persisted balance mismatch');
    }

    console.log('PASS: credits deduction flow');
    server.kill();
    process.exit(0);
  } catch (err) {
    console.error('FAIL:', err && err.message ? err.message : err);
    if (serverError) console.error('SERVER STDERR:\n' + serverError);
    server.kill();
    process.exit(1);
  }
})();