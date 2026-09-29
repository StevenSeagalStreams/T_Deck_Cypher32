-- World Board, third pass (after review).
--
-- 1. Registration takes the device's own secret, made and saved on the device
--    before it is sent. Registering again with the same secret is accepted,
--    so a reply lost on the way back no longer locks a device out for good.
-- 2. Every report is acknowledged once its id has been read, even if the rest
--    of it is malformed — otherwise the device resends it forever.
-- 3. Matching is its own function, takes a lock on the pair, and each sync
--    retries the caller's own unmatched recent reports: two devices syncing at
--    the same instant can no longer both miss each other.
-- 4. Fights between devices on the same connection score again. Players at
--    the same meetup share venue Wi-Fi, which is exactly where LoRa fights
--    happen, and a cheater can simply use two networks. They stay flagged
--    (same_net) for an admin to look at.

drop view if exists c32.scored;
create view c32.scored as
  select f.id, f.attacker, f.defender, f.outcome, f.at, row_number() over (
           partition by least(f.attacker, f.defender), greatest(f.attacker, f.defender),
                        date_trunc('week', f.at)
           order by f.at) as nth
  from c32.fights f;

drop function if exists public.c32_register(text, text, text);
create or replace function public.c32_register(p_chip text, p_codename text, p_faction text,
                                               p_secret text default null)
returns jsonb language plpgsql security definer set search_path = '' as $$
declare
  v_ip text := c32.client_ip();
  v_hash bytea;
  v_have bytea;
begin
  p_chip := lower(coalesce(p_chip, ''));
  if p_chip !~ '^[0-9a-f]{8}$' then
    return jsonb_build_object('ok', false, 'error', 'bad chip id');
  end if;
  if coalesce(p_secret, '') !~ '^[0-9a-f]{32,64}$' then
    return jsonb_build_object('ok', false, 'error', 'update the firmware to join the board');
  end if;
  v_hash := extensions.digest(p_secret, 'sha256');
  select secret_hash into v_have from c32.players where chip = p_chip;
  if found then
    if v_have = v_hash then
      return jsonb_build_object('ok', true);            -- the same device, again
    end if;
    return jsonb_build_object('ok', false, 'error', 'chip already registered');
  end if;
  if (select count(*) from c32.reg_log where ip = v_ip and at > now() - interval '1 day') >= 3 then
    return jsonb_build_object('ok', false, 'error', 'too many new devices from here today');
  end if;
  insert into c32.reg_log (ip) values (v_ip);
  delete from c32.reg_log where at < now() - interval '2 days';
  insert into c32.players (chip, secret_hash, codename, faction, net_hash)
  values (p_chip, v_hash,
          left(regexp_replace(coalesce(p_codename, ''), '[^A-Za-z0-9 _-]', '', 'g'), 16),
          case when p_faction in ('B','W','R','G') then p_faction else '?' end,
          c32.net_hash())
  on conflict (chip) do nothing;
  return jsonb_build_object('ok', true);
end $$;

-- Try to pair one report with the other side of the same hack.
create or replace function c32.try_match(p_id bigint) returns void
language plpgsql set search_path = '' as $$
declare
  r c32.reports;
  m c32.reports;
  v_want text; v_att text; v_def text; v_outcome text; v_same boolean;
begin
  select * into r from c32.reports where id = p_id;
  if not found or r.matched then return; end if;
  -- One pair at a time, so two simultaneous syncs see each other's rows.
  perform pg_advisory_xact_lock(hashtext(least(r.reporter, r.other) || greatest(r.reporter, r.other)));
  select * into r from c32.reports where id = p_id;
  if r.matched then return; end if;
  v_want := case r.kind when 'won' then 'breached' when 'breached' then 'won'
                        when 'lost' then 'held' else 'lost' end;
  select * into m from c32.reports x
   where x.reporter = r.other and x.other = r.reporter and x.kind = v_want
     and x.seq = r.seq and not x.matched
     and abs(extract(epoch from (x.at - r.at))) < 6 * 3600
   order by abs(extract(epoch from (x.at - r.at))) limit 1
   for update;
  if not found then return; end if;
  update c32.reports set matched = true where id in (r.id, m.id);
  v_att := case when r.kind in ('won','lost') then r.reporter else r.other end;
  v_def := case when r.kind in ('won','lost') then r.other else r.reporter end;
  v_outcome := case when r.kind in ('won','breached') then 'breach' else 'hold' end;
  -- The game's own locks: one breach per pair per 12 h, one hold per 30 min.
  if exists (select 1 from c32.fights f where f.attacker = v_att and f.defender = v_def
               and f.outcome = v_outcome
               and f.at > least(r.at, m.at) - case v_outcome when 'breach'
                           then interval '11 hours' else interval '25 minutes' end) then
    return;
  end if;
  select coalesce(a.net_hash = b.net_hash, false) into v_same
    from c32.players a, c32.players b where a.chip = v_att and b.chip = v_def;
  insert into c32.fights (attacker, defender, outcome, at, same_net)
  values (v_att, v_def, v_outcome, least(r.at, m.at), coalesce(v_same, false));
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
    xp        = greatest(0, least(100000, coalesce(p_xp, xp))),
    last_sync = now(),
    net_hash  = c32.net_hash()
  where chip = p_chip;

  if jsonb_typeof(p_events) = 'array' then
    for e in select * from jsonb_array_elements(p_events) limit 32 loop
      -- The id first, on its own: anything we can name, we acknowledge.
      begin
        v_eid := (e->>'eid')::bigint;
      exception when others then
        continue;
      end;
      if v_eid is null then continue; end if;
      v_acked := v_acked || to_jsonb(v_eid);
      begin
        v_kind  := e->>'kind';
        v_other := lower(e->>'other');
        v_seq   := (e->>'seq')::int;
        v_age   := greatest(0, coalesce((e->>'age')::int, 0));
      exception when others then
        continue;
      end;
      if v_kind is null or v_kind not in ('won','lost','held','breached') or v_other is null
         or v_other !~ '^[0-9a-f]{8}$' or v_other = p_chip or v_seq is null
         or v_seq not between 0 and 255 or v_age > 7 * 86400 then
        continue;
      end if;
      v_at := now() - make_interval(secs => v_age);
      v_id := null;
      insert into c32.reports (reporter, eid, kind, other, seq, at)
      values (p_chip, v_eid, v_kind, v_other, v_seq, v_at)
      on conflict (reporter, eid) do nothing
      returning id into v_id;
      if v_id is not null then perform c32.try_match(v_id); end if;
    end loop;
  end if;

  -- Anything of ours still waiting (the other side may have landed since).
  perform c32.try_match(r.id) from c32.reports r
   where r.reporter = p_chip and not r.matched and r.at > now() - interval '6 hours';

  select * into v_me from c32.standings() s where s.chip = p_chip;
  select coalesce(jsonb_agg(jsonb_build_object('r', s.rank, 'n', s.codename, 'f', s.faction,
                                               'l', s.level, 's', s.score) order by s.rank), '[]'::jsonb)
    into v_top from (select * from c32.standings() order by rank limit 10) s;
  return jsonb_build_object('ok', true, 'acked', v_acked, 'rank', v_me.rank,
                            'score', v_me.score, 'breaches', v_me.breaches, 'holds', v_me.holds,
                            'players', (select count(*) from c32.players where not banned),
                            'top', v_top);
end $$;

revoke all on function public.c32_register(text, text, text, text) from public;
grant execute on function public.c32_register(text, text, text, text) to anon, authenticated;
revoke all on all functions in schema c32 from public, anon, authenticated;
