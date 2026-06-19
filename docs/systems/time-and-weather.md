# Time, seasons, day/night & weather

**Source files:** the whole model is `weather.cpp` (`weather_and_time`, `another_hour`,
`weather_change`, `get_season`, `get_sun_level`, `set_sun_info`, `initialize_weather`); state structs
`structs.h:999` (`time_info_data`) and `structs.h:1768` (`weather_data` = global `weather_info`);
constants `structs.h:1739-1766` (SUN_/SKY_/MOON_/SEASON_); the heartbeat that drives it
`comm.cpp:825-826`; boot init `db.cpp:457-462` (`reset_time` → `mud_time_passed`); calendar names
`consts.cpp:1735-1754`; `time`/`weather` commands `act_info.cpp:1925-2010`; light macros
`utils.h:237-243` (`IS_DARK`/`IS_LIGHT`/`IS_SUNLIT`), sun penalty `utils.h:425`.
**Status:** ✅ clock, calendar, sun cycle, moon, seasons, the pressure weather model, and every
gameplay effect (vision, Power-of-Arda combat penalty, weather-gated spells, tracking, mob/zone gates).

> **Cross-refs.** The combat side of daylight (the OB/parry/dodge magnitudes of Power of Arda, and
> which races suffer it) is [races.md §1](races.md) + [combat-loop.md](combat-loop.md); the
> sun/weather-gated **spells** (dark spells, Lightning Bolt/Strike) are [magic-system.md §7](magic-system.md).
> This doc owns the *clock and the weather engine* and points at those for the downstream numbers.

---

## 0. At a glance — the clock and its rates

A **MUD hour is 60 real seconds** (`SECS_PER_MUD_HOUR`, `structs.h:95`). The driver
`weather_and_time(1)` runs once per MUD hour — `comm.cpp:825` fires it every `SECS_PER_MUD_HOUR·4 =
240` pulses (= 60 s, since the pulse clock is 4/s). Each call advances the hour and rolls the weather.

| MUD unit | = | Real time |
|---|---|---|
| 1 hour | 60 s | **1 minute** |
| 1 day (24 h) | 1 440 s | **24 minutes** |
| 1 month (30 days) | 43 200 s | **12 hours** |
| 1 year (12 months / 360 days) | 518 400 s | **~6 days** |
| moon cycle (28 days) | 40 320 s | **~11.2 hours** |

