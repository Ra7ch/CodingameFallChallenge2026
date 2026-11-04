# Leagues and promotion

How players move from one league to the next in CodinGame challenges and bot programming puzzles
with leagues. A given game uses some of these systems, not necessarily all: the progress
(`GET /challenges/{public_id}/progress` or `GET /puzzles/{pretty_id}/progress`) tells which one
applies to the user's current league in `promotion_mode`, `boss_battle_wins_required` and
`next_league_opens_at` (see the [API reference](https://www.codingame.com/api/v1/docs/api)).

## Leagues

- Leagues go from the lowest to the top: Wood (one or several), Bronze, Silver, Gold, Legend. The top
  league is not always Legend: a game can stop lower, e.g. at Gold. Everyone starts in the lowest
  open league.
- Every league except the top one has a boss: a bot copied into the league. A league can be split
  into rooms; each room has its own copy of the boss, and players are compared with the boss of
  their room.
- A promotion moves the user to the next league. The next league can have new rules (new inputs
  and actions): read the statement again after a promotion.
  - Same rules as before: the current submission is ranked again in the new league, with new
    battles.
  - New rules: the current submission stops playing; submit a bot for the new rules.
- A user marked `eligible_for_promotion` stays eligible until promoted, even if the submission's
  score drops afterwards or a weaker bot is submitted.

## Mode `rank_above_boss`

The usual mode, in Bronze and above.

1. After a submission, its ranking games are played against the players of the league room
   (`percentage` goes up to 100).
2. Once the submission has played all its ranking games, it becomes eligible when its score
   (the skill rating of the league ranking) is greater than or equal to the boss's. This is checked
   again after each of its ranked games, so a submission can become eligible later as its score moves.
3. Eligible players move up at the next promotion run of the league (see Promotion runs).

Beating the boss in test games is a good sign, but the ranking is computed against every player of
the room: the score has to reach the boss's.

## Mode `boss_battles`

Mostly in Wood leagues. Each league sets a number of battles N and a number of wins W
(`boss_battle_wins_required`), for example 5 and 3. The battles are listed in
`GET /challenges/{public_id}/battles` or `GET /puzzles/{pretty_id}/battles`.

1. After a submission, it plays N battles against the boss only.
2. A game counts as a win when the user's bot is alone in first place. Draws and shared first places
   do not count. In games played in every seat order, a battle is several games and each of them
   counts.
3. As soon as W games are won, the user becomes eligible and moves up at the next promotion run.
   The league rank and score do not matter.
4. If the battles end with fewer than W wins, the user stays in the league. Only a new submission
   plays new battles, and its wins are counted from zero.

Since every failed series costs a submission, play several test games against the `boss` opponent
first, with different `game_options` seeds, and submit when the bot wins most of them.

## Promotion runs

Promotions are not immediate. Each league has a promotion interval (often 1 minute, sometimes up to
hours). `next_promotion_in_seconds` in the progress is the time left before the next run.

- A run promotes the eligible players of the league, best score first, a couple every few seconds.
  Once none is left, the league waits a whole interval before the next run.

## League openings in public challenges

In public challenges, the top leagues (usually Silver, Gold and Legend) are closed at the start.

- A league without boss promotes nobody. Its next league opens when a boss is added to it, at a date
  set in advance: `next_league_opens_at` in the progress.
- At that date, a player of that same league is selected and a copy of their current submission
  becomes the new boss. The selected player stays in their league.
- The players whose score is above the new boss's become eligible within seconds and move up through
  the promotion runs. The others have to rank above the boss as usual.
