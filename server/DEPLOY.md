# Deploying ModelBench Backend to Production

## 1. Prerequisites
- A Linux VPS / cloud VM with Node.js 20+ and `pnpm` installed, or a PaaS (Render, Railway, Fly.io).
- A Supabase project (auth + Postgres).
- A Whop account with products for Hobby ($7), Scale ($25), Teams ($149).
- An OpenRouter API key (for cloud AI credits).
- A public HTTPS URL (the "site URL").

## 2. Supabase setup
1. Create a Supabase project.
2. In the SQL editor, run `src/lib/schema.sql` (creates profiles, credits, entitlements, RLS policies).
3. Get from Project Settings → API:
   - `SUPABASE_URL` (Project URL)
   - `SUPABASE_ANON_KEY` (anon/public key)
   - `SUPABASE_SERVICE_ROLE_KEY` (service role — KEEP SECRET)

## 3. Whop setup
1. Create 3 products in Whop: Hobby ($7/mo), Scale ($25/mo), Teams ($149/mo).
2. Copy each plan ID into `WHOP_PLAN_HOBBY`, `WHOP_PLAN_SCALE`, `WHOP_PLAN_TEAMS`.
3. Create a webhook in Whop pointing to `https://YOUR_SITE/billing/webhook`.
4. Copy the webhook signing secret into `WHOP_WEBHOOK_SECRET`.

## 4. Environment
On the server:
```bash
cd server
cp .env.example .env
# Edit .env — fill in ALL values from steps 2-3 and your OpenRouter key.
# Set SITE_URL to your public HTTPS URL.
# Set TRUST_PROXY=1 if behind a reverse proxy.
```

## 5. Install & run (production)
```bash
cd server
pnpm install --frozen-lockfile
pnpm start
```
The server listens on `PORT` (default 3000).

### Run under a process manager (recommended)
```bash
# PM2
pnpm add -g pm2
pm2 start src/index.js --name modelbench-server
pm2 save
pm2 startup
```

### Reverse proxy (Nginx example)
```nginx
server {
    listen 443 ssl;
    server_name your-site.example.com;
    ssl_certificate     /path/cert.pem;
    ssl_certificate_key /path/key.pem;
    location / {
        proxy_pass http://127.0.0.1:3000;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $remote_addr;
    }
}
```

## 6. Verify
```bash
curl https://your-site.example.com/health
# Should return: {"ok":true,"service":"modelbench-server",...}
# "mode" should be "production" (not "mock"), ai: "OpenRouter", billing: "Whop"
```

## 7. Hardcode site URL in the desktop app
Once deployed, tell me the public URL and I'll replace the default `http://localhost:3000`
in `app/core/AccountManager.h` and the CORS defaults in the server.

## 8. PaaS one-click notes
- **Render**: New Web Service → Node → build `pnpm install`, start `pnpm start`, add all env vars.
- **Railway**: New Project → deploy from GitHub → set env vars → railway will detect Node.
- **Fly.io**: `fly launch`, set secrets via `fly secrets set SUPABASE_URL=...`.

## Security reminders
- NEVER commit `.env` (it's gitignored).
- Keep `SUPABASE_SERVICE_ROLE_KEY` and `WHOP_WEBHOOK_SECRET` secret.
- `ALLOW_UNSIGNED_WEBHOOKS` and `RATE_LIMIT_DISABLED` must NOT be enabled in production.
