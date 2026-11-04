# CodinGame API reference

Base URL: `https://www.codingame.com/api/v1`

## Conventions

- **Authentication**: `Authorization: Bearer <token>` on every endpoint except `/auth/*`, `/docs*`,
  `/problems/*`, `GET /puzzles`, `GET /puzzles/{pretty_id}` and `GET /puzzles/{pretty_id}/stats`, which
  also accept a token to add the user's data. Tokens look like `cgp_` followed by 42 letters and digits.
- **Scopes**: each endpoint needs one scope, for challenges and puzzles alike: `challenge:play` for
  registering, starting test games and changing the nickname, squad or teams, `challenge:submit` for
  submitting, `challenge:read` for every other endpoint. The contribution endpoints need
  `contribution:read` or `contribution:write`. The endpoints that need no token check the scope when a
  token is sent.
- **Bodies**: JSON objects, read as JSON whatever the `Content-Type`. Send
  `Content-Type: application/json` anyway.
- **Dates**: ISO-8601 instants (`2026-10-11T15:00:00Z`). Durations are in seconds (`*_in_seconds`).
- **Absent members**: optional members are omitted rather than sent as `null`, unless stated.
- **Ids**: `public_id` identifies a challenge (e.g. `spring-challenge-2026`), `pretty_id` a puzzle
  (e.g. `coders-strike-back`). Numeric ids identify test games, games, submissions and agents.

## Errors

Every error is an RFC 9457 problem, served as `application/problem+json`:

```json
{
  "type": "https://www.codingame.com/api/v1/problems/not-registered",
  "title": "Not registered",
  "status": 409,
  "detail": "You are not registered to the contest spring-challenge-2026.",
  "hint": "Register with POST /api/v1/challenges/spring-challenge-2026/register (scope challenge:play), then retry."
}
```

- `hint` tells what to do next. Follow it.
- `code` is present when an existing CodinGame rule rejected the request (e.g. `INVALID_LANGUAGE`,
  `CHALLENGE_NOT_FOUND`, `ERROR_268`).
- `type` links to the description of the problem (`GET /problems/<slug>`).

| Status | Slug | Meaning |
|---|---|---|
| 400 | `bad-request` | Invalid query parameter or body. The hint shows a valid body. |
| 400 | `request-rejected` | A rule rejected the request; read `code` and `detail`. Also 422 for some rules. |
| 401 | `missing-token` | No `Authorization` header. Get a token with the device flow. |
| 401 | `invalid-token` | Unknown, malformed, expired or revoked token, or disabled account. Get a new token. |
| 403 | `insufficient-scope` | The token lacks a scope listed in `required_scope`. |
| 403 | `forbidden` | Not allowed for this user (e.g. `code` `REPLAYS_DISABLED`). |
| 403 | `arcade-game` | The puzzle is an arcade game (a classic puzzle): listed, never played. `url` is its page for the user. |
| 404 | `not-found` | Unknown URL or id. `code` tells which: `CHALLENGE_NOT_FOUND`, `PUZZLE_NOT_FOUND`, `TEST_GAME_NOT_FOUND`, `GAME_NOT_FOUND`, `SUBMISSION_NOT_FOUND`, `SQUAD_NOT_FOUND`, `SCHOOL_NOT_FOUND`, `CONTRIBUTION_NOT_FOUND`, `BUILD_NOT_FOUND`. |
| 405 | `method-not-allowed` | Wrong HTTP method. |
| 409 | `not-registered` | The user is not registered to the challenge. |
| 409 | `challenge-not-started` | The challenge has not started. |
| 409 | `challenge-ended` | The challenge has ended, or the user's site has no play window left: no more test games or submissions. |
| 409 | `challenge-paused` | The user's site is outside its play windows: `next_play_window` and `Retry-After` tell when play resumes. |
| 409 | `not-a-squad-contest` | Squad endpoint on a challenge without squads. |
| 409 | `no-school-company-teams` | Team endpoint on a challenge that does not rank schools and companies. |
| 409 | `puzzle-in-contest` | The puzzle is played as the contest `challenge_public_id`: use the challenge endpoints. |
| 409 | `unsupported-game-mode` | Opponents or battles of an optimization or code golf puzzle. |
| 422 | `nickname-rejected` | The nickname is taken, too long or uses forbidden characters. |
| 409 | `test-game-running` | A test game of the user is still running: `test_game_id` and `poll_url` point to it. Without them, another request of the user is running a test case that is not a game. |
| 409 | `contribution-version-conflict` | The contribution changed since the `base_version` of the update. |
| 409 | `contribution-already-exists` | The user created a contribution with this title less than a day ago: `handle`. |
| 422 | `contribution-invalid` | A rule of the contribution pages rejected the content: `field`, and `test_case` and `kind` for a test case. |
| 422 | `test-case-failed` | The solution of a contribution fails a test case or a validator: `test_case`, `kind`, `found`, `expected` or `error`. |
| 422 | `registration-form-invalid` | The registration form needs values, listed in `fields`, with the whole `form`. |
| 429 | `rate-limited` | A limit is reached. Wait `Retry-After` seconds. |
| 500 | `internal-error` | Server failure. Retry later. |

The device flow errors (`/auth/token`) are OAuth errors: see below.

## Limits

### API requests

Every request counts in a fixed window of one minute, per user for requests with a token and per IP
address for requests without one (currently 120 and 60 requests). The window is named `requests_1m`.
Every JSON object response carries it as `limits.requests`, and every response has it in the
`RateLimit` and `RateLimit-Policy` headers:

```json
"limits": {"requests": {"name": "requests_1m", "limit": 120, "used": 3, "remaining": 117, "window_seconds": 60, "resets_in_seconds": 41}}
```

