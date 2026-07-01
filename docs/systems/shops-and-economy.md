# Shops and economy

**Source files:** shop runtime `src/shop.cpp` (`shop_data:36`, `is_ok:128`, `trade_with:183`,
`shop_producing:199`, `has_already:212`, `shopping_buy:225`, `shopping_sell:318`,
`shopping_value:389`, `shopping_list:426`, `SPECIAL(shop_keeper):494`, `boot_the_shops:586`,
`assign_the_shopkeepers:655`); money `src/handler.cpp` (`create_money:2220`, `money_message:2407`),
`src/structs.h:103-104` (`COPP_IN_GOLD`/`COPP_IN_SILV`), `src/utils.h:398` (`GET_GOLD`); group
split `src/act_othe.cpp` (`calculate_gold_amount:639`, `give_share:675`, `do_split:682`); "bank"
stub commands `src/interpre.cpp:437-439` mapped to `do_not_here` (`src/interpre.cpp:2010-2015`,
`src/act_othe.cpp:134`); rent/crash economics `src/objsave.cpp` (`cost_per_day:1687`,
`Crash_is_unrentable:1044`, `Crash_calculate_rent:1076`, `Crash_offer_rent:1262`,
`Crash_report_rent:1249`, `gen_receptionist:1361`, `Crash_rentsave:1177`, `Crash_idlesave:1127`,
`Crash_crashsave` sig `src/handler.h:144`); death/corpse economics `src/fight.cpp`
(`make_physical_corpse:685`, `move_gold:546`, `corpse_decay_time:571`, `remove_random_item:594`,
`move_wearables_to_corpse:673`, `raw_kill:877`, `spirit_death:773`, `SPECIAL_DAMAGE` call sites
`:1643-1654,2571-2582`); combat spec-proc dispatch `src/interpre.cpp` (`special:1605`,
`activate_char_special:1556`) and `src/interpre.h` (`SPECIAL`/`special_func` macros); allocator
`src/utility.cpp` (`create_function:1300`); corpse decay tick `src/limits.cpp:728-774`,
`src/config.cpp:30-32` (`max_npc_corpse_time`, `max_pc_corpse_time`, `LOOT_DECAY_TIME`); pulse
timing `src/structs.h:57-58,95` (`PULSE_ZONE`, `PULSE_MOBILE`, `SECS_PER_MUD_HOUR`) and
`src/comm.cpp:45,812-830` (`OPT_USEC`, heartbeat loop); mob starting gold `src/db.cpp:1333-1334`;
zone restocking `src/db.cpp` zone-command loader (format documented in `world-files.md`).
**Status:** 🟡 partial — shop pricing, money denominations, rent, and death/corpse economics are
verified against live code and data, including a combat-engine trace showing shopkeepers cannot
take damage via the standard melee path (see Open questions); per-shop `bankAccount` persistence
and non-melee damage sources remain open questions.

> **Scope.** The `.shp` on-disk record layout is documented in
> [data-formats/shop-files.md](../data-formats/shop-files.md) — this doc does not repeat that
> grammar, only the runtime behavior built on top of it. Likewise the binary `.obj` crash/rent
> file layout is in [data-formats/object-rent-files.md](../data-formats/object-rent-files.md), and
> the player-file `gold` field is in [data-formats/player-save.md](../data-formats/player-save.md)
> (`gold <int>`, line ~100).

## Purpose
RotS has a single, flat currency (no player banking, no interest) that flows between players and
NPC shopkeepers through a small set of spec-proc-driven shop commands, and drains/regenerates
around three events: shop trading, group splitting, and death (which douses a shopkeeper's pocket
cash and dumps a dead player's/mob's carried money and gear into a lootable corpse). "Renting" — the
save-and-log-off mechanic — is nominally priced but the charge is hardcoded to zero, making
inn storage effectively free.

## Data structures

### Money
There is **one** money field per character, `char_point_data.gold` (`structs.h:1061`, accessed via
`GET_GOLD(ch)` — `utils.h:398`) — an `int`, **not** a struct of denominations. Money is
denominated for *display and shop math only* in copper, using
```c
#define COPP_IN_GOLD 1000   // structs.h:103
#define COPP_IN_SILV 100    // structs.h:104
```
i.e. `GET_GOLD` always holds a **raw copper count**; "gold" and "silver" are just
`amount / 1000` and `(amount % 1000) / 100` for display (`money_message`, `handler.cpp:2407`) or
input multipliers (`split <n> gold|silver|copper`, `drop <n> gold|silver`). Object cost fields
(`obj_flag_data.cost`, `structs.h:411`, comment says "gp." but is functionally copper — see
worked example) and `rent_info.gold` (`structs.h:1870`, see object-rent-files.md) use the same
copper-denominated integer.

