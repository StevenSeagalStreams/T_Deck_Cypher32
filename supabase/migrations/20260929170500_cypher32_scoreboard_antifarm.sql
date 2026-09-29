-- Harden the board against one person scoring with many devices.
--
-- 1. The client address comes from Cloudflare's cf-connecting-ip (set at the
--    edge, not forgeable by the caller), then x-real-ip, and only then the
--    LAST x-forwarded-for entry (the one our own proxy appended) — never the
--    first, which the caller can write themselves.
-- 2. Each player keeps a hash of the connection they last synced from (never
--    the address itself). A fight between two players on the same connection
--    is recorded but does not score: one person with a drawer of chips at home
--    cannot confirm their own fights.
-- 3. Three new registrations per connection per day.

create or replace function c32.client_ip() returns text
language sql stable set search_path = '' as $$
  with h as (select current_setting('request.headers', true)::json as j)
  select coalesce(
           nullif(h.j->>'cf-connecting-ip', ''),
           nullif(h.j->>'x-real-ip', ''),
           nullif(trim(regexp_replace(coalesce(h.j->>'x-forwarded-for', ''), '^.*,', '')), ''),
           'unknown')
  from h;
$$;

create or replace function c32.net_hash() returns text
language sql stable set search_path = '' as $$
  select encode(extensions.digest('c32-net:' || c32.client_ip(), 'sha256'), 'hex');
$$;

alter table c32.players add column if not exists net_hash text;
alter table c32.fights  add column if not exists same_net boolean not null default false;

drop view if exists c32.scored;
create view c32.scored as
  select f.id, f.attacker, f.defender, f.outcome, f.at, row_number() over (
           partition by least(f.attacker, f.defender), greatest(f.attacker, f.defender),
                        date_trunc('week', f.at)
           order by f.at) as nth
  from c32.fights f
  where not f.same_net;

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
    return jsonb_build_object('ok', false, 'error', 'chip already registered');
  end if;
  if (select count(*) from c32.reg_log where ip = v_ip and at > now() - interval '1 day') >= 3 then
    return jsonb_build_object('ok', false, 'error', 'too many new devices from here today');
  end if;
  insert into c32.reg_log (ip) values (v_ip);
  delete from c32.reg_log where at < now() - interval '2 days';
  v_secret := encode(extensions.gen_random_bytes(24), 'hex');
  insert into c32.players (chip, secret_hash, codename, faction, net_hash)
  values (p_chip, extensions.digest(v_secret, 'sha256'),
          left(regexp_replace(coalesce(p_codename, ''), '[^A-Za-z0-9 _-]', '', 'g'), 16),
          case when p_faction in ('B','W','R','G') then p_faction else '?' end,
          c32.net_hash());
  return jsonb_build_object('ok', true, 'secret', v_secret);
end $$;

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
  v_net text := c32.net_hash();
  v_same boolean;
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
    xp        = greatest(0, least(100000, coalesce(p_xp, xp))),
    last_sync = now(),
    net_hash  = v_net
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
      if v_kind is null or v_kind not in ('won','lost','held','breached') or v_other is null
         or v_other !~ '^[0-9a-f]{8}$' or v_other = p_chip or v_seq is null
         or v_seq not between 0 and 255 or v_age > 7 * 86400 then
        continue;
      end if;
      v_at := now() - make_interval(secs => v_age);

      insert into c32.reports (reporter, eid, kind, other, seq, at)
      values (p_chip, v_eid, v_kind, v_other, v_seq, v_at)
      on conflict (reporter, eid) do nothing
      returning id into v_id;
      if v_id is null then continue; end if;

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
      if exists (select 1 from c32.fights f where f.attacker = v_att and f.defender = v_def
                   and f.outcome = v_outcome
                   and f.at > least(v_at, v_match.at) - case v_outcome when 'breach'
                               then interval '11 hours' else interval '25 minutes' end) then
        continue;
      end if;
      -- Both sides report from the same connection: kept, but not scored.
      select coalesce(a.net_hash = b.net_hash, false) into v_same
        from c32.players a, c32.players b where a.chip = v_att and b.chip = v_def;
      insert into c32.fights (attacker, defender, outcome, at, same_net)
      values (v_att, v_def, v_outcome, least(v_at, v_match.at), coalesce(v_same, false));
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

revoke all on all functions in schema c32 from public, anon, authenticated;