`resets_in_seconds` is when the whole window resets. Over the limit, the request answers 429
`rate-limited` with `Retry-After` and `limits.requests`. Prefer `wait=30` to polling: it costs one
request.

### Test games and submissions

Test games and submissions are counted per user and per time window. The windows depend on the
challenge; typical values:

| Window | Limit |
|---|---|
| `submits_5m`, `submits_30m`, `submits_1h`, `submits_5h` | 5, 20, 40, 100 |
| `test_games_1m`, `test_games_5m`, `test_games_15m`, `test_games_1h`, `test_games_3h`, `test_games_6h`, `test_games_24h` | 10, 20, 30, 80, 150, 200, 600 |

A window object:

```json
{"name": "submits_5m", "limit": 5, "used": 2, "remaining": 3, "window_seconds": 300, "resets_in_seconds": 120}
```

`resets_in_seconds`: when `remaining` is 0, the seconds until one more request is allowed; otherwise
the seconds until the oldest counted request leaves the window (0 when nothing is counted).

The `limits` member of the test game, submission and progress responses also holds the tightest
window of each kind: `{"submits": <window>, "test_games": <window>, "requests": <window>}`.
`GET /limits` lists every window.

Headers on the same responses:

```
RateLimit-Policy: "submits_5m";q=5;w=300, "submits_30m";q=20;w=1800, ..., "requests_1m";q=120;w=60
RateLimit: "submits_5m";r=3;t=120, "requests_1m";r=117;t=41
```

`RateLimit` describes the tightest window of each kind, then the request window: `r` remaining, `t`
seconds until reset. The tightest window is the one with the fewest remaining requests, then the
longest wait.

When a window is exhausted, the request answers 429 `rate-limited` with a `Retry-After` header, a
`retry_after_seconds` member and the exhausted window in `rate_limit`. Wait that long; an earlier
retry is rejected again.

There are no windows (empty lists, absent `limits` members) until the session has been opened once,
with `GET /challenges/{public_id}` while the challenge is ongoing, or `GET /puzzles/{pretty_id}` with a
token.

## Authentication: OAuth device flow (RFC 8628)

These endpoints take form-encoded or JSON bodies, need no token and ignore any `Authorization` header.
Responses have `Cache-Control: no-store`.

### POST /auth/device

| Parameter | Required | Description |
|---|---|---|
| `client_name` | yes | Name of the agent, shown to the user on the approval page (100 characters at most). `client_id` is accepted as an alias. |
| `scope` | no | Space-separated scopes (or a JSON array). Default: every scope, `challenge:read challenge:play challenge:submit contribution:read contribution:write`. |

```sh
curl -s -X POST https://www.codingame.com/api/v1/auth/device -d client_name="Claude Code"
```

200:

```json
{
  "device_code": "...",
  "user_code": "BCDF-GHJK",
  "verification_uri": "https://www.codingame.com/authorize",
  "verification_uri_complete": "https://www.codingame.com/authorize?code=BCDF-GHJK",
  "expires_in": 900,
  "interval": 5
}
```

Show `verification_uri_complete` and `user_code` to the user. Keep `device_code` private.

### POST /auth/token

| Parameter | Required | Value |
|---|---|---|
| `grant_type` | yes | `urn:ietf:params:oauth:grant-type:device_code` |
| `device_code` | yes | From `/auth/device` |

Poll every `interval` seconds. 200 once the user has approved:

```json
{
  "access_token": "cgp_...",
  "token_type": "Bearer",
  "scope": "challenge:read challenge:play challenge:submit contribution:read contribution:write",
  "expires_in": 63072000,
  "expires_at": "2028-10-04T12:00:00Z"
}
```

A device code gives a token only once. Errors are 400 with OAuth members `error` and
`error_description` (plus the problem members), as `application/json`:

| `error` | What to do |
|---|---|
| `authorization_pending` | Not approved yet. Poll again after `interval` seconds. |
| `slow_down` | Polling too fast. Use the new `interval` from the response from now on. |
| `access_denied` | The user refused. Stop. |
| `expired_token` | The device code expired. Start again with `/auth/device`. |
| `invalid_grant` | Unknown or already used device code. Start again. |
| `invalid_request`, `invalid_scope`, `unsupported_grant_type` | Fix the parameters. |

Store the token in a file readable only by the user (e.g. `~/.config/codingame/token`, mode 600),
never in the conversation. The user can revoke tokens at `https://www.codingame.com/settings/tokens`.

## Challenges

Only bot programming contests hosted on CodinGame are available. Other ids answer 404
`CHALLENGE_NOT_FOUND`. Squad contests cannot be joined through the API.

States used below:
- **registered**: else 409 `not-registered`.
- **started**: the challenge is ongoing or ended, else 409 `challenge-not-started`.
- **ongoing**: started and not ended, else 409 `challenge-not-started` or `challenge-ended`.

### GET /challenges

Ongoing and upcoming challenges (by start date), then at most 20 recently ended ones.

```json
{
  "challenges": [
    {"public_id": "spring-challenge-2026", "title": "Spring Challenge 2026", "status": "ongoing",
     "start": "2026-10-01T15:00:00Z", "end": "2026-10-11T15:00:00Z"}
  ]
}
```

`status` is `upcoming`, `ongoing` or `ended`. `start` or `end` is absent when not scheduled yet.

### GET /challenges/{public_id}

```json
{
  "public_id": "spring-challenge-2026",
  "title": "Spring Challenge 2026",
  "status": "ongoing",
  "start": "2026-10-01T15:00:00Z",
  "end": "2026-10-11T15:00:00Z",
  "ends_in_seconds": 3600,
  "url": "https://www.codingame.com/contests/spring-challenge-2026",
  "registered": true,
  "league": "Wood 2",
  "players_min": 2,
  "players_max": 2,
  "disable_replays": false,
  "languages": [{"id": "Python3", "name": "Python 3"}],
  "statement": "## Goal\n\n..."
}
```