**The clock doesn't drift and survives reboots — it's a pure function of real time.** At boot
`reset_time` sets `time_info = mud_time_passed(now, beginning_of_time)` (`db.cpp:461`), where
`beginning_of_time = 650336715` is a fixed Unix epoch (≈ Aug 1990, `db.cpp:116`); `mud_time_passed`
(`utility.cpp:1226`) just slices the elapsed real seconds into hours/days/moon/months/years. Runtime
`another_hour` then keeps incrementing in lockstep, so the two methods always agree. (One consequence:
the in-game **year** is `elapsed / ~6 days`, so it's a large, ever-climbing number.)

---

## 1. The calendar

`another_hour` (`weather.cpp:358`) is the odometer: `hours` 0–23 → `day` 0–29 (**30 days/month**) →
`month` 0–11 (**12 months/year**) → `year`; a separate `moon` counter ticks **0–27** once per day.

- **Weekdays** (7, `consts.cpp:1735`): the Quenya names **Elenya, Anarya, Isilya, Alduya, Menelya,
  Valanya, Earenya**. `time` computes `weekday = (30·month + day + 1) % 7` (`act_info.cpp:1940`).
- **Months** (`consts.cpp:1738`): the 12 Quenya month-names *Narvinië, Nénimë, Súlimë, Víressë,
  Lótessë, Nárië, Cermië, Urimë, Yavannië, Narquelië, Hísimë, Ringarë*. (The array actually holds 17
  strings — indices 12–16 are unused "Month of the Dark Shades / Shadows / …" names the 12-month
  cycle never reaches; see §8.)
- **Year** is printed as *"the Nth year of the Fourth Age of Arda, by the Steward's Reckoning"*
  (`act_info.cpp:1949`).
- **Character age** = `mud_time_passed(now, birth)` **+ 17 years** (everyone starts at 17,
  `utility.cpp:1254`).

---

## 2. The day/night (sun) cycle

`weather_info.sunlight` has four states (`structs.h:1739`): **`SUN_DARK`** (night), **`SUN_RISE`**
(the dawn hour), **`SUN_LIGHT`** (day), **`SUN_SET`** (the dusk hour). RISE and SET are one-hour
transitional states.

**Sunrise/sunset depend on the month** (`sun_events[12][2]`, `weather.cpp:25`), so **days are longer
in summer, shorter in winter**:

| Months | Sunrise | Sunset | Daylight |
|---|--:|--:|--:|
| 0, 1, 10 (deep winter) | 9 | 17 | ~8 h |
| 2, 9 | 7–8 | 18–19 | ~11 h |
| 3, 8 | 6–7 | 19–20 | ~13 h |
| 4, 7 | 5–6 | 20–21 | ~15 h |
| **5, 6 (high summer)** | **4–5** | **21–22** | **~17 h** |
| 11 | 9 | 16 | ~7 h |

Each hour `set_sun_info` (`weather.cpp:248`) compares `time_info.hours` to that month's rise/set:
hitting the rise hour → `SUN_RISE` + a sunrise message; hitting the set hour → `SUN_SET` + a sunset
message; the following hour `check_sun_change` promotes RISE→`SUN_LIGHT` (*"The day has now begun."*)
or SET→`SUN_DARK` (*"Night falls over the land."*), broadcast to all outdoor rooms.

**What the sun state gates** (details in the linked sections): room light (`IS_DARK`/`IS_LIGHT`/
`IS_SUNLIT`, `utils.h:237-243` — outdoor non-city rooms go dark at `SUN_DARK`); the evil-race combat
penalty (§6); dark- and lightning-spell bonuses (§6); gatekeeper mobs and sun-gated zone resets (§6).

---

## 3. The moon

The `moon` counter runs **0–27** (a 28-"day" cycle), and `another_hour` (`weather.cpp:394-413`)
derives:

- **Phase:** `moonphase = moon % 8` — the eight `MOON_*` steps **New → First-Quarter → First-Half →
  Second-Quarter → Full → Third-Quarter → Second-Half → Fourth-Quarter** (`structs.h:1753`).
  ⚠️ Because it's `% 8` over a 0–27 counter, the *phase* cycles every 8 days and is **not** synced to
  the 28-day `moon` counter — see §8.
- **Moonrise drifts later each day:** `moon_rise = (SUN_RISE + 24·moon/28) % 24`, and the moon stays
  up **12 hours** (`moon_rise` → `moon_rise + 12`). On rise it sets `moonlight = 1` and announces
  *"The \<phase\> moon shows in the sky."*; 12 hours later `moonlight = 0`.
- **A New moon gives no light:** while `moonphase == MOON_NEW` the moon is up but `moonlight` stays 0.

**Why it matters:** `moonlight` is the *only* thing that lets the moon-sighted races (**Wood-Elf,
High-Elf, Haradrim** — `AFF_MOONVISION`, races.md) see in an otherwise-dark outdoor room at night
(`CAN_SEE`, `utility.cpp:1457`: needs `OUTSIDE && AFF_MOONVISION && weather_info.moonlight`). No moon
up (or a new moon) → those races are as blind as anyone else without infravision or a light.

---

## 4. Seasons

`get_season` (`weather.cpp:263`) maps month → season: **Spring** 2–4, **Summer** 5–7, **Autumn** 8–10,
**Winter** 11/0/1. Season is not its own clock — it's a view of the month — but it changes real
behavior:

| Season | Pressure pull (→ weather, §5) | Snow? | Daylight (§2) | `weather` flavor |
|---|---|:-:|---|---|
| **Spring** | toward ~1000 mb | no | rising | "bursting with new life" |
| **Summer** | toward ~1015 mb (high → clearer) | no | **longest** | "long drawn-out days" |
| **Autumn** | toward ~995 mb (lower → stormier) | no | falling | "blustery storms" |
| **Winter** | toward ~980 mb (lowest → worst) | **yes** | **shortest** | "the land shivers" |

Only **Winter** can produce `SKY_SNOWING`/`SKY_BLIZZARD` (`weather.cpp:481,507,524`), and winter's
low pressure target makes storms the norm.

---

## 5. Weather — the pressure model

`weather_change` (`weather.cpp:428`) runs every MUD hour and is a barometric simulation. **Sky is
tracked per sector type** (`weather_info.sky[13]`), with `SECT_FIELD` as the benchmark everything else
derives from.

**Pressure walk.** Pressure is clamped to **[960, 1040] mb** and a momentum term `change` to
**[−12, 12]**. Each hour `change += dice(1,4)·diff + dice(2,6) − dice(2,6)`, where `diff` pushes
pressure toward the **season's target** (§4: +2 below target, −2/−3 above), then `pressure += change`.
So weather has inertia and trends toward stormy in autumn/winter, clear in summer.

**Sky state machine** (`SECT_FIELD`, `weather.cpp:471-520`): lower pressure walks the sky downhill,
higher pressure clears it:
```
CLOUDLESS ⇄ CLOUDY ⇄ RAINING ⇄ LIGHTNING        (+ CLOUDY→SNOWING, LIGHTNING→SNOWING in winter)
```
Roughly: `< 990` mb pushes toward clouds; `< 970` toward rain/snow; rain `< 970` can escalate to
**lightning** (a storm); high pressure (`> 1010/1030`) walks it back toward cloudy/clear.

**Per-sector derivation** (`weather.cpp:522-531`) — others are shifted off `SECT_FIELD`:
- **City** = field − 1 (milder; cloudless stays cloudless); **forest / road / water** = field;
- **Hills** = field (+1 sometimes in winter); **Mountains** = hills (+1 ~1/6 of the time, up to
  blizzard) — so high ground gets the worst weather;
- **Dense forest / swamp / crack** = city (milder).

**Snow on the ground** (`weather_info.snow[]`) accumulates **+1/hr** while a sector is SNOWING/BLIZZARD
and melts **−1/hr** otherwise (water sectors never accumulate, `weather.cpp:537-544`).

**Temperature** (`weather_info.temperature[]`) exists in the struct but is **not implemented** — the
code comment at `weather.cpp:534` says so explicitly (see §8).

Every hour the new conditions are broadcast to each sector via `send_to_sector` (and pushed over MSDP);
`weather` (`do_weather`) prints your sector's line plus the season blurb, and `weather_to_char` says
*"You can have no feeling about the weather here"* indoors.

---

## 6. What time & weather actually change (gameplay)

### Vision & light
At `SUN_DARK`, every outdoor room that isn't a city/inside sector becomes **dark** (`IS_DARK`,
`utils.h:237`), so you can't see without **infravision** (`AFF_INFRARED`), **moonvision +
moonlight** (§3), `HOLYLIGHT`, or a light source (`CAN_SEE`, `utility.cpp:1447-1539`). `get_sun_level`
(`weather.cpp:177`) grades how *brightly* an outdoor room is lit (base 8–10 by sector; **+4 snow on
ground, +2 cloudless, −1 cloudy-or-worse, ÷2 shadowy, ÷2 at dawn/dusk**) — this is the brightness that
feeds Power of Arda below.

