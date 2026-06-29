# Shop files (`.shp`)

**Source files:** `src/shop.cpp` (`shop_data:36`, `boot_the_shops:586`,
`assign_the_shopkeepers:655`); loaded via `db.cpp index_boot(DB_BOOT_SHP)`
**Status:** ✅ format verified against the 23 real `.shp` files

## Purpose
Shops bind a shopkeeper mobile to a buy/sell inventory, pricing, hours, and a set of
canned messages. Files live in `lib/world/shp/`, listed by that directory's index file.
Unlike the other categories, shop loading does **not** pre-count records — `boot_the_shops`
`realloc`s as it goes (`index_boot:650` skips counting for `DB_BOOT_SHP`).

## Data structure — `struct shop_data` (`shop.cpp:36`)
`MAX_PROD = 5`, `MAX_TRADE = 5` (`shop.cpp:23-24`).

| Field | Meaning |
|-------|---------|
| `producing[5]` | item vnums the shop sells (always in stock); `0` = empty slot (code also accepts negatives, but no file uses them) |
| `profit_buy` | integer percent applied when the **player buys**: buy price = `cost * profit_buy / 100` (observed 70–500) |
| `profit_sell` | integer percent applied when the **player sells**: sell price = `cost * profit_sell / 100` (observed 0–100) |
| `type[5]` | item *types* the shop will buy/trade (byte; see object `type_flag`) |
| `no_such_item1/2`, `do_not_buy`, `missing_cash1/2`, `message_buy`, `message_sell` | canned keeper messages |
| `temper1` | keeper reaction when player lacks cash |
| `temper2` | keeper reaction when attacked |
| `keeper` | shopkeeper mob vnum (`assign_the_shopkeepers` sets `mob_index[].func = shop_keeper`) |
| `material` | bitvector of materials the shop buys; `0` = all |
| `in_room` | room vnum where the shop operates |
| `stock_room` | room vnum where goods are stored |
| `open1/close1/open2/close2` | two open/close hour windows (game hours) |

## Format — `boot_the_shops:594-652`
Per record, every numeric field is on its **own line** (one int per line), in order. The
parser uses `fscanf("%d")` so it is whitespace-agnostic, but the canonical on-disk layout
is one-per-line as shown:
```
#<vnum>~
<prod0>                                      (5 lines, one vnum each; items sold, 0 = empty slot)
<prod1>
<prod2>
<prod3>
<prod4>
<profit_buy>                                 (integer percent; buy price = cost * profit_buy / 100)
<profit_sell>                                (integer percent; sell price = cost * profit_sell / 100)
<type0>                                       (5 lines, one type each; tradeable item types)
<type1>
<type2>
<type3>
<type4>
<no_such_item1>~
<no_such_item2>~
<do_not_buy>~
<missing_cash1>~
<missing_cash2>~
<message_buy>~
<message_sell>~
<temper1>
<temper2>
<keeper>                                     (shopkeeper mob vnum)
<material>                                   (bitvector; 0 = all)
<in_room>
<stock_room>
<open1>
<close1>
<open2>
<close2>
```
The file ends with `$~` on its own line — the `$` is read by `fread_string`, so the trailing
`~` is required (`boot_the_shops:650`). Confirmed in every `.shp` file.
At assignment time, each `producing[]` item with vnum ≥ 0 is instantiated and placed in the
`stock_room` (`assign_the_shopkeepers:665-669`).

## RotS-specific notes
- `material` (materials the shop will buy) ties into the RotS object `material` field
  (see `world-files.md` object format).
- Two open/close windows (`open1/close1`, `open2/close2`) support split trading hours.
- Numbers/messages are otherwise close to CircleMUD's shop layout, but the exact field
  order above is RotS's and must be matched precisely.

## Real-data notes (23 `.shp` files)
- Header is `#<vnum>~` and the file terminator is `$~` (both confirmed in every file).
- Empty `producing[]`/`type[]` slots are written as `0`; no file uses negative values.
- `temper1`/`temper2` are `0` in every shop in the corpus.
- A few files place `$~` *before* the last record(s), so those trailing shops are dead — they
  are never loaded (`166.shp` `#16610`, `168.shp` `#16808`).
- `15.shp` shop `#1551` is malformed: it lists only three hour values before the next `#`
  header, so `close2` is read past the end of the record (latent data bug).

## Open questions
- The `temper*` codes (keeper reaction values) — to be pinned down in the Shops system doc
  from the buy/sell logic. (All real shops use `0`/`0`, so no live example exercises them.)
