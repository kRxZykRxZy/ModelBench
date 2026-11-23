# ModelBench Backend API

Node.js API backend for the ModelBench desktop app. Provides Supabase-based auth, user profiles, entitlements, and Whop billing webhooks.

## Setup

```bash
pnpm install
cp .env.example .env
# Fill in .env with your Supabase and Whop credentials
pnpm start
```

## Environment variables

See `.env.example`.

## Supabase schema

Run this in your Supabase SQL editor:

```sql
create table if not exists profiles (
  id uuid primary key references auth.users(id) on delete cascade,
  email text,
  display_name text,
  tier text default 'free',
  updated_at timestamptz default now()
);

alter table profiles enable row level security;
create policy "users read own profile" on profiles for select using (auth.uid() = id);
create policy "users update own profile" on profiles for update using (auth.uid() = id);
```

## Endpoints

- `GET /health`
- `POST /auth/signup` { email, password }
- `POST /auth/login` { email, password }
- `POST /auth/logout`
- `GET /auth/me` (Bearer token)
- `GET /user/profile` (Bearer token)
- `PUT /user/profile` { display_name } (Bearer token)
- `GET /user/entitlements` (Bearer token)
- `POST /billing/checkout` { planId, userId }
- `GET /billing/entitlements?userId=...`
- `POST /billing/webhook` (Whop webhook)