### Combat — Power of Arda (evil races wilt in daylight)
While an evil race is **outdoors in sunlight**, `do_power_of_arda` (`limits.cpp:1196`) accumulates a
`SPELL_ARDA` affect at `get_sun_level(room)` per tick (so **brighter/snowier/clearer = faster
buildup**, storms slower), **×3 for Olog-Hai**, **exempt for Haradrim**; indoors/at night it decays.
The stored modifier becomes `sun_mod = modifier/25` (`utility.cpp:1596`), which docks **OB, parry and
dodge** (multipliers + flat `−sun_mod`) for Uruk/Orc/Magus/Olog and also **hurts spell concentration**
(`spell_pa.cpp:896`). The exact OB/parry/dodge fractions and the race table are in
[races.md §1](races.md) / [combat-loop.md]; here the point is that **time of day, weather, and snow
set how fast the penalty builds**.

### Spells — daylight and storms
- **Dark spells weaken in daylight** (the `SUN_PENALTY` macro, `utils.h:425`: Uruk/Orc/Olog/Magus,
  outdoors, `SUN_LIGHT`, not in a DARK/SHADOWY room). Dark Bolt / Searing Darkness lose their bonus
  term, and **Spear of Darkness is reduced to 0 base** in daylight (`mage.cpp`); magic-system §7.
