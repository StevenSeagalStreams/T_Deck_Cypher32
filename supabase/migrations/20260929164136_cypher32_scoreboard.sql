-- Cypher32 worldwide scoreboard.
--
-- Devices never touch tables. Everything lives in the private schema c32
-- (not exposed over the API); the only entry points are three functions in
-- public: c32_register, c32_sync and c32_board.
--
-- Opponent-confirmed: a device reports what happened to it ("I breached X",
-- "I was breached by Y", "I was traced by X", "I held Y off"). A fight only
-- counts when BOTH devices have reported the same hack — the same pair, the
-- same radio sequence number, within a few hours of each other.

create extension if not exists pgcrypto with schema extensions;

create schema if not exists c32;
revoke all on schema c32 from public, anon, authenticated;

create table c32.players (
  chip        text primary key check (chip ~ '^[0-9a-f]{8}$'),
  secret_hash bytea not null,
  codename    text not null default '',
  faction     text not null default '?' check (faction in ('B','W','R','G','?')),
  level       int  not null default 1 check (level between 1 and 32),
  xp          int  not null default 0 check (xp >= 0),
  created_at  timestamptz not null default now(),
  last_sync   timestamptz not null default now(),
  banned      boolean not null default false
);

create table c32.reports (
  id        bigserial primary key,
  reporter  text not null references c32.players(chip) on delete cascade,
  eid       bigint not null,                 -- device-chosen id: a resend is not a second report
  kind      text not null check (kind in ('won','lost','held','breached')),
  other     text not null check (other ~ '^[0-9a-f]{8}$'),
  seq       int  not null check (seq between 0 and 255),
  at        timestamptz not null,
  matched   boolean not null default false,
  unique (reporter, eid)
);
create index reports_match on c32.reports (other, reporter, seq) where not matched;

create table c32.fights (
  id        bigserial primary key,
  attacker  text not null references c32.players(chip) on delete cascade,
  defender  text not null references c32.players(chip) on delete cascade,
  outcome   text not null check (outcome in ('breach','hold')),
  at        timestamptz not null
);
create index fights_attacker on c32.fights (attacker, at);
create index fights_defender on c32.fights (defender, at);

create table c32.reg_log (ip text not null, at timestamptz not null default now());
create index reg_log_ip on c32.reg_log (ip, at);

alter table c32.players enable row level security;
alter table c32.reports enable row level security;
alter table c32.fights  enable row level security;
alter table c32.reg_log enable row level security;

-- Points: 10 per confirmed breach, 5 per confirmed hold. At most 3 fights
-- between the same two players count per week, so two friends (or one
-- person with two devices) cannot farm each other up the board.
create or replace view c32.scored as
  select f.*, row_number() over (
           partition by least(f.attacker, f.defender), greatest(f.attacker, f.defender),
                        date_trunc('week', f.at)
           order by f.at) as nth
  from c32.fights f;

create or replace function c32.standings()
returns table (chip text, codename text, faction text, level int, score bigint,
               breaches bigint, holds bigint, last_sync timestamptz, rank bigint)
language sql stable set search_path = '' as $$
  with pts as (
    select attacker as chip, 10 as p, 1 as b, 0 as h from c32.scored where outcome = 'breach' and nth <= 3
    union all
    select defender, 5, 0, 1 from c32.scored where outcome = 'hold' and nth <= 3
  ), agg as (
    select chip, sum(p) as score, sum(b) as breaches, sum(h) as holds from pts group by chip
  )
  select p.chip, p.codename, p.faction, p.level,
         coalesce(a.score, 0), coalesce(a.breaches, 0), coalesce(a.holds, 0), p.last_sync,
         rank() over (order by coalesce(a.score, 0) desc, p.level desc, p.xp desc)
  from c32.players p left join agg a on a.chip = p.chip
  where not p.banned;
$$;

create or replace function c32.client_ip() returns text
language sql stable set search_path = '' as $$
  select coalesce(split_part(current_setting('request.headers', true)::json->>'x-forwarded-for', ',', 1), 'unknown');
$$;

-- ── register ────────────────────────────────────────────────────────────
-- First come, first served for a chip id. The secret is made here and
-- returned exactly once; only its hash is kept.
create or replace function public.c32_register(p_chip text, p_codename text, p_faction text)
returns jsonb language plpgsql security definer set search_path = '' as $$
declare
  v_secret text;
  v_ip text := c32.client_ip();
begin
  p_chip := lower(coalesce(p_chip, ''));
  if p_chip !~ '^[0-9a-f]{8}$' then
    return jsonb_build_object('ok', false, 'error', 'bad chip id');
  end if;
  if exists (select 1 from c32.players where chip = p_chip) then
    return jsonb_build_object('ok', false, 'error', 'already registered');
  end if;
  if (select count(*) from c32.reg_log where ip = v_ip and at > now() - interval '1 day') >= 10 then
    return jsonb_build_object('ok', false, 'error', 'too many registrations from here today');
  end if;
  insert into c32.reg_log (ip) values (v_ip);
  v_secret := encode(extensions.gen_random_bytes(24), 'hex');
  insert into c32.players (chip, secret_hash, codename, faction)
  values (p_chip, extensions.digest(v_secret, 'sha256'),
          left(regexp_replace(coalesce(p_codename, ''), '[^A-Za-z0-9 _-]', '', 'g'), 16),
          case when p_faction in ('B','W','R','G') then p_faction else '?' end);
  return jsonb_build_object('ok', true, 'secret', v_secret);
end $$;