Loose money is not a special data type — it's an ordinary `obj_data` with
`type_flag == ITEM_MONEY` (`structs.h:134`) and `value[0] == cost ==` the copper amount
(`create_money`, `handler.cpp:2220-2291`); picking it up (`act_obj1.cpp:171`) or destroying it
just manipulates that one object.

### Shop pricing fields (recap; full field list in shop-files.md)
| Field | Role in pricing |
|---|---|
| `producing[5]` | vnums always in stock, restocked on every sale (see below) |
| `profit_buy` | integer percent: **player-buy price = `item cost * profit_buy / 100`** |
| `profit_sell` | integer percent: **player-sell price = `item cost * profit_sell / 100`** |
| `type[5]` | item `type_flag`s the shop will accept when a player *sells* |
| `material` | bitvector of `obj_flags.material` values the shop will buy; `0` = all materials |
| `bankAccount` | **not on disk** — added to `shop_data` at `shop.cpp:56` as a runtime-only overflow pool (see "Shopkeeper cash" below); zeroed at boot (`boot_the_shops:647`), never saved |

## Format / Algorithm

### 1. Shop command dispatch (`SPECIAL(shop_keeper)`, `shop.cpp:494`)
Every shopkeeper mob has `mob_index[].func = shop_keeper` (`assign_the_shopkeepers:661`). The
spec-proc is invoked with different `callflag`s by the engine, and with `cmd` values (command IDs)
by the player interpreter:
- **`SPECIAL_DEATH`** — see "Keeper death" below.
- **`SPECIAL_SELF`** (fired once per `mobile_activity` pass, i.e. every `PULSE_MOBILE` = 24 pulses
  = **6 real seconds**, `structs.h:58`) — `GET_GOLD(keeper) += CASH_REGEN` where
  `CASH_REGEN = 100` copper (`shop.cpp:25,517`), i.e. **~1000 copper (1 gold) per real minute** of
  passive income per shopkeeper, independent of any sales.