- **Lightning spells reward the outdoors and storms.** Lightning Bolt gets its bonus term outdoors;
  **Lightning Strike** is an outdoor spell, and mob casters only *choose* it when the sky is actually
  `SKY_LIGHTNING` (`spec_pro.cpp:1717`, `mage.cpp` expose-elements) — i.e. the weather you documented
  in §5 gates which spells the AI throws.

### Tracking & footprints
Blood-trails and footprints age each hour by `sector_age_value` — **1** in clear/cloudy, **2** in
rain/lightning, **4** snowing, **5** blizzard — *unless* there's **snow on the ground**, in which case
they age only `rand(0,1)` (`weather.cpp:339,354`). Net: active **storms scrub tracks fast**, but a
**snow-covered ground preserves them** for trackers.

### Mobs, doors & zone resets
- **Gatekeeper** mobs open their gate during the day (`SUN_RISE`→`SUN_SET`) and **lock it at night**
  (`spec_pro.cpp:715,884`).
- **Zone resets** can be **sun-gated**: a `require_sun_up` reset is skipped while the sun is up (or
  down), so some mobs/objects only populate by day or only by night (`zone.cpp:439,461`).

### Display / MSDP
Sunrise/sunset/moonrise/weather changes broadcast to outdoor rooms and via MSDP
(`eMSDP_WORLD_TIME`, `eMDSP_WEATHER`); `time` and `weather` print the calendar, moon, sun-countdown
and sky.

---

## 7. Reading the `time` output

`time` (`act_info.cpp:1925`) decodes the clock for players: the 12-hour AM/PM hour, the Quenya
weekday, the "*Nth day of \<month\>*", the Fourth-Age year, the moon phase + whether it's *shining*,
and how many hours until the next sunrise/sunset (from this month's `sun_events`). Everything it shows
is derived from `time_info` + `weather_info`, i.e. ultimately from real time (§0).

---

## 8. Open questions / flags

- **`moonphase = moon % 8` over a 0–27 counter** (`weather.cpp:394`) makes the displayed phase cycle
  every 8 days and repeat ~3.5× per 28-day `moon` cycle — almost certainly meant to be `moon·8/28`
  (or `moon/3.5`). The moon *rise time* uses the proper `/28`, so the two are out of step.
- **`temperature[]` is declared but unimplemented** (`weather.cpp:534`: "should come into effect here,
  but is not implemented yet") — snow melt is purely tick-counted, with no temperature.
- **`initialize_weather`'s zero-init loop never runs:** `for (count = 1; count > 13; count++)`
  (`weather.cpp:606`) has a backwards condition, so the sky/snow/temperature arrays aren't cleared
  there. Harmless in practice (the global starts zeroed and `weather_change` repopulates every sector
  from `SECT_FIELD`), but it's a dead loop.
- **Stale comments:** `do_time` carries a `/* 35 days in a month */` comment while the code uses **30**
  (`another_hour` resets at `day > 29`); and `month_name[17]` keeps 5 unused "evil month" strings.
- **Daylight-length table is approximate** above; the authoritative per-month rise/set hours are
  `sun_events` (`weather.cpp:25`).