-- ── sync ────────────────────────────────────────────────────────────────
-- p_events: [{ "eid": n, "kind": "won|lost|held|breached", "other": "8hex",
--              "seq": 0-255, "age": seconds ago }]
create or replace function public.c32_sync(p_chip text, p_secret text, p_codename text,
                                           p_faction text, p_level int, p_xp int,
                                           p_events jsonb default '[]'::jsonb)
returns jsonb language plpgsql security definer set search_path = '' as $$
declare
  v_player c32.players;
  e jsonb;
  v_kind text; v_other text; v_seq int; v_age int; v_eid bigint; v_at timestamptz;
  v_id bigint;
  v_att text; v_def text; v_want text; v_outcome text;
  v_match c32.reports;
  v_acked jsonb := '[]'::jsonb;
  v_me record;
  v_top jsonb;
begin
  p_chip := lower(coalesce(p_chip, ''));
  select * into v_player from c32.players where chip = p_chip;
  if not found or v_player.secret_hash <> extensions.digest(coalesce(p_secret, ''), 'sha256') then
    return jsonb_build_object('ok', false, 'error', 'unknown device or wrong secret');
  end if;
  if v_player.banned then
    return jsonb_build_object('ok', false, 'error', 'banned');
  end if;

  update c32.players set
    codename  = left(regexp_replace(coalesce(p_codename, codename), '[^A-Za-z0-9 _-]', '', 'g'), 16),
    faction   = case when p_faction in ('B','W','R','G') then p_faction else faction end,
    level     = greatest(1, least(32, coalesce(p_level, level))),
    xp        = greatest(0, coalesce(p_xp, xp)),
    last_sync = now()
  where chip = p_chip;

  if jsonb_typeof(p_events) = 'array' then
    for e in select * from jsonb_array_elements(p_events) limit 32 loop
      begin
        v_eid   := (e->>'eid')::bigint;
        v_kind  := e->>'kind';
        v_other := lower(e->>'other');
        v_seq   := (e->>'seq')::int;
        v_age   := greatest(0, coalesce((e->>'age')::int, 0));
      exception when others then
        continue;
      end;
      if v_eid is null then continue; end if;
      v_acked := v_acked || to_jsonb(v_eid);
      -- Anything malformed or older than a week is acknowledged and dropped.
      if v_kind not in ('won','lost','held','breached') or v_other !~ '^[0-9a-f]{8}$'
         or v_other = p_chip or v_seq not between 0 and 255 or v_age > 7 * 86400 then
        continue;
      end if;
      v_at := now() - make_interval(secs => v_age);

      insert into c32.reports (reporter, eid, kind, other, seq, at)
      values (p_chip, v_eid, v_kind, v_other, v_seq, v_at)
      on conflict (reporter, eid) do nothing
      returning id into v_id;
      if v_id is null then continue; end if;          -- a resend

      -- The other side of the same hack.
      v_want := case v_kind when 'won' then 'breached' when 'breached' then 'won'
                            when 'lost' then 'held' else 'lost' end;
      select * into v_match from c32.reports r
       where r.reporter = v_other and r.other = p_chip and r.kind = v_want
         and r.seq = v_seq and not r.matched
         and abs(extract(epoch from (r.at - v_at))) < 6 * 3600
       order by abs(extract(epoch from (r.at - v_at))) limit 1
       for update;
      if not found then continue; end if;

      update c32.reports set matched = true where id in (v_id, v_match.id);
      v_att := case when v_kind in ('won','lost') then p_chip else v_other end;
      v_def := case when v_kind in ('won','lost') then v_other else p_chip end;
      v_outcome := case when v_kind in ('won','breached') then 'breach' else 'hold' end;
      -- The game's own locks: one breach per pair per 12 h, one hold per 30 min.
      if exists (select 1 from c32.fights f where f.attacker = v_att and f.defender = v_def
                   and f.outcome = v_outcome
                   and f.at > least(v_at, v_match.at) - case v_outcome when 'breach'
                               then interval '11 hours' else interval '25 minutes' end) then
        continue;
      end if;
      insert into c32.fights (attacker, defender, outcome, at)
      values (v_att, v_def, v_outcome, least(v_at, v_match.at));
    end loop;
  end if;

  select * into v_me from c32.standings() s where s.chip = p_chip;
  select coalesce(jsonb_agg(jsonb_build_object('r', s.rank, 'n', s.codename, 'f', s.faction,
                                               'l', s.level, 's', s.score) order by s.rank), '[]'::jsonb)
    into v_top from (select * from c32.standings() order by rank limit 10) s;
  return jsonb_build_object('ok', true, 'acked', v_acked, 'rank', v_me.rank,
                            'score', v_me.score, 'breaches', v_me.breaches, 'holds', v_me.holds,
                            'players', (select count(*) from c32.players where not banned),
                            'top', v_top);
end $$;

-- ── board (the public page) ─────────────────────────────────────────────
create or replace function public.c32_board(p_limit int default 100)
returns table (rank bigint, codename text, faction text, level int, score bigint,
               breaches bigint, holds bigint, last_sync timestamptz)
language sql stable security definer set search_path = '' as $$
  select s.rank, s.codename, s.faction, s.level, s.score, s.breaches, s.holds, s.last_sync
  from c32.standings() s order by s.rank limit greatest(1, least(coalesce(p_limit, 100), 500));
$$;

revoke all on function public.c32_register(text, text, text) from public;
revoke all on function public.c32_sync(text, text, text, text, int, int, jsonb) from public;
revoke all on function public.c32_board(int) from public;
grant execute on function public.c32_register(text, text, text) to anon, authenticated;
grant execute on function public.c32_sync(text, text, text, text, int, int, jsonb) to anon, authenticated;
grant execute on function public.c32_board(int) to anon, authenticated;
revoke all on all functions in schema c32 from public, anon, authenticated;
