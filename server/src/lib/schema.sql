-- ============================================================================
-- ModelBench — Supabase schema
-- ============================================================================
-- Run this file in the Supabase SQL editor (or `supabase db push`).
-- Safe to re-run: every object is created with IF NOT EXISTS / CREATE OR REPLACE
-- and policies are dropped before being recreated.
--
-- Tables
--   profiles        one row per auth.users row (tier + cached credit balance)
--   credit_ledger   append-only ledger: every grant/deduction with balance_after
--   usage_events    one row per cloud AI request (tokens + USD cost)
--   entitlements    feature flags derived from the subscription tier
--   subscriptions   mirror of the Whop membership (lifecycle + period)
--   webhook_events  idempotency guard for billing webhooks
--
-- Credit model: 1 credit == 1 USD of OpenRouter usage. Credits are stored as
-- NUMERIC(14,6) so sub-cent requests are accounted for exactly.
-- ============================================================================

create extension if not exists pgcrypto;

-- ----------------------------------------------------------------------------
-- profiles
-- ----------------------------------------------------------------------------
create table if not exists public.profiles (
  id uuid primary key references auth.users(id) on delete cascade,
  email text,
  display_name text,
  tier text not null default 'free'
    check (tier in ('free', 'hobby', 'scale', 'teams')),
  credits_balance numeric(14,6) not null default 0
    check (credits_balance >= 0),
  credits_lifetime_granted numeric(14,6) not null default 0
    check (credits_lifetime_granted >= 0),
  credits_lifetime_used numeric(14,6) not null default 0
    check (credits_lifetime_used >= 0),
  -- Month bucket (YYYY-MM) in which plan credits were last granted. Prevents
  -- duplicate grants when Whop retries or sends several events per purchase.
  credit_period_key text,
  last_credit_at timestamptz,
  whop_user_id text,
  whop_membership_id text,
  subscription_status text,
  current_period_end timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create index if not exists profiles_email_idx on public.profiles (lower(email));
create index if not exists profiles_whop_membership_idx on public.profiles (whop_membership_id);

-- ----------------------------------------------------------------------------
-- credit_ledger — append-only
-- ----------------------------------------------------------------------------
create table if not exists public.credit_ledger (
  id bigint generated always as identity primary key,
  user_id uuid not null references public.profiles(id) on delete cascade,
  delta numeric(14,6) not null,
  kind text not null check (kind in ('grant', 'deduction', 'refund', 'adjustment', 'expiry')),
  reason text,
  balance_after numeric(14,6) not null check (balance_after >= 0),
  model text,
  prompt_tokens integer,
  completion_tokens integer,
  cost_usd numeric(14,8),
  metadata jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now()
);

create index if not exists credit_ledger_user_created_idx
  on public.credit_ledger (user_id, created_at desc);

-- ----------------------------------------------------------------------------
-- usage_events — cloud AI cost tracking
-- ----------------------------------------------------------------------------
create table if not exists public.usage_events (
  id bigint generated always as identity primary key,
  user_id uuid not null references public.profiles(id) on delete cascade,
  model text not null,
  provider text,
  prompt_tokens integer not null default 0 check (prompt_tokens >= 0),
  completion_tokens integer not null default 0 check (completion_tokens >= 0),
  total_tokens integer generated always as (prompt_tokens + completion_tokens) stored,
  cost_usd numeric(14,8) not null default 0 check (cost_usd >= 0),
  credits_charged numeric(14,6) not null default 0 check (credits_charged >= 0),
  request_id text,
  status text not null default 'ok' check (status in ('ok', 'error', 'canceled', 'mock')),
  metadata jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now()
);

create index if not exists usage_events_user_created_idx
  on public.usage_events (user_id, created_at desc);
create index if not exists usage_events_request_idx
  on public.usage_events (request_id);

-- ----------------------------------------------------------------------------
-- entitlements — feature flags per user
-- ----------------------------------------------------------------------------
create table if not exists public.entitlements (
  user_id uuid not null references public.profiles(id) on delete cascade,
  feature text not null,
  enabled boolean not null default true,
  source text not null default 'plan',
  expires_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  primary key (user_id, feature)
);

-- ----------------------------------------------------------------------------
-- subscriptions — Whop membership mirror
-- ----------------------------------------------------------------------------
create table if not exists public.subscriptions (
  id text primary key,
  user_id uuid not null references public.profiles(id) on delete cascade,
  plan_id text,
  tier text not null default 'free'
    check (tier in ('free', 'hobby', 'scale', 'teams')),
  status text not null default 'inactive',
  whop_user_id text,
  current_period_end timestamptz,
  cancel_at_period_end boolean not null default false,
  last_credit_period text,
  metadata jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create index if not exists subscriptions_user_idx on public.subscriptions (user_id);

-- ----------------------------------------------------------------------------
-- webhook_events — idempotency
-- ----------------------------------------------------------------------------
create table if not exists public.webhook_events (
  id text primary key,
  type text,
  payload jsonb not null default '{}'::jsonb,
  processed_at timestamptz not null default now()
);

-- ----------------------------------------------------------------------------
-- updated_at helper
-- ----------------------------------------------------------------------------
create or replace function public.touch_updated_at()
returns trigger
language plpgsql
as $$
begin
  new.updated_at = now();
  return new;
end;
$$;

drop trigger if exists profiles_touch_updated_at on public.profiles;
create trigger profiles_touch_updated_at
  before update on public.profiles
  for each row execute function public.touch_updated_at();

drop trigger if exists subscriptions_touch_updated_at on public.subscriptions;
create trigger subscriptions_touch_updated_at
  before update on public.subscriptions
  for each row execute function public.touch_updated_at();

drop trigger if exists entitlements_touch_updated_at on public.entitlements;
create trigger entitlements_touch_updated_at
  before update on public.entitlements
  for each row execute function public.touch_updated_at();

-- ----------------------------------------------------------------------------
-- Auto-create a profile for every new auth user
-- ----------------------------------------------------------------------------
create or replace function public.handle_new_user()
returns trigger
language plpgsql
security definer
set search_path = public
as $$
begin
  insert into public.profiles (id, email, display_name)
  values (
    new.id,
    new.email,
    coalesce(new.raw_user_meta_data ->> 'display_name', split_part(coalesce(new.email, ''), '@', 1))
  )
  on conflict (id) do nothing;
  return new;
end;
$$;

drop trigger if exists on_auth_user_created on auth.users;
create trigger on_auth_user_created
  after insert on auth.users
  for each row execute function public.handle_new_user();

-- Keep the mirrored email in sync when auth.users changes.
create or replace function public.handle_user_email_update()
returns trigger
language plpgsql
security definer
set search_path = public
as $$
begin
  update public.profiles
     set email = new.email,
         updated_at = now()
   where id = new.id
     and coalesce(email, '') is distinct from coalesce(new.email, '');
  return new;
end;
$$;

drop trigger if exists on_auth_user_email_updated on auth.users;
create trigger on_auth_user_email_updated
  after update of email on auth.users
  for each row execute function public.handle_user_email_update();

-- ----------------------------------------------------------------------------
-- Credit functions (call with the service-role key only)
-- ----------------------------------------------------------------------------

-- Grant credits and append a ledger row. Returns the new balance.
create or replace function public.grant_credits(
  p_user_id uuid,
  p_amount numeric,
  p_reason text default 'grant',
  p_metadata jsonb default '{}'::jsonb
)
returns numeric
language plpgsql
security definer
set search_path = public
as $$
declare
  v_balance numeric(14,6);
begin
  if p_amount is null or p_amount <= 0 then
    raise exception 'amount must be positive' using errcode = '22023';
  end if;

  insert into public.profiles (id) values (p_user_id)
  on conflict (id) do nothing;

  update public.profiles
     set credits_balance = credits_balance + p_amount,
         credits_lifetime_granted = credits_lifetime_granted + p_amount,
         last_credit_at = now(),
         updated_at = now()
   where id = p_user_id
  returning credits_balance into v_balance;

  if v_balance is null then
    raise exception 'profile_not_found' using errcode = 'P0002';
  end if;

  insert into public.credit_ledger
    (user_id, delta, kind, reason, balance_after, metadata)
  values
    (p_user_id, p_amount, 'grant', p_reason, v_balance, coalesce(p_metadata, '{}'::jsonb));

  return v_balance;
end;
$$;

-- Deduct credits atomically. Raises `insufficient_credits` when the balance is
-- too low, so concurrent requests cannot overdraw an account.
create or replace function public.deduct_credits(
  p_user_id uuid,
  p_amount numeric,
  p_reason text default 'deduction',
  p_metadata jsonb default '{}'::jsonb
)
returns numeric
language plpgsql
security definer
set search_path = public
as $$
declare
  v_balance numeric(14,6);
begin
  if p_amount is null or p_amount < 0 then
    raise exception 'amount must not be negative' using errcode = '22023';
  end if;

  select credits_balance into v_balance
    from public.profiles
   where id = p_user_id
   for update;

  if not found then
    raise exception 'profile_not_found' using errcode = 'P0002';
  end if;

  if p_amount = 0 then
    return v_balance;
  end if;

  if v_balance < p_amount then
    raise exception 'insufficient_credits'
      using errcode = 'P0001',
            detail = format('balance=%s required=%s', v_balance, p_amount);
  end if;

  update public.profiles
     set credits_balance = credits_balance - p_amount,
         credits_lifetime_used = credits_lifetime_used + p_amount,
         updated_at = now()
   where id = p_user_id
  returning credits_balance into v_balance;

  insert into public.credit_ledger
    (user_id, delta, kind, reason, balance_after, metadata)
  values
    (p_user_id, -p_amount, 'deduction', p_reason, v_balance, coalesce(p_metadata, '{}'::jsonb));

  return v_balance;
end;
$$;

-- Record a cloud AI request and charge credits in one transaction.
-- Returns {"balance": <new balance>, "charged": <credits>}.
create or replace function public.record_usage(
  p_user_id uuid,
  p_model text,
  p_prompt_tokens integer,
  p_completion_tokens integer,
  p_cost_usd numeric,
  p_credits numeric,
  p_request_id text default null,
  p_provider text default null,
  p_status text default 'ok',
  p_metadata jsonb default '{}'::jsonb
)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
  v_balance numeric(14,6);
  v_credits numeric(14,6) := coalesce(p_credits, 0);
  v_cost numeric(14,8) := coalesce(p_cost_usd, 0);
begin
  if v_credits < 0 then
    raise exception 'credits must not be negative' using errcode = '22023';
  end if;

  select credits_balance into v_balance
    from public.profiles
   where id = p_user_id
   for update;

  if not found then
    raise exception 'profile_not_found' using errcode = 'P0002';
  end if;

  if v_credits > 0 then
    if v_balance < v_credits then
      raise exception 'insufficient_credits'
        using errcode = 'P0001',
              detail = format('balance=%s required=%s', v_balance, v_credits);
    end if;

    update public.profiles
       set credits_balance = credits_balance - v_credits,
           credits_lifetime_used = credits_lifetime_used + v_credits,
           updated_at = now()
     where id = p_user_id
    returning credits_balance into v_balance;

    insert into public.credit_ledger
      (user_id, delta, kind, reason, balance_after, model,
       prompt_tokens, completion_tokens, cost_usd, metadata)
    values
      (p_user_id, -v_credits, 'deduction', 'cloud_ai_usage', v_balance, p_model,
       coalesce(p_prompt_tokens, 0), coalesce(p_completion_tokens, 0), v_cost,
       coalesce(p_metadata, '{}'::jsonb));
  end if;

  insert into public.usage_events
    (user_id, model, provider, prompt_tokens, completion_tokens, cost_usd,
     credits_charged, request_id, status, metadata)
  values
    (p_user_id, p_model, p_provider, coalesce(p_prompt_tokens, 0),
     coalesce(p_completion_tokens, 0), v_cost, v_credits, p_request_id,
     coalesce(p_status, 'ok'), coalesce(p_metadata, '{}'::jsonb));

  return jsonb_build_object('balance', v_balance, 'charged', v_credits);
end;
$$;

-- Replace the feature set for a user after a tier change.
create or replace function public.sync_entitlements(
  p_user_id uuid,
  p_features text[],
  p_source text default 'plan'
)
returns void
language plpgsql
security definer
set search_path = public
as $$
begin
  delete from public.entitlements
   where user_id = p_user_id
     and feature <> all (coalesce(p_features, '{}'::text[]));

  insert into public.entitlements (user_id, feature, enabled, source)
  select p_user_id, feature, true, coalesce(p_source, 'plan')
    from unnest(coalesce(p_features, '{}'::text[])) as feature
  on conflict (user_id, feature)
  do update set enabled = true, source = excluded.source, updated_at = now();
end;
$$;

-- ----------------------------------------------------------------------------
-- Row Level Security
-- ----------------------------------------------------------------------------
alter table public.profiles enable row level security;
alter table public.credit_ledger enable row level security;
alter table public.usage_events enable row level security;
alter table public.entitlements enable row level security;
alter table public.subscriptions enable row level security;
alter table public.webhook_events enable row level security;

drop policy if exists "profiles: read own" on public.profiles;
create policy "profiles: read own" on public.profiles
  for select using (auth.uid() = id);

drop policy if exists "profiles: update own display name" on public.profiles;
create policy "profiles: update own display name" on public.profiles
  for update using (auth.uid() = id) with check (auth.uid() = id);

drop policy if exists "ledger: read own" on public.credit_ledger;
create policy "ledger: read own" on public.credit_ledger
  for select using (auth.uid() = user_id);

drop policy if exists "usage: read own" on public.usage_events;
create policy "usage: read own" on public.usage_events
  for select using (auth.uid() = user_id);

drop policy if exists "entitlements: read own" on public.entitlements;
create policy "entitlements: read own" on public.entitlements
  for select using (auth.uid() = user_id);

drop policy if exists "subscriptions: read own" on public.subscriptions;
create policy "subscriptions: read own" on public.subscriptions
  for select using (auth.uid() = user_id);

-- webhook_events is server-only: no policies => no client access.

-- Column-level grants: clients may read their rows and update only their
-- display name. Tier and credit columns can only be written by the service role.
revoke all on public.profiles from anon, authenticated;
grant select on public.profiles to authenticated;
grant update (display_name, updated_at) on public.profiles to authenticated;

revoke all on public.credit_ledger from anon, authenticated;
grant select on public.credit_ledger to authenticated;

revoke all on public.usage_events from anon, authenticated;
grant select on public.usage_events to authenticated;

revoke all on public.entitlements from anon, authenticated;
grant select on public.entitlements to authenticated;

revoke all on public.subscriptions from anon, authenticated;
grant select on public.subscriptions to authenticated;

revoke all on public.webhook_events from anon, authenticated;

-- Credit functions are server-side only.
revoke all on function public.grant_credits(uuid, numeric, text, jsonb) from public, anon, authenticated;
revoke all on function public.deduct_credits(uuid, numeric, text, jsonb) from public, anon, authenticated;
revoke all on function public.record_usage(uuid, text, integer, integer, numeric, numeric, text, text, text, jsonb) from public, anon, authenticated;
revoke all on function public.sync_entitlements(uuid, text[], text) from public, anon, authenticated;

grant execute on function public.grant_credits(uuid, numeric, text, jsonb) to service_role;
grant execute on function public.deduct_credits(uuid, numeric, text, jsonb) to service_role;
grant execute on function public.record_usage(uuid, text, integer, integer, numeric, numeric, text, text, text, jsonb) to service_role;
grant execute on function public.sync_entitlements(uuid, text[], text) to service_role;

-- ----------------------------------------------------------------------------
-- Convenience view (optional; used by dashboards)
-- ----------------------------------------------------------------------------
create or replace view public.credit_summary as
select
  p.id as user_id,
  p.email,
  p.tier,
  p.credits_balance,
  p.credits_lifetime_granted,
  p.credits_lifetime_used,
  p.subscription_status,
  p.current_period_end,
  coalesce(u.requests, 0) as usage_requests,
  coalesce(u.prompt_tokens, 0) as prompt_tokens,
  coalesce(u.completion_tokens, 0) as completion_tokens,
  coalesce(u.cost_usd, 0) as cost_usd
from public.profiles p
left join (
  select user_id,
         count(*) as requests,
         sum(prompt_tokens) as prompt_tokens,
         sum(completion_tokens) as completion_tokens,
         sum(cost_usd) as cost_usd
    from public.usage_events
   group by user_id
) u on u.user_id = p.id;

revoke all on public.credit_summary from anon, authenticated;
grant select on public.credit_summary to service_role;