- **`SPECIAL_DAMAGE`** — replies "Don't even think about it." via `do_tell` and returns `1`
  (`shop.cpp:520-531`). Traced through the combat engine (`fight.cpp:1643-1654`, `:2571-2582`):
  `special()`'s dispatch for this callflag runs the spec-proc of the **combat victim**, not the
  attacker (`tmpwtl.targ1.ptr.ch = victim` then `activate_char_special(tmpch=victim, ch=attacker,
  ...)`, `interpre.cpp:1654-1660`/`1556-1566`) — so `host` inside `shop_keeper` is the shopkeeper
  being attacked and `ch` is the attacker, matching the in-game flavor ("the shopkeeper tells you
  not to try it"). Both call sites gate on `victim->specials.fighting != attacker`, and returning
  `1` makes `hit()`/`damage()` bail out **before** the `set_fighting` calls that would establish
  that state (`fight.cpp:1672-1697`, `:2584-2596`) — so the block re-fires on every subsequent swing
  too. Net effect: **a shopkeeper cannot be damaged through the normal `hit`/`kill` melee path at
  all** (see Open questions for the residual uncertainty around non-melee damage sources).
- **`cmd == CMD_BUY/CMD_SELL/CMD_VALUE/CMD_LIST`**, gated on `ch->in_room == shop's in_room` —
  dispatch to `shopping_buy`/`shopping_sell`/`shopping_value`/`shopping_list`.
- Every dispatch first calls **`is_ok`** (`shop.cpp:128`), which refuses service if: the keeper is
  hostile to `ch` (`IS_AGGR_TO`), `ch` is a shadow, `ch` fails the shop's race filter
  (`RP_RACE_CHECK`, `utils.h:643` — an NPC keeper with no `rp_flag` serves everyone; one with an
  `rp_flag` bitmask only serves races in that mask), the shop is closed (see hours below), or the
  keeper can't see `ch` (invisibility).

### 2. Price formula
Both buy and sell use straight percentage multipliers of the item's `cost` field — **there is no
charisma, reputation, haggling-skill, or leadership-stat modifier anywhere in the shop code.**
```
buy_price  = (int)(item->obj_flags.cost * shop.profit_buy  / 100)   // shopping_buy, shop.cpp:261,296
sell_price = (int)(item->obj_flags.cost * shop.profit_sell / 100)   // shopping_sell, shop.cpp:351,363
```
`shopping_value` (the `value` command) previews the sell price with the identical formula
(`shop.cpp:420`) without completing a sale. `shopping_list` previews the buy price the same way
for every visible item in `stock_room` (`shop.cpp:449-458`). Integer truncation (`(int)` cast on a
division that's already integer) means fractional copper is simply dropped.

### 3. Item acceptance (selling to a shop)
`shopping_sell` (`shop.cpp:318`) requires, in order:
1. The item isn't alignment-locked against the keeper (`ITEM_ANTI_EVIL`/`ITEM_ANTI_GOOD` vs.
   `IS_EVIL`/`IS_GOOD(keeper)`) — refused with a headshake, no message variant used.
2. `trade_with(item, shop_nr)` (`shop.cpp:183`): `item->obj_flags.cost >= 1`, the shop's `material`
   bitvector is `0` **or** includes the item's material, and the item's `type_flag` appears
   somewhere in the shop's `type[5]` array. Otherwise the `do_not_buy` message fires.
3. The combined keeper cash pool (`GET_GOLD(keeper) + bankAccount`) must cover the sell price, or
   `missing_cash1` fires and the sale is refused (a keeper can run out of money).
4. On success, the sale draws first from `bankAccount` (all of it is folded back into
   `GET_GOLD(keeper)`, then the sell price is deducted) — `shop.cpp:365-369`.
5. The sold item is **destroyed** (`extract_obj`) if it's one of the shop's `producing[]` items, an
   `ITEM_TRASH`, or the stock room already holds a copy below level 20 (`has_already`,
   `shop.cpp:212-223` — prevents unbounded duplicate stockpiling); otherwise it's physically moved
   into `stock_room` and becomes purchasable by the next customer via `shopping_list`/`shopping_buy`.

### 4. Production items — the two restocking mechanisms
A shop's inventory in `stock_room` comes from **two independent sources**:
- **`producing[5]`** (from the `.shp` file): instantiated once at boot into `stock_room`
  (`assign_the_shopkeepers:665-669`) and — critically — **re-instantiated from the vnum every time
  one is bought** (`shop_producing` check + `read_object(temp1->item_number, REAL)` in
  `shopping_buy:308-309`) instead of being removed from the room. These slots are **unlimited
  stock** by construction; there are at most 5 per shop.
- **Zone reset `O` commands** (format in `world-files.md` §"Zone reset-command semantics"): e.g.
  zone 30's reset script drops a roster of named weapons straight into shop #3001's stock room
  (`lib/world/zon/30.zon:99-109`, `O 0 <vnum> 3058 0 <prob%> 1`). These are **ordinary,
  finite-stock objects** — once sold (and not a `producing[]`/trash/already-stocked duplicate) they
  are gone until the next zone reset re-rolls the probability check. This is how a shop can carry
  more variety than its 5 `producing` slots (Setharin's shop in the worked example below carries an
  extra, 50%-probability-loaded weapon, the "fine 2h axe", via an `O` command rather than
  `producing[]`).

### 5. Shop hours
`open1/close1`, `open2/close2` are two independent open/close hour pairs (`shop-files.md`). At the
top of every `shop_keeper` invocation with `cmd <= 0` (i.e. the periodic tick, not a player
command), the proc compares `time_info.hours` against these four values and calls
`opening_time`/`closing_time` (`shop.cpp:533-544`), which toggles the persistent `is_open` flag and
drives the keeper to actually open/close/lock the shop's doors (`shop.cpp:72-126`) and evict
patrons at closing. **`is_ok` only consults the `is_open` flag**, not the hour fields directly, when
deciding to serve a request mid-transaction (`shop.cpp:147-161`) — the hour fields there are used
only to pick which "come back later / we're closed / come back tomorrow" flavor message to show.
A shop whose `close1` equals `open2` (mod 24) never actually triggers a closing event
(`shop.cpp:539-540` requires the two to differ), so **coincident close1/open2 values make a shop
open 24/7** despite having "two windows" on paper (see worked example).

### 6. Messages / temper
The seven canned strings (`no_such_item1/2`, `do_not_buy`, `missing_cash1/2`, `message_buy`,
`message_sell`) are `printf`-style templates taking the customer's name and/or a
`money_message()`-formatted price (documented per-field in shop-files.md). `temper1` (reaction when
the *player* lacks cash, `shopping_buy:265-274`) and `temper2` (reaction when the keeper is
attacked, `shopping_kill:472-491`, currently **dead code** — the `CMD_KILL`/`CMD_HIT` dispatch that
would call it is commented out in `SPECIAL(shop_keeper)`, `shop.cpp:570-581`) select between two
hardcoded emote/action variants by integer code (`0` or `1`); every real `.shp` file uses `0` for
both (shop-files.md), so `temper2`'s alternate branch has never been exercised by live content and
`shopping_kill` itself is presently unreachable from the command table.

### 7. Shopkeeper cash & "bank"
A keeper's **visible** cash (`GET_GOLD(keeper)`) is capped at 15000 copper (15 gold): any purchase
that would push it over the cap sweeps the excess into `shop_index[shop_nr].bankAccount`
(`shopping_buy:300-305`) — a **process-memory-only** field with no `.shp` on-disk representation,
reset to `0` at every boot (`boot_the_shops:647`). This pool is drawn on automatically (and fully
drained back into `GET_GOLD`) the moment a player sells something the keeper can't otherwise afford
(`shopping_sell:365-369`), so in practice a shop's total liquidity is unbounded even though its
displayed cash never exceeds 15 gold. (A comment at `shop.cpp:301` notes the >15000 sweep was
originally meant to bound how much a killer could loot from a shopkeeper, "disabled because keepers
have so many HP now" — the sweep itself is still active, only a second, symmetrical sweep on the
sell path was commented out at `shop.cpp:371-376`.)

### 8. Keeper death
`raw_kill` (`fight.cpp:877`) calls the mob's spec-proc with `SPECIAL_DEATH` **before** building the
corpse (`shop.cpp:511-515`, invoked from `fight.cpp:891`): the shopkeeper's handler zeroes
`GET_GOLD(host)` outright with the flavor line "With $s last breath, $n bequeathes all $s money to
charity" — no object or character actually receives the money; it is simply deleted. Because this
runs before `make_physical_corpse`'s `move_gold` call, **the killer gets nothing from a
shopkeeper's pocket cash**, and because `bankAccount` is a separate field never touched by the
death handler, **the shop's accumulated bank survives the keeper's death** untouched (and is
reused when the mob respawns at the next zone reset, until the *process* restarts, at which point
it resets to `0` since it's never persisted).

### 9. Group money split (`do_split`, `act_othe.cpp:682`)
`split <amount> gold|silver|copper` expects a denomination keyword, but omitting/mistyping it does
**not** actually block the split: `calculate_gold_amount` (`act_othe.cpp:639-673`) sends "You must
specify what type of coin to split." (`:663`) in that case, but — since it doesn't reset `amount` to
`0` — still returns the raw, un-multiplied number the player typed (i.e. treats it as a bare copper
count instead of gold/silver), and `do_split` only bails out on a **genuinely zero/non-numeric**
amount (`total_gold_to_split == 0`, `:692`). So `split 100` (no denomination) prints the warning but
still splits 100 *copper* among the group. Given a valid denomination, `amount` is multiplied by
1000/100/1 for gold/silver/copper (`:651-661`) and divided among the group members present **in the
splitter's room** (`ch->group->get_pcs_in_room`, `act_othe.cpp:701`) by integer division; the
splitter keeps the remainder (`GET_GOLD(ch) -= share_amount * (share_count - 1)`, `act_othe.cpp:709`,
so their own receipt plus any rounding leftover). Refuses if `ch` isn't grouped, has insufficient
gold, or is alone in the room with the group.

### 10. No player banking
The command table lists `"balance"`, `"deposit"`, `"withdraw"` (`interpre.cpp:437-439`), but every
one of them is wired to `do_not_here` (`interpre.cpp:2010-2015`), whose entire body is
`send_to_char("Sorry, but you can't do that here!\n\r", ch)` (`act_othe.cpp:134`). **There is no
player-facing bank, interest, or off-person gold storage anywhere in this codebase** — a
character's entire liquid wealth is always the single `GET_GOLD` value they're carrying, at risk to
death looting (see below). The only "bank" concept that exists at all is the internal, per-shop
`bankAccount` overflow described above, which players cannot access, deposit into, or withdraw from.

### 11. Rent / crash-save economics
"Renting" via an inn receptionist (`gen_receptionist`, `objsave.cpp:1361`, `SPECIAL(receptionist)`)
computes a **cost estimate purely for display**: `Crash_offer_rent` (`objsave.cpp:1262`) sums a
per-item cost via `Crash_report_rent` into `totalcost` (seeded from `ch->player.level * factor`,
`:1281`), and the first message — "it will cost you N (for the first day)" (`:1298-1302`) — formats
that **real** `totalcost` through `money_message` before anything is zeroed. Immediately after,
`totalcost = 0;` (`objsave.cpp:1304`) discards it, and the *second* message ("you have enough gold
for N days/months/years/a lifetime", `:1316-1354`) is computed from `timeval`, which is derived from
the now-zeroed `totalcost` (`:1305-1308`): since `RENT_HALFTIME * 0 == 0`, the `GET_GOLD(ch) <
0`-equivalent branch is never taken and `timeval` is unconditionally set to `99999` — so **this
second message always reports "you have enough gold for a lifetime of rent," regardless of the
player's actual gold or item costs.** The function then zeroes `totalcost` again (`:1357`, a no-op
since it's already `0`) and returns `0`. The live rent path
(`gen_receptionist:1569-1570`, `/* we don't charge for rent; yet */ cost = 0;`) confirms this is
intentional/known-incomplete, not a bug: **inn rent is entirely free**. The only real gate on
renting is `Crash_report_unrentables` (`objsave.cpp:1230`) refusing the whole transaction (no
partial storage) if the player carries anything flagged `ITEM_NORENT`, with `cost_per_day < -1`, a
negative `item_number`, or `ITEM_KEY` (`Crash_is_unrentable:1044-1053`) — the player must drop
those first. `cost_per_day(obj)` itself (`objsave.cpp:1687-1690`) is
`(cost_per_day_field == -1 ? cost/100 : cost_per_day_field) / (level <= 5 ? 8 : 4)` — used only for
this dead pricing display and for `Crash_extract_expensive` (idle-timeout item eviction priority,
not currently called) and `Crash_calculate_rent` (also currently unused by any live save path).
Field-camping (the historical CircleMUD `rent` self-service command) is hardcoded disabled: `do_rent`
(`objsave.cpp:1620`) immediately `send_to_char`s "Field-rent is disabled. You have to go to an inn
now." and returns before reaching its own (dead) `Crash_rentsave(ch, 0)` call.

**What actually persists items:** any of `Crash_rentsave` (inn rent, `rentcode = RENT_RENTED`),
`Crash_crashsave` (normal quit/link-loss, default `rentcode = RENT_CRASH`, `handler.h:144`), or
`Crash_idlesave` (auto-idle timeout, `rentcode = RENT_TIMEDOUT`, `limits.cpp:565`) write the
player's full carried + worn item set to their binary `.obj` file (format:
object-rent-files.md) — **all three are free**; none deduct `GET_GOLD`. `Crash_idlesave`
additionally strips `ITEM_NORENT`/unrentable items outright before saving
(`Crash_extract_norents`, `objsave.cpp:1055-1063,1143-1146`) — those are lost, not stored, if a
player idles out while carrying them.

### 12. Death & corpse looting
Both PC and NPC deaths build a fully lootable corpse (`make_physical_corpse`, `fight.cpp:685`) —
**RotS does not exempt player corpses from looting**:
- All carried items (`character->carrying`) and all worn equipment
  (`unequip_char` per slot, `fight.cpp:743-747`) are moved into the corpse object.
- **Anti-hide rule:** if the kill was by another player or by poison
  (`attack_type == SPELL_POISON || !IS_NPC(killer)`), every wearable item and every key nested
  inside any container in the corpse is recursively pulled out into the corpse's top level
  (`move_wearables_to_corpse`/`parse_container`, `fight.cpp:673-680,651-667`) — a dead player (or
  poison victim) can't hide their gear from a PK looter by keeping it bagged.
- **Money:** `move_gold(character, corpse, 0)` (`fight.cpp:546-562`) converts the dead character's
  `GET_GOLD` into a single money object and, for `option == 0`, puts it *inside* the corpse if the
  dead character is an NPC **or** a still-connected player (`IS_NPC(ch) || (!IS_NPC(ch) &&
  ch->desc)`, `:553`) — the only case that drops it on the room floor **instead of** the corpse is a
  disconnected/linkless *player* character (`:556-559`). `GET_GOLD` is zeroed unconditionally
  afterward (`:561`).
- **Item-loss-on-death chance exists in code but is disabled**: `remove_random_item`
  (`fight.cpp:594-617`, a 1/25 chance per non-key/non-artifact/non-NORENT item) is present but its
  call site in `make_physical_corpse` is commented out (`fight.cpp:765`,
  `// remove_random_item(character, corpse); // To re-enable item decay, remove the comment`) —
  currently **no items are randomly destroyed on death**.
- **Corpse decay** (`corpse_decay_time`, `fight.cpp:571-586`, driven by `limits.cpp:728-774`, one
  tick per mud-hour = 60 real seconds, `structs.h:95`): NPC corpses decay after
  `max_npc_corpse_time = 10` ticks (**10 real minutes**; **+15** = 25 ticks if `MOB_ORC_FRIEND`,
  `config.cpp:30`), PC corpses after `max_pc_corpse_time = 45` ticks (**45 real minutes**,
  `config.cpp:31`). On decay the corpse's contents are **not destroyed** — they spill into the
  room (or the carrier, or the corpse's own containing object) and get a fresh, short
  `LOOT_DECAY_TIME = 5`-tick (5-minute) timer before finally being extracted for good
  (`limits.cpp:754-772`).
- A shadow-form death (`IS_SHADOW`) skips corpse creation entirely: `spirit_death`
  (`fight.cpp:773-778`) drops all items straight to the room floor (`remove_and_drop_object`), but
  **the character's gold is simply destroyed, not dropped**: `move_gold(character, NULL, 1)` passes
  `option == 1`, and `move_gold`'s money-object-creation branch is only reachable when `option == 0`
  (`fight.cpp:552-560`) — there is no `else` for non-zero `option`, so the function falls straight
  through to `GET_GOLD(ch) = 0;` (`:561`) without ever calling `create_money`. A shadow's carried
  gold therefore vanishes from the game entirely on death, with no corpse and nothing on the ground
  to loot.
- Post-death, a PC killed **by another player or poison** wakes with `hit = max_hit/4`, `mana = 0`;
  killed by anything else, all six abilities are cut to 2/3 and hit/move/mana reset to 1/0/0
  (`raw_kill:922-943`) — a survivability/economy penalty layered on top of the corpse loot, but not
  itself a gold transfer.

### 13. Other sinks / sources
No donation-room command, altar, or "pray for gold" mechanic exists in the codebase — `do_pray`
(`act_comm.cpp:775`) is a pure social emote with no `GET_GOLD` side effect, and a repo-wide search
found no `do_donate`/donation logic. Mobs carry a fixed starting `gold` value loaded straight from
their `.mob` file (`GET_GOLD(mob_proto+i) = tmp`, `db.cpp:1334`) which becomes lootable via the same
`move_gold` corpse path as players — ordinary mob kills are therefore the base gold *source* in the
economy, supplemented for shopkeepers specifically by the `CASH_REGEN` passive income in §7. The
economy's main unconditional gold *sink* besides shop markup is a shadow-form (`IS_SHADOW`) death:
per §12, `move_gold(character, NULL, 1)` destroys the dying character's gold outright rather than
dropping it, so any coin carried into a shadow death simply leaves the economy. No interest, tax, or
other passive drain/source was found.

## RotS-specific notes
- **Copper/silver/gold denomination on a single flat integer** (`COPP_IN_GOLD=1000`,
  `COPP_IN_SILV=100`) is a RotS/`money_message` presentation layer on top of what is otherwise
  stock CircleMUD's single `int gold` field — there's no separate "silver" or "copper" storage.
- **No charisma/reputation/haggling modifier in shop pricing** — buy/sell price is pure
  `cost * profit_{buy,sell} / 100`. (Confirmed absent, not merely undocumented — `shopping_buy`/
  `shopping_sell` reference no ability score at all.)
- **No player banking whatsoever** — `balance`/`deposit`/`withdraw` are stubbed to a generic "can't
  do that here" refusal. This differs from stock CircleMUD, which ships a working bank spec-proc;
  RotS appears to have removed/never finished it while leaving the command names reserved.
- **Rent is free in practice** despite a fully-computed, displayed cost estimate — the charge is
  hardcoded to `0` at the point of sale, and field-camping is disabled outright with a
  "go to an inn" message. This looks like an intentional simplification (or an unfinished
  monetization feature) rather than a bug, given the explicit `/* we don't charge for rent; yet */`
  comment.
- **Shops draw stock from two layers** — the `.shp` file's fixed, infinitely-restocking
  `producing[]` array, and ordinary zone-reset `O` object-load commands that place finite,
  probability-gated stock in the same room. A shop's apparent catalog size can therefore exceed the
  5-slot `producing[]` cap.
- **Player corpses are fully lootable**, including an explicit anti-hide mechanic
  (`move_wearables_to_corpse`) that defeats stashing gear in a bag specifically when the kill was
  PvP or poison — i.e. RotS deliberately makes PK looting harder to dodge than PvE looting.
- **Shopkeeper "bank" is a volatile, in-memory-only overflow**, not a `.shp`/save-file field; it is
  never written to disk and resets to zero on every reboot, yet is drawn on transparently during
  play as if it were part of the keeper's cash.
- **Race-gated commerce**: both shop service (`RP_RACE_CHECK` against a keeper's `rp_flag`) and
  several inn receptionists (`gen_receptionist:1411-1436` — Wood-Elf-only, Dwarf-only,
  Beorning-only, Olog-Hai-only, Haradrim-only rooms) restrict trade by race, beyond stock
  CircleMUD's alignment-only `ITEM_ANTI_EVIL`/`ITEM_ANTI_GOOD` checks (which RotS also keeps).

## Worked example
Real shop `#3001` (`lib/world/shp/30.shp:1-30`), the weaponsmith Setharin's shop in zone 30 (a real
production zone, not the zeroed-out zone-11 test data called out in shop-files.md):

```
#3001~
2001            producing[0] = key #2001 (see below)
5008            producing[1] = shortsword
5003            producing[2] = broadsword
5102            producing[3] = bastard sword
5103            producing[4] = two-handed sword
200             profit_buy  = 200%
70              profit_sell = 70%
5 0 0 0 0       type[0] = 5 (ITEM_WEAPON); rest empty
...             (canned messages, temper1=0, temper2=0)
3026            keeper = Setharin the Craftsman (dwarf, level 25)
0               material = 0 (accepts any material)
3036            in_room
3058            stock_room
0               open1
11              close1
11              open2
24              close2
```
- **Shop hours collapse to always-open.** `close1 (11) == open2 (11) mod 24`, so per §5 the
  `closing_time` transition never fires — the shop opens once at hour 0 and never closes.
- **`producing[0]` (item #2001) is a dead slot.** `lib/world/obj/20.obj` shows #2001 is
  `"an ornate silver and gold key"`, `type_flag = 18` (`ITEM_KEY`), **`cost = 0`**. Because
  `shopping_buy` treats `cost <= 0` as "no such item" and immediately `extract_obj`s the stocked
  copy (`shop.cpp:254-259`), this producing slot can never actually be bought — a live example of
  that code path (see Open questions).
- **A real weapon purchase.** `#5003`, the broadsword (`lib/world/obj/50.obj:51-66`), has
  `weight=380, cost=3000, cost_per_day=-1`. Buying it:
  `buy_price = 3000 * 200 / 100 = 6000` copper → `money_message(6000)` = **"6 gold"**.
  Selling it back: `sell_price = 3000 * 70 / 100 = 2100` copper = **"2 gold and 1 silver"**
  (2000 + 100; `1 silver = 100 copper`). The 200%/70% spread (130 copper margin per 1000 of cost)
  is the shop's markup.
- **A bigger-ticket item.** `#5102`, the bastard sword, `cost = 6750`: buy price
  `6750*200/100 = 13500` copper = **"13 gold and 5 silver"**; sell price `6750*70/100 = 4725`
  copper = **"4 gold, 7 silver and 25 copper"**.
- **The extra, non-`producing` stock.** Zone 30's reset script loads eleven more named weapons
  straight into `stock_room` 3058 via `O` commands (`lib/world/zon/30.zon:99-109`, 11 lines), e.g.
  `O 0 5212 3058 0 50 1` — object #5212 ("fine 2h axe") loads with **50% probability** per zone
  reset, alongside ten other weapons at 100%. These are ordinary finite-stock objects: once bought
  (and since #5212 isn't in `producing[]`), each copy is gone until the next reset re-rolls it.
- **The keeper's own cash.** Setharin (`lib/world/mob/30.mob:481-501`, mob vnum 3026) loads with
  `level=25`, starting `gold=600` copper (well under a single gold coin's worth relative to the
  items he sells) and regenerates `+100` copper every 6 real seconds while active (§7) — i.e. his
  visible cash climbs roughly 1 gold/minute purely from `SPECIAL_SELF` ticks, on top of whatever
  players pay him, until it crosses 15000 copper and starts flowing into the invisible
  `bankAccount` overflow.
- **Sanity-checked against a live player file**: `lib/players/A-E/achmed.30.18.1009978068.1740339546.64`
  (level 30) carries `gold 136913` — i.e. 136 gold, 9 silver, 13 copper — a plausible amount for a
  character who could afford several bastard-sword-tier purchases (13g 5s each) from a shop like
  Setharin's. `lib/players/A-E/achilleus...` (level 31) shows `gold 991` (9 silver, 91 copper),
  confirming the field really is stored as a raw sub-gold copper count, not a "gold pieces" integer.

## Open questions
- **Is a shopkeeper actually killable?** Traced through the combat engine (see §1): `special()`'s
  `SPECIAL_TARGET`/`SPECIAL_DAMAGE` routing runs the spec-proc of the character stored in
  `wtl->targ1.ptr.ch` — which both `fight.cpp` call sites (`:1643-1648`, `:2571-2576`) set to the
  **combat victim**, not the attacker — via `activate_char_special(tmpch, ch, ...)`
  (`interpre.cpp:1654-1660`) where `tmpch` (`= victim`) becomes the proc's `host` and `ch` (the
  caller's local `ch`/`attacker`) becomes its `ch`. So a shopkeeper's own `SPECIAL_DAMAGE` case fires
  on itself whenever it is attacked (consistent with the "Don't even think about it" line being
  addressed *to* the attacker), and both gates (`victim->specials.fighting != attacker`) sit
  **before** the `set_fighting` calls that would establish mutual combat state
  (`fight.cpp:1672-1697`, `:2584-2596`). Because the proc's `return 1` short-circuits `hit()`/
  `damage()` before those `set_fighting` calls run, the "not yet fighting this attacker" condition
  is never cleared, so the block re-fires on every subsequent swing from the same attacker too —
  **a shopkeeper cannot take damage via the standard `hit`/`kill` melee path.** Not traced here:
  whether any non-melee damage source (area spells, DoTs, or other code that calls `damage()`
  outside `fight.cpp:1643-1654`) can pre-establish `fighting` state and bypass this gate, which would
  make `SPECIAL_DEATH`/the keeper-death economics in §8 reachable through some other route.
- **`is_open`'s boot-time initialization order — resolved to a real, if narrow, bug.**
  `boot_the_shops` sets `shop_index[n].is_open = is_ok(0, 0, n)` (`shop.cpp:648`) — but `is_ok`
  itself reads `shop_index[shop_nr].is_open` (`shop.cpp:147`) as part of deciding the return value,
  so this is a read-before-first-write on every shop record. Whether that read is well-defined
  depends on *how the record's memory was obtained*, and the two shop-loading paths differ
  (`shop.cpp:599-605`): shop index `0` is allocated via `CREATE(shop_index, struct shop_data, 1)`,
  which routes through `create_function` → `calloc` (`utility.cpp:1300-1308`), so that first read is
  reliably `0`/"closed" (well-defined, if unintuitive). Every subsequent shop (`number_of_shops >
  0`), however, is grown via a **plain `realloc(shop_index, ...)`** call with no zeroing of the newly
  extended memory — so for shop index 1 and up, `is_ok`'s read of `is_open` at boot is a genuine read
  of **uninitialized heap memory**, not just "probably closed." In practice this only affects the
  single opening-hours check performed once at boot for shops #2 onward; the flag is overwritten
  right after by the assignment at `:648`, and steady-state behavior is governed by the
  `opening_time`/`closing_time` toggling in `SPECIAL(shop_keeper)` (`shop.cpp:533-544`) regardless of
  what that first read produced.
- **`temper2` / `shopping_kill`** is dead code (the `CMD_KILL`/`CMD_HIT`/`CMD_BASH`/`CMD_KICK`
  dispatch that would call it is commented out, `shop.cpp:570-581`), and every real `.shp` file
  uses `temper1=temper2=0` (shop-files.md), so neither temper branch's live behavior (nor whether
  `shopping_kill` would ever be reconnected) is exercised by any current content.
- **Per-shop `bankAccount` persistence.** It is explicitly never written to the `.shp` file or any
  save file and resets to `0` on every process restart. Given how central it is to keeping a
  high-traffic shop solvent (§7), it's unclear whether this volatility is an accepted design
  tradeoff or an unfinished feature (a rewrite should decide whether to persist it).
- **`Crash_calculate_rent`/`Crash_extract_expensive`** exist in `objsave.cpp` but no live save path
  calls them — likely vestiges of a once-working idle-item-eviction-by-value feature that was
  superseded by the current flat `Crash_extract_norents` (evict anything `NORENT`-flagged,
  regardless of value) on the idle-timeout path.
