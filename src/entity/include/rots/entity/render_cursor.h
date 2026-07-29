#ifndef ROTS_ENTITY_RENDER_CURSOR_H
#define ROTS_ENTITY_RENDER_CURSOR_H

// LS-3b Wave, T2 (ruling R-3b-B; .superpowers/sdd/ls3b-census-review.md's
// Table 1 / F15). The cursor-family API: a scoped guard reproducing the
// "location field as a view cursor" idiom (28 production sites across 10
// windows at HEAD) that today hand-rolls save/write/restore around
// ch->in_room so act()/room_of()/occupant walks *inside* a window render a
// different room than the character actually occupies.
//
// CONTRACT (binding, F15): this class is a byte-equivalent SUBSTITUTE for
// the manual `int saved = location_of(ch); set_location(ch, room); ...;
// set_location(ch, saved);` idiom it replaces -- nothing more. In
// particular:
//
//   * It REPRODUCES today's persistence-spoof hazard; it does not fix it.
//     Any read that happens inside the window -- location_of(ch),
//     room_of(ch), an occupant walk over the spoofed room, or a capture
//     into a persisted field such as char_data::specials.was_in_room or
//     the specials2.load_room stash -- sees the SPOOFED room, exactly as
//     it would with the raw field write. A caller that persists a location
//     read from inside a ScopedRenderLocation window persists the spoofed
//     value, deliberately and unchanged from today's behavior. Fixing that
//     hazard is explicitly OUT of this wave's scope (F15) -- it is a
//     property of the ten call sites, not of this class.
//   * IT CANNOT SPOOF NOWHERE OVER A LINKED CHARACTER (LS-3b T9b,
//     review-1 finding M-1). The constructor and retarget() refuse that one
//     write and log a tripwire, because it is the single argument value that
//     would falsify the location invariant's first half (placement.cpp's
//     banner) -- the half every absence guard and teardown path relies on.
//     Every production call site guards its own argument, so the refusal is
//     unreachable today; it exists so the banner's claim is mechanical.
//     Writing NOWHERE for a character who is already at NOWHERE is NOT a
//     spoof and is allowed unchanged.
//   * Room MEMBERSHIP never changes. Pre-swap (T2 runs before T5's store
//     split), set_location() is a bare field write with no room->people /
//     next_in_room bookkeeping, so this class inherits that: constructing,
//     retargeting, or destroying a cursor never touches a room's occupant
//     chain, never bumps light or zone-power counters, and never appends
//     or removes ch from anything. The character stays linked into its
//     real room's chain for the cursor's entire lifetime -- the torn state
//     (chain says the real room, the field says the spoofed one) is
//     deliberate and load-bearing here, exactly as census C's Idiom D
//     documents.
//   * No control-flow leak protection beyond RAII scope exit. The ten
//     production windows this class replaces were independently audited
//     (F15) to contain zero return/break/continue leaks under the manual
//     idiom -- this class's destructor-based restore is not "fixing a
//     latent leak class" (that census D claim is WITHDRAWN by the review),
//     it is simply the mechanical form scope-exit restoration takes in
//     C++. The one leak vector RAII genuinely adds robustness against is
//     an exception unwinding through the window; the tree does not throw
//     across any of these sites today.
//
// retarget(int room_id) supports a cursor written more than once inside a
// single window (ranger.cpp's do_whistle/do_scan zone-wide scans, fight.cpp's
// per-door death-cry broadcast) without re-capturing the original location a
// second time. restore() supports a window with more than one exit point
// (act_info.cpp's look-exam render restores on two branches); it is
// idempotent so a destructor firing after an already-executed restore() is a
// harmless no-op, not a second write.
//
// Non-copyable, non-movable: a copy or move would either double-restore (two
// objects racing to write ch_'s saved_location_ back) or leave one of the two
// objects holding a stale ch_ with no restore obligation -- neither has a
// sensible meaning for a single scoped write/restore pair, so both are
// deleted rather than given an ad-hoc semantics nobody needs.
//
// This class does NOT decide keying/representation (ruling R-3b-A: the
// store swap keeps a private handle inside char_data, landing in T5) --
// set_location()/location_of() stay exactly the thin field wrappers they
// are today; T5's internal reimplementation of those two functions is
// entirely transparent to this class and to every call site that
// constructs one.
namespace rots::entity {

class ScopedRenderLocation {
public:
    // Captures ch's current location (location_of(ch)) as the value this
    // guard restores, then writes room_id via set_location(ch, room_id) --
    // the same two operations the manual idiom performs, in the same
    // order.
    ScopedRenderLocation(char_data* ch, int room_id);

    // Restores ch to the captured location if restore() has not already
    // run; a no-op otherwise. Matches the manual idiom's unconditional
    // final set_location() call for every window that does not also call
    // restore() early.
    ~ScopedRenderLocation();

    ScopedRenderLocation(const ScopedRenderLocation&) = delete;
    ScopedRenderLocation& operator=(const ScopedRenderLocation&) = delete;
    ScopedRenderLocation(ScopedRenderLocation&&) = delete;
    ScopedRenderLocation& operator=(ScopedRenderLocation&&) = delete;

    // Re-points the cursor at a new room without touching the originally
    // captured restore value -- for windows that write more than one room
    // id over the guard's lifetime (a per-iteration zone scan, a per-door
    // broadcast). Unconditionally calls set_location(ch, room_id); it does
    // not compare room_id against the character's current location first
    // (there is no cheaper-but-equivalent short-circuit to take: pre-swap,
    // set_location() is a bare field write with no other observable side
    // effect, so calling it again with the same value it already holds is
    // indistinguishable from not calling it at all).
    void retarget(int room_id);

    // Restores ch to the captured location now, ahead of scope exit, for a
    // window with more than one exit path. Idempotent: calling restore()
    // again, or letting the destructor run afterward, performs no further
    // write.
    void restore();

private:
    // THE ABSENCE-INVARIANT PRECONDITION (LS-3b T9b; review-1 finding M-1).
    // Returns true -- and logs a tripwire -- when writing `room_id` would
    // put NOWHERE into the field of a character whose captured real location
    // is a real room, i.e. one the location invariant says is linked into
    // that room's occupant chain. The constructor and retarget() skip their
    // write in that case, keeping the real location. Writing NOWHERE for a
    // character who is ALREADY at NOWHERE is not a spoof and returns false
    // (fight.cpp's death_cry() opens exactly such a window); see the
    // implementation's comment in placement.cpp for the full derivation and
    // for why this is narrower than a blanket "reject NOWHERE".
    bool would_break_the_absence_invariant(int room_id) const;

    // The character this cursor spoofs; never null for the guard's
    // lifetime (set once at construction, read by every retarget()/
    // restore() call and by the destructor).
    char_data* ch_;

    // ch_'s real location, captured via location_of(ch_) at construction
    // time, before the first write -- the value restore()/the destructor
    // write back.
    int saved_location_;

    // Set true the first time restore() runs (explicitly, or implicitly
    // from the destructor); makes a second restore() call, or the
    // destructor firing after an explicit restore(), a no-op instead of a
    // redundant write.
    bool restored_;
};

} // namespace rots::entity

#endif