In a challenge with a timetable, outside the user's play windows:

```json
{
  "public_id": "hackathon-2026",
  "status": "paused",
  "ends_in_seconds": 86400,
  "registered": true,
  "play_windows": [
    {"start": "2026-10-09T08:00:00Z", "end": "2026-10-09T12:00:00Z", "status": "past"},
    {"start": "2026-10-09T14:00:00Z", "end": "2026-10-09T18:00:00Z", "status": "upcoming", "starts_in_seconds": 5400}
  ],
  "next": "The contest is paused for you until 2026-10-09T14:00:00Z (in 5400 seconds): ..."
}
```

Before registration (upcoming or ongoing):

```json
{
  "public_id": "spring-challenge-2026",
  "title": "Spring Challenge 2026",
  "status": "ongoing",
  "url": "https://www.codingame.com/contests/spring-challenge-2026",
  "registered": false,
  "registration_form": [
    {"id": "pseudo", "type": "text", "label": "Nickname", "required": true, "max_length": 64, "value": "Alice"}
  ],
  "next": "The rules are available once the user is registered and the contest has started. ..."
}
```

- `status`: `upcoming`, `ongoing`, `paused` or `ended`. `paused` and a per-user `ended` only happen
  in challenges with a timetable (see `play_windows`).
- `starts_in_seconds` is present while upcoming, `ends_in_seconds` while ongoing or paused.
- `league`: the user's league, when registered, in challenges with leagues.
- `players_min`, `players_max` (players per game, the user included), `languages` and `statement`
  (Markdown, in the language of the user's account) are present only when registered and ongoing.
  That call opens the user's challenge session like opening the IDE does; the first call starts it
  and later calls move it to the user's current league. Read the statement again after a promotion.
- `disable_replays`: when `true`, only the games the user played in can be read.
- `registration_form`: only while not registered, for contests with a registration form. Each field
  has a `type` (the input of the website form: `text`, `switch`, `select`, `country`, ...) and an
  English `label` in Markdown. `title` and `paragraph` entries are text to show the user, without
  `id`. `options` lists the accepted values of `switch` (`checked`, `unchecked`) and `select` fields.
  `value` is what the website fills in from the user's profile.
- `squad_max_size`: only when players register in squads (see the squad endpoints).
- `school_company_teams`: `true` only when the challenge ranks the schools and companies of its
  players (see the team endpoints).
- `play_windows`: only in challenges with a timetable, for registered users: the windows during
  which the user's site may run test games and submit, each `past`, `open` (with `ends_in_seconds`)
  or `upcoming` (with `starts_in_seconds`). While `paused`, the rules, languages and statement are
  absent, and test games, submissions, the stub generator and the opponents answer 409
  `challenge-paused`.
- `next`: only while not registered or paused.

### POST /challenges/{public_id}/register

Scope `challenge:play`. Optional body: `{"form_values": {"<field id>": "<value>"}}` for challenges with
a registration form (see `registration_form` above). Fields omitted from `form_values` get their
`value` from the form, like on the website.

In a squad challenge, the body also needs `"squad_id"`: an existing squad of the challenge (see
`GET /challenges/{public_id}/squads`). Without it, 400 `bad-request`.

201 when newly registered, 200 when already registered:

```json
{"registered": true, "already_registered": false, "next": "Read the rules with GET /api/v1/challenges/spring-challenge-2026 once the contest has started."}
```

Errors:
- 422 `registration-form-invalid`, with `fields` mapping each rejected field to the error of the
  contest's form rules (e.g. `{"pseudo": "takenPseudo"}`), and `form`, the
  `registration_form`. The detail repeats the value sent and the accepted values. Ask the user for
  the values, or ask them to register on the challenge page.
- 404 `not-found`, `code` `SQUAD_NOT_FOUND`; 422 `request-rejected`, `code` `SQUAD_TOO_FULL`.
- 422 `request-rejected`, `code` `ERROR_268` or `ERROR_269`: registration is closed.

### GET /challenges/{public_id}/stub-generator

Registered, ongoing.

```json
{"stub_generator": "read width:int height:int\ngameloop\n...", "syntax_doc": "https://www.codingame.com/api/v1/docs/stub-generator"}
```

The stub generator describes the input and output of the bot. Read the
[syntax guide](https://www.codingame.com/api/v1/docs/stub-generator) and write the parsing code from it.

### GET /challenges/{public_id}/code

Registered, started. The code currently in the user's IDE. Submissions and test games do not change
it, except test games sent with `overwrite_ide_code`.

| Query | Description |
|---|---|
| `language` | A language id. Omitted: the code currently in the session, whatever its language (ongoing only). |

```json
{"language": "Python3", "code": "import sys\n..."}
```

`code` is `null` when nothing is saved for that language. Without `language`, `language` is absent
when nothing is saved. While the challenge is ongoing, a language the challenge does not offer
answers 400 `INVALID_LANGUAGE`.

The IDE saves the code only when the user clicks Play or Submit: code the user is typing in an open
IDE is not returned until then.

### POST /challenges/{public_id}/test-games

Scope `challenge:play`. Registered, ongoing. Starts a test game of the given code, like the Play
button of the IDE. The code in the user's IDE does not change, unless `overwrite_ide_code` is `true`.

| Query | Description |
|---|---|
| `wait` | 0 to 30 (default 0): seconds to wait for the end of the game before answering. |
| `format` | `text` (default) or `json`: format of `log`. |

Body:

```json
{"language": "Python3", "code": "...", "opponents": ["default"], "game_options": null, "show_in_ide": false,
 "overwrite_ide_code": false, "overwrite_ide_game_parameters": false}
```

or, to replay a game:

```json
{"language": "Python3", "code": "...", "replay_game_id": 610230700}
```

- `opponents` (default `["default"]`), each one of:
  - `default`: the default bot, replaced by the boss of the user's league in challenges with leagues.
  - `boss`: the boss of the user's league; 400 `NO_BOSS` when the league has none.
  - `me`: the same code again.
  - an `agent_id` (number or string) from `GET /challenges/{public_id}/opponents`, or of a player
    of one of the last 500 finished battles of the current submission in the user's current league
    (the battles the IDE can send to its test games). These are the agents the IDE lets the user
    test against; any other agent answers 400 `INVALID_OPPONENT`.
- The tested code is agent 0; opponents follow in order. The total number of players must be
  between `players_min` and `players_max`.
- `game_options`: referee options such as `seed=12` (one per line), e.g. the `options` of a previous
  log to replay the same map. `null` or absent for random options.
- `replay_game_id`: a battle or test game of the user in this challenge (from `GET .../battles` or a
  previous test game). The test game takes its game options, and its players in the same seats; the
  given code plays in the user's seat. Like the "Send game parameters to IDE" button of the IDE's last
  battles. `opponents` and `game_options` must then be absent. 404 `GAME_NOT_FOUND` for a game of
  another challenge, 400 `NOT_A_PLAYER_OF_GAME` when the user did not play in it. The opponents
  must still be opponents the user can test against, as for `opponents`: 400 `INVALID_OPPONENT`
  otherwise, e.g. for a game of an earlier league. Replay games of the current league.
- `show_in_ide` (default `false`): `true` to show the game in the user's IDE of the challenge, in
  its viewer and console like a game played from the IDE. The game is sent to the IDE when this call
  or `GET /test-games/{test_game_id}` first reports it `done`, so read its end; an IDE opened
  later does not get it. Without the flag, the IDE never shows the game.
- `overwrite_ide_code` (default `false`): `true` to save the code and language of this test game as
  the code of the user's IDE, as the IDE's Play button does, and load it at once in the IDE if it is
  open. **A plain overwrite: nothing checks whether the user changed the code in the IDE since, and
  the IDE's previous code is lost.** Use it only when the user asks for it.
- `overwrite_ide_game_parameters` (default `false`): `true` to replace the opponents and game options
  of the user's open IDE with the ones of this game when it ends (sent like `show_in_ide`), without
  asking the user. The IDE's next Play then replays the game. The IDE keeps these settings in the
  page only: an IDE opened later does not get them.

202 while running (waiting for a code machine or being played):

```json
{
  "test_game_id": 610230745,
  "status": "running",
  "poll_url": "/api/v1/test-games/610230745",
  "limits": {"submits": {...}, "test_games": {"name": "test_games_1m", "limit": 10, "used": 1, "remaining": 9, "window_seconds": 60, "resets_in_seconds": 59}}
}
```

200 when the game ended within `wait`: `status` `done` with `log` (a string with `format=text`, an
object with `format=json`), or `status` `failed` with `failure` and `hint`.

Headers: `RateLimit` and `RateLimit-Policy` over the test game windows.

Errors:
- 409 `test-game-running`: one test game at a time. `test_game_id` and `poll_url` give the running
  game; wait for its end before starting another one.
- 429 `rate-limited`.
- 400 `request-rejected` with `code` `INVALID_OPPONENT`, `INVALID_PLAYER_COUNT`, `NO_BOSS`,
  `NOT_A_PLAYER_OF_GAME` or `INVALID_LANGUAGE`.
- 409 `challenge-paused` outside the play windows of a challenge with a timetable.

### GET /challenges/{public_id}/opponents

Registered, ongoing. The players the user can test against, as in the agent selection of the IDE:
in challenges with leagues, the players and bosses of the user's league room and of the rooms of
the lower leagues with the same rules; otherwise the leaderboard of the challenge.

```json
{
  "opponents": [
    {"nickname": "Boss 2", "agent_id": 789, "league": "Silver", "boss": true},
    {"nickname": "Alice", "agent_id": 999, "rank": 1, "league": "Silver", "me": true},
    {"nickname": "Bob", "agent_id": 456, "rank": 12, "league": "Bronze"}
  ]
}
```

- `agent_id`: the opponent to send to `POST /test-games`.
- `rank`: rank in the opponent's league room, absent for bosses. `league` is absent in challenges
  without leagues.
- `boss` and `me`: only on league bosses and on the user's own agent.
- Players without a submission are not listed; at most 1000 opponents.

### GET /test-games/{test_game_id}

Only the user's own test games (else 404 `TEST_GAME_NOT_FOUND`). Results are kept about one day.

| Query | Description |
|---|---|
| `wait` | 0 to 30 (default 0): seconds to wait for the end of a running game. |
| `format` | `text` (default, `text/plain`) or `json`. |

`format=text`:

```
status: running
hint: poll again with GET /api/v1/test-games/610230745?wait=30
```

```
status: failed
failure: No code machine was available to run the game in time.
hint: The code machines are busy. Wait a minute or two before starting a new test game: every start counts against the test game limits.
```

```
status: done
game 610230745
...the text log...
```

`format=json`: `{"test_game_id": 610230745, "status": "done", "poll_url": "...", "log": {...}}`, with
`failure` and `hint` when failed.

Failures:
- `No code machine was available to run the game in time.`: no code machine took the game within
  about two minutes, so it was canceled. Wait a minute or two before starting a new one.
- `The code machine failed to run the game.`: start a new test game; if the same code fails again,
  check locally that it starts and reads its input.
- `The result of this test game is no longer available.`: start a new test game.

### POST /challenges/{public_id}/submissions

Scope `challenge:submit`. Registered, ongoing. Submits the code to the arena: it replaces the
user's current submission and starts its ranking games. The submission shows in the history of the
user's IDE, with its battles; the code in the IDE does not change.

Body: `{"language": "Python3", "code": "..."}`.

201:

```json
{"submission_id": 321, "progress_url": "/api/v1/challenges/spring-challenge-2026/progress", "limits": {...}}
```

Headers: `RateLimit` and `RateLimit-Policy` over the submit windows. 429 `rate-limited` when a window
is exhausted. 400 `INVALID_LANGUAGE`.

### GET /challenges/{public_id}/submissions

Registered, started.

```json
{"submissions": [{"submission_id": 321, "language": "Python3", "submitted_at": "2026-10-05T12:00:00Z", "current": true}]}
```

Newest first. `current` is `true` on the submission playing the ranking games (in optimization
puzzles: the last submission), absent on the others.

### GET /challenges/{public_id}/submissions/{submission_id}

Registered, started.

```json
{"submission_id": 321, "language": "Python3", "current": true, "code": "..."}
```

404 `SUBMISSION_NOT_FOUND`.

### GET /challenges/{public_id}/progress

Registered, started. Ranking of the current submission.

```json
{
  "submission_id": 321,
  "language": "Python3",
  "in_progress": true,
  "percentage": 37,
  "score": 25.5,
  "rank": 120,
  "total": 3500,
  "league": "Bronze",
  "league_rank": 12,
  "league_total": 400,
  "eligible_for_promotion": false,
  "next_promotion_in_seconds": 1234,
  "league_boss": {"name": "Boss 3", "rank": 9, "score": 24.1, "agent_id": 789},
  "league_history": [{"league": "Wood 1", "entered_at": "2026-10-01T15:02:00Z"},
                     {"league": "Bronze", "entered_at": "2026-10-02T09:00:00Z"}],
  "promotion_mode": "rank_above_boss",
  "limits": {...}
}
```

- `percentage`: share of the ranking games played for the current submission; `rank` and `score` move
  until it reaches 100.
- `in_progress`: the ranking games are still being played.
- `league_*`, `eligible_for_promotion` and `next_promotion_in_seconds` exist in challenges with
  leagues. Players above the league boss are promoted when the promotion runs. `league_total`
  counts the boss.
- `league_boss`: the boss of the user's league, with its `rank` and `score` in the league. The
  leaderboard does not list bosses. Test against it with the opponent `boss`.
- `league_history`: the leagues the user entered, oldest first: the league where they joined the
  challenge, then one entry per promotion, with its date in `entered_at`. The last entry is the
  current `league`.
- `promotion_mode`: how the user gets out of their league. Absent when the league has no boss (top
  league, or a league whose next one is not open yet).
  - `rank_above_boss`: the submission becomes `eligible_for_promotion` once it has played all its
    ranking games and its score reaches the boss's; eligible players move up at the next promotion
    run (`next_promotion_in_seconds`).
  - `boss_battles`: each submission plays a series of battles against the boss only, and becomes
    eligible once it wins `boss_battle_wins_required` games outright (alone in first place). The
    league rank does not matter.
- `next_league_opens_at`: in a league without boss, when a boss is scheduled to be added to it,
  which opens the next league. Until then nobody in the league can be promoted.
- Details on each system: [leagues and promotion](https://www.codingame.com/api/v1/docs/leagues).
- Members without a value are absent.

Headers: `RateLimit` and `RateLimit-Policy` over all windows. When the leaderboard of the challenge
is hidden, this answers `request-rejected`.

### GET /challenges/{public_id}/leaderboard

| Query | Description |
|---|---|
| `country` | Two-letter country code, e.g. `FR`. |
| `language` | Language, as in the rows' `language`. |
| `league` | `legend`, `gold`, `silver`, `bronze`, `wood`, or `wood` followed by its number (`wood2`). |
| `keyword` | Part of a nickname. |
| `around_me` | `true`: the players around the user (`true` or `false`). |
| `offset` | 0 to 999 (default 0). |
| `limit` | 1 to 100 (default 20). |

At most one filter among `country`, `language`, `league`, `keyword` and `around_me`. A league the
contest does not have answers 400 `request-rejected` with `code` `INVALID_LEAGUE`, an unknown
language 400 `INVALID_LANGUAGE`; the detail lists the valid values.

```json
{
  "total": 4000,
  "matching": 30,
  "offset": 0,
  "limit": 20,
  "rows": [
    {"rank": 1, "nickname": "Alice", "league": "Legend", "score": 50.1, "language": "C++",
     "country": "FR", "agent_id": 999, "percentage": 100, "me": true}
  ],
  "me": {"rank": 1, "nickname": "Alice", "...": "..."}
}
```

- `me` in a row: only on the user's row. The top-level `me` is the user's row wherever it is, absent
  when the user is not ranked.
- League bosses are not rows of this leaderboard: see `league_boss` in the progress.
- `agent_id`: the player's current agent. Test games only accept the agents of
  `GET /challenges/{public_id}/opponents`: players of higher leagues are refused.
- The leaderboard is cached for a short time; only its first 1000 matching rows can be paged through.

### GET /challenges/{public_id}/battles

Registered, started. Ranking games of one of the user's submissions, newest first, from every
league that submission played in.

| Query | Description |
|---|---|
| `submission` | A `submission_id` from `GET /submissions`, current or past. Default: the current submission. |
| `before` | Only games whose `game_id` is lower than this one: pass the `next_before` of the previous page. |
| `from` | Only games whose `game_id` is greater than or equal to this one. To fetch the games added or finished since a previous call, pass the lowest `game_id` that was `done` false then, or the highest `game_id` seen when none was: games do not end in `game_id` order. |
| `limit` | 1 to 100 (default 20). |

```json
{
  "submission_id": 321,
  "battles": [
    {"game_id": 123, "done": true, "ended_at": "2026-10-05T12:03:00Z", "my_position": 0,
     "players": [{"position": 0, "nickname": "Alice", "agent_id": 456, "me": true},
                 {"position": 1, "nickname": "Bronze boss", "agent_id": 789, "boss": true, "league": "Bronze"}]}
  ],
  "next_before": 123
}
```

- `next_before`: present when older games exist; absent on the last page. Page through all the games
  with `?before=<next_before>` until it is absent, keeping the same `from` if you sent one.
- `position` and `my_position` are 0 for the winner, absent while the game is being played (`done`
  false, no `ended_at`).
- `boss` and `league` are only on league bosses. A boss without nickname is named `<League> boss`.
- No `submission_id` and no battles when the user has no current submission and sent no `submission`.
- 404 `SUBMISSION_NOT_FOUND`: the submission is not one of the user's in this contest.
- No scores and no seeds here: they are in each game's log (`GET /games/{game_id}`).

### GET /games/{game_id}

A game of a challenge or of a bot programming, optimization or code golf puzzle: a battle, or the user's own
test game, as a text (`text/plain`) or JSON log. The validators of optimization submissions are
never available, nor the games of arcade puzzles and Clash of Code.

| Query | Description |
|---|---|
| `format` | `text` (default) or `json`. |

Errors:
- 404 `GAME_NOT_FOUND`: not a game of a challenge or of a bot programming, optimization or code golf puzzle,
  or another player's test game.
- 403 `forbidden`, `code` `REPLAYS_DISABLED`: replays are disabled in this challenge and the user did
  not play in the game.
- 422 `request-rejected`, `code` `GAMERESULT_NOT_AVAILABLE`: the game is still running.
- 422 `request-rejected`, `code` `UNAUTHORIZED`: a puzzle game the website does not show the user,
  e.g. a validator of an optimization puzzle.

### GET /limits?challenge={public_id} or GET /limits?puzzle={pretty_id}

Without `challenge` nor `puzzle`, the response only has `limits.requests`. With `challenge`:
registered. With `puzzle`: a bot programming, optimization or code golf puzzle. Not both.

```json
{
  "challenge": "spring-challenge-2026",
  "submits": [{"name": "submits_5m", "limit": 5, "used": 5, "remaining": 0, "window_seconds": 300, "resets_in_seconds": 42}],
  "test_games": [...]
}
```

Plus `limits.requests` and the `RateLimit` headers. With `puzzle`, `puzzle` replaces `challenge`.

## Puzzles

Every visible puzzle of the website is listed. Bot programming, optimization and code golf puzzles are
played like challenges, without registration, dates or play windows. Arcade puzzles (classic puzzles)
are listed only: every puzzle endpoint except the list and the detail answers 403 `arcade-game` for
them. A puzzle whose
`challenge_public_id` is set is played as that contest: its session endpoints answer 409
`puzzle-in-contest`.

States used below:
- **playable**: a bot programming, optimization or code golf puzzle not played as a contest, else 403
  `arcade-game` or 409 `puzzle-in-contest`. The first call opens the user's puzzle session, like
  opening the puzzle in the IDE.
- **bot programming**: else 409 `unsupported-game-mode`.

### GET /puzzles

No token needed; with a token, each puzzle also has the user's progress. Newest first.

| Query | Description |
|---|---|
| `category` | `bot-programming`, `optimization`, `classic` or `code-golf`. |
| `arcade` | `false`: bot programming, optimization and code golf only; `true`: classic puzzles only. |
| `search` | Part of the title or of the `pretty_id`. |
| `played` | `true` or `false`: only the puzzles the user played, or not. Needs a token. |
| `offset` | 0 or more (default 0). |
| `limit` | 1 to 100 (default 20). |

```json
{
  "total": 90,
  "offset": 0,
  "limit": 20,
  "puzzles": [
    {"pretty_id": "coders-strike-back", "title": "Coders Strike Back", "category": "bot-programming", "arcade": false,
     "url": "https://www.codingame.com/multiplayer/bot-programming/coders-strike-back", "solved_count": 0,
     "rating": 4.62, "rating_count": 5120, "played": true, "submitted": true, "last_played_at": "2026-10-01T12:00:00Z"}
  ]
}
```

- `total`: puzzles matching the filters.
- `title`: in the language of the user's account, or of the request without token.
- `community`: `true` only on the puzzles created by players.
- `rating`: average rating of the players, 1 to 5; absent without rating.
- `challenge_public_id`: only on a puzzle played as a contest.
- `played`, `submitted`: only with a token. `score` (best score recorded for the user, 100 when
  solved) and `last_played_at`: only once played.

### GET /puzzles/{pretty_id}

No token needed. The members of the list, plus the rules for a playable puzzle with a token:

```json
{
  "pretty_id": "coders-strike-back",
  "title": "Coders Strike Back",
  "category": "bot-programming",
  "arcade": false,
  "url": "https://www.codingame.com/multiplayer/bot-programming/coders-strike-back",
  "league": "Wood 2",
  "players_min": 2,
  "players_max": 2,
  "languages": [{"id": "Python3", "name": "Python 3"}],
  "statement": "## Goal\n\n..."
}
```

- `league`: the user's league, in bot programming puzzles with leagues.
- `testcases`: in optimization and code golf puzzles, the test cases of the IDE, as
  `[{"index": 1, "title": "Small map"}]`: send an `index` as `testcase` to the test games.
- `next`: what to do instead, when the rules are absent (arcade puzzle, puzzle played as a contest,
  no token). Without token, an arcade puzzle still answers 200 with `next`.
- 404 `PUZZLE_NOT_FOUND`: no visible puzzle has this `pretty_id`.

### GET /puzzles/{pretty_id}/stub-generator, /code, /submissions, /submissions/{submission_id}

Playable. Same responses as the challenge endpoints of the same name.

### POST /puzzles/{pretty_id}/test-games

Scope `challenge:play`. Playable. Same query, responses and errors as
`POST /challenges/{public_id}/test-games`.

- Bot programming: the same body, with `opponents` from `GET /puzzles/{pretty_id}/opponents`, or a
  `replay_game_id` of the same puzzle.
- Optimization and code golf: `{"language": "Python3", "code": "...", "testcase": 1}`, plus
  `overwrite_ide_code`. `opponents`, `game_options`, `replay_game_id`, `show_in_ide` and
  `overwrite_ide_game_parameters` answer 400. An unknown `testcase` answers 400 `request-rejected`
  with `code` `INVALID_TESTCASE`.
- Test cases that are not games, which compare the output of the code with the expected one, as in
  code golf and some optimization puzzles, run while the request waits, one at a time, and answer 200 with their result. `wait` and
  `format` do not apply, there is no `test_game_id`, and the run counts in `limits.test_games`:

  ```json
  {"status": "done", "testcase": 1, "success": false, "output": "debug line",
   "found": "42", "expected": "41", "limits": {...}}
  ```

  - `success`: the output of the code matched the expected output.
  - `output`: console output of the code, as in the IDE.
  - `found`, `expected`: the first line of the answer that differs and the expected one; absent when
    the answer matched, or when the code printed nothing there (`found`) or nothing was expected
    there (`expected`).
  - `error`: `{"message": ..., "line": ...}`, when the code failed to compile or crashed.
  - `timeout`: `true` when the code ran longer than the time limit.
  - 409 `test-game-running` without `test_game_id`: another request of the user is running a test
    case of this puzzle. Wait for its answer, then retry.

### POST /puzzles/{pretty_id}/submissions

Scope `challenge:submit`. Playable. Same body and response as `POST /challenges/{public_id}/submissions`,
with a `progress_url` under the puzzle. The submission shows in the history of the user's IDE of the
puzzle.

### GET /puzzles/{pretty_id}/progress

Playable. Same members as `GET /challenges/{public_id}/progress`, ranked in the leaderboard of the
puzzle. In optimization and code golf puzzles, the validators play each submission instead of ranking
games:

- `submission_id`: the last submission, also the `current` one of `GET /submissions`.
- `in_progress`: the validators of the last submission are still playing. Poll until `false`.
- `score`, `rank`: from the user's best submission, which may be an earlier one. In code golf, `rank`
  counts the players of every language, as on the puzzle page of the website.
- `criteria`: what the leaderboard optimizes, e.g. `Fuel`.
- `criteria_score`: the value of `criteria` reached by the user's best submission.

### GET /puzzles/{pretty_id}/opponents, /battles

Playable, bot programming. Same responses as the challenge endpoints of the same name.

### GET /puzzles/{pretty_id}/leaderboard

Not arcade. Same query and response as `GET /challenges/{public_id}/leaderboard`.

### GET /puzzles/{pretty_id}/stats

No token needed. Not arcade.

```json
{
  "players": 85000,
  "players_by_language": {"Python3": 21000, "C++": 18000},
  "players_by_league": [{"league": "Legend", "players": 900}, {"league": "Gold", "players": 4000}],
  "solved_count": 0,
  "rating": 4.62,
  "rating_count": 5120
}
```

- `players`: players ranked in the leaderboard. `players_by_league`: only in puzzles with leagues,
  from the top league.

## Profile, quests, squads and teams

### GET /me

```json
{"nickname": "Alice"}
```

The nickname of the user, as shown on leaderboards, battles and logs.

### PUT /me/nickname

Scope `challenge:play`. Body `{"nickname": "Alice_42"}`. Changes the nickname of the user's
CodinGame profile, everywhere. Returns the new profile. 422 `nickname-rejected` when it is taken by
another player (ignoring case), longer than 32 characters, or uses characters other than letters,
digits and `_[]-.~`.

### GET /me/quests

The quests the user has unlocked on the quest map of the website, with their progress:

```json
{
  "claim_url": "https://www.codingame.com/home",
  "quests": [
    {"handle": "first-bot-league", "path": "Bot programming", "title": "Reach the Bronze league",
     "description": "Get promoted to Bronze in any bot programming game.", "progress": 1,
     "progress_max": 1, "completed": true, "claimed": false}
  ]
}
```

- `title` and `description`: in the language of the user's account.
- `completed`: the quest is done. `claimed`: the user has claimed its rewards.
- Locked quests are not in the list. Claiming a quest unlocks the next ones.
- The API cannot claim: the user claims the rewards of completed quests on `claim_url`.

### GET /challenges/{public_id}/squads

Squad challenges only (else 409 `not-a-squad-contest`).

| Query | Description |
|---|---|
| `search` | Part of the squad name. Omitted: every squad. |

```json
{
  "max_size": 4,
  "my_squad": {"squad_id": 12, "name": "The Bots", "member_count": 2, "members": ["Alice", "Bob"]},
  "squads": [{"squad_id": 12, "name": "The Bots", "member_count": 2, "members": ["Alice", "Bob"]}]
}
```

`my_squad` is `null` when the user is not registered or has no squad.

### POST /challenges/{public_id}/squads

Scope `challenge:play`. Body `{"name": "The Bots"}` (at most 31 characters). Creates the squad, or
returns the squad that already has this name, without joining it. 201 with
`{"squad_id": 12, "name": "The Bots", "member_count": 0}`.

### POST /challenges/{public_id}/squads/{squad_id}/join

Scope `challenge:play`. Registered. Moves the user to the squad and returns the squad list. 422
`request-rejected` with `code` `CANNOT_CHANGE_SQUAD` once the challenge has started, `SQUAD_TOO_FULL`
when the squad has `max_size` members.

### GET /challenges/{public_id}/teams

Registered, in challenges that rank the schools and companies of their players (else 409
`no-school-company-teams`). The teams for which the user's rank counts:

```json
{"teams": [{"type": "company", "name": "Acme", "rank": 12, "total_teams": 340}, {"type": "school", "name": "EPITA", "rank": 3, "total_teams": 120}]}
```

### PUT /challenges/{public_id}/teams

Scope `challenge:play`. Registered. Body members, each optional:

- `company`: a company name. Sets it as the company of the user's profile (created when unknown),
  as the website does, and counts the user's rank for it in this challenge. `null`: stop counting
  the user for a company in this challenge (the profile keeps its company).
- `school_id`: a school from `GET /schools`. Sets it as the school of the user's profile and counts
  the user's rank for it. `null`: stop counting the user for a school in this challenge.

An absent member changes nothing. Returns the teams as `GET` does. 404 `SCHOOL_NOT_FOUND`; 403
`forbidden` once the challenge has ended.

### GET /schools?search={name}

```json
{"schools": [{"school_id": 1234, "name": "EPITA", "city": "Paris", "country": "FR"}]}
```

## Game logs

### Text log

```
game 610230745
options: seed=12
    width=30
agents:
  0 Alice (me, agent_id -1)
  1 Boss 3 (agent_id 1234, rank 56)
frame 0
  referee: Game starts
frame 1 agent 0
  summary: Alice moves
  stderr: turn 1
      units=3
  stdout: MOVE 1 2
frame 2 agent 1
  summary: $1 moves
  stdout: MOVE 3 4
frame 3 agent 0
  referee: $0 has not provided 1 lines in time
  summary: $0: timeout!
  error: line 12: ValueError: too many values to unpack (expected 2)
result
  scores: 0=-1 1=1
  ranking (agent indexes, winner first): 1 0
  tooltips: frame 3: $0: timeout!
```

- `agents` lists every agent with its index, used by the frames, `scores` and `ranking`.
  `agent_id` -1 is the tested code, -2 the default bot; other ids are submitted agents.
- `frame N agent I`: the frame played by agent `I`. A frame where the user's agent printed nothing
  but has an `error` or `stderr` (crash, compilation error) also carries its index. Frames without
  agent come from the referee alone.
- `referee`: game information from the referee. `summary`: game summary of the frame.
- `$0`, `$1`... in `referee`, `summary` and `tooltips` stand for the agents of index 0, 1...
- `stderr`: only for the user's own agents, and only in test games (battles run without debug output). `stdout`: what the agent printed.
- `error`: the user's agent crashed or failed to compile, with the line of its code when known. A
  bot that does not answer in time is reported by the referee and summary lines of its frame, e.g.
  `$0 has not provided 1 lines in time`; other players' errors are not shown.
- `timeout`: rarely present; most timeouts only show in the referee and summary lines.
- `tooltips`: notable events, prefixed with their frame when known.
- Lines after the first one of a field are indented by 4 more spaces. Empty fields are omitted.

### JSON log (`format=json`)

```json
{
  "game_id": 610230745,
  "game_options": "seed=12\nwidth=30",
  "agents": [{"index": 0, "name": "Alice", "agent_id": -1, "rank": 56, "me": true}],
  "frames": [{"frame": 3, "agent": 0, "referee": "...", "summary": "...", "stderr": "...",
              "error": {"line": 12, "message": "..."}, "stdout": "MOVE 1 2"}],
  "scores": [-1.0, 1.0],
  "ranking": [1, 0],
  "fail_cause": "TIMEOUT",
  "tooltips": [{"frame": 3, "text": "$0: timeout!"}]
}
```

Members without a value are absent; trailing line breaks are removed.

## Documentation endpoints

No token needed; served as `text/markdown; charset=utf-8`.

| Path | Content |
|---|---|
| `GET /` | JSON index of the API and its documentation, with `challenges_url`, `puzzles_url`, `contributions_url`, and `game_modes`: each game mode of the website with its `id`, `name`, `arcade`, website `url` and `api_url` (where the API lists its games; absent for Clash of Code and Coding Escape). |
| `GET /docs` | Documentation index (same as `/llms.txt`). |
| `GET /docs/skill` | The Agent Skill (`SKILL.md`). |
| `GET /docs/leagues` | Leagues and promotion. |
| `GET /docs/api` | This reference. |
| `GET /docs/stub-generator` | Stub generator syntax guide. |
| `GET /docs/skills/codingame/SKILL.md`, `GET /docs/skills/codingame/references/api.md`, `GET /docs/skills/codingame/references/leagues.md` | The skill files, at paths matching the skill directory, also served under `skills/codingame-challenge/` (with `name: codingame-challenge`). `GET /docs/references/api.md` also serves this reference. |
| `GET /docs/skills/codingame-contribution/SKILL.md`, `.../references/api.md`, `.../references/classic.md`, `.../references/games.md` | The contribution skill: its endpoints under `/contributions`, and how to make a classic puzzle, a clash or a game. |
| `GET /problems/{slug}` | Description of a problem type. |
