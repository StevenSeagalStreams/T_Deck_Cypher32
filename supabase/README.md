# The World Board backend

The online scoreboard is a Supabase (Postgres) project. Everything it does is
in `migrations/`, applied in order:

- `20260929164136_cypher32_scoreboard.sql`: the private schema `c32`, the
  three public entry points (`c32_register`, `c32_sync`, `c32_board`), and
  the opponent-confirmed scoring.
- `20260929170500_cypher32_scoreboard_antifarm.sql`: the caller's real
  address from the edge (never a header the caller can write), stored only
  as a hash per player, and at most 3 new devices per connection per day.
- `20260929171922_cypher32_scoreboard_v3.sql`: registration takes the
  device's own secret, and repeating it is harmless. Every named report is
  acknowledged. Matching locks the pair and is retried on every sync, so two
  devices syncing at once still pair up. Same-connection fights score again,
  because players at one meetup share its Wi-Fi. They stay flagged
  (`same_net`) for review.

The tables are not reachable through the API at all: row level security is
on with no policies, and the schema is not exposed. The three functions are
`SECURITY DEFINER` on purpose. They are the API, and each checks what it is
given (the device secret, the shape of every report). Supabase's advisor
flags both of these; both are intended.

To run your own board: create a project, apply the migrations in order, and
put its host and publishable key in `C32_BOARD_HOST` / `C32_BOARD_KEY`
(`tdeck_net.h`) and in `web/board.html`.

Admin, from the SQL editor:

```sql
-- free a chip id someone registered before its owner did
delete from c32.players where chip = 'a1b2c3d4';
-- take a cheater off the board
update c32.players set banned = true where chip = 'a1b2c3d4';
```
