import os
_REPO=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
import sys as _sys
_sys.path.insert(0,os.path.dirname(__file__))
import sys
from wbis import *

OBJDIR=os.path.join(_REPO,'lib/world/obj')
recs=load_all(OBJDIR)

BAD=['godly','immortal','test','gizmo','invisible','dummy','prototype','reserved',
     ' imm','(imm','staff ','builder','do not','aura of','an aura']
def realistic(it):
    n=it['name'].lower()
    if any(b in n for b in BAD): return False
    if it['typ']==5 and it['values'][3] not in WT: return False  # invalid weapon type
    return True

weapons=[r for r in recs if r['typ']==5 and realistic(r)]
armor=[r for r in recs if r['typ'] in (9,11) and realistic(r)]
shields=[r for r in recs if r['typ']==24 and realistic(r)]
print(f"counts: weapons={len(weapons)} armor/worn={len(armor)} shields={len(shields)}  (total recs {len(recs)})")

# ---- weapons by type, top by damage and by OB ----
print("\n===== WEAPONS: per-type leaders (dmg = get_weapon_damage @lvl30 skill100, /10) =====")
from collections import defaultdict
byt=defaultdict(list)
for w in weapons:
    byt[w['values'][3]].append(w)
for wtid in sorted(byt):
    nm,sk,twoh,rng=WT[wtid]
    lst=byt[wtid]
    for w in lst: w['_dmg']=weapon_damage(w)
    topd=sorted(lst,key=lambda x:-x['_dmg'])[:3]
    topob=sorted(lst,key=lambda x:-x['values'][0])[:2]
    print(f"\n-- type {wtid} {nm} (skill {sk}{' 2H' if twoh else ''}) n={len(lst)}")
    print("   by DMG: "+" | ".join(f"#{w['vnum']} {w['name'][:22]} d{w['_dmg']/10:.1f} ob{w['values'][0]} pa{w['values'][1]} bulk{w['values'][2]} wt{w['weight']} lvl{w['level']}" for w in topd))
    print("   by OB : "+" | ".join(f"#{w['vnum']} {w['name'][:22]} ob{w['values'][0]} pa{w['values'][1]} d{weapon_damage(w)/10:.1f} bulk{w['values'][2]} wt{w['weight']}" for w in topob))

# ---- armor per slot ----
SLOTS=[('BODY',8),('HEAD',16),('LEGS',32),('FEET',64),('HANDS',128),('ARMS',256),
       ('ABOUT',1024),('WAISTE',2048),('WRIST',4096),('NECK',4),('FINGER',2),('HOLD',16384)]
print("\n===== ARMOR: per-slot leaders =====")
for nm,bit in SLOTS:
    cand=[a for a in armor if a['wearflags']&bit]
    if not cand:
        print(f"\n-- {nm}: (none)"); continue
    for a in cand:
        a['_abs']=armor_absorb(a); a['_min']=a['values'][1]; a['_dod']=a['values'][3]
        a['_ob']=apply_sum(a,'OB'); a['_dr']=apply_sum(a,'DAMROLL')
        a['_str']=apply_sum(a,'STR'); a['_dx']=apply_sum(a,'DEX'); a['_cn']=apply_sum(a,'CON')
        a['_hp']=apply_sum(a,'HIT'); a['_dodapp']=apply_sum(a,'DODGE'); a['_spd']=apply_sum(a,'SPEED')
    topabs=sorted(cand,key=lambda x:-(x['_abs']+x['_min']))[:3]
    topoff=sorted(cand,key=lambda x:-(x['_ob']*1.0+x['_dr']*1.5+x['_str']))[:3]
    topdef=sorted(cand,key=lambda x:-(x['_dod']+x['_dodapp']+x['_abs']))[:2]
    print(f"\n-- {nm} (n={len(cand)})")
    def fmt(a): return f"#{a['vnum']} {a['name'][:20]} abs{a['_abs']}+{a['_min']} dod{a['_dod']+a['_dodapp']} ob{a['_ob']} dr{a['_dr']} st{a['_str']} dx{a['_dx']} cn{a['_cn']} hp{a['_hp']} spd{a['_spd']} wt{a['weight']} enc{a['values'][2]} lvl{a['level']}"
    print("   absorb: "+"\n           ".join(fmt(a) for a in topabs))
    print("   offense:"+"\n           ".join(fmt(a) for a in topoff))

# ---- shields ----
print("\n===== SHIELDS =====")
for s in sorted(shields,key=lambda x:-(x['values'][0]+x['values'][1])):
    print(f"#{s['vnum']} {s['name'][:24]} dodge(v0){s['values'][0]} parry(v1){s['values'][1]} ob{apply_sum(s,'OB')} dr{apply_sum(s,'DAMROLL')} wt{s['weight']} enc{s['values'][2]} lvl{s['level']}")
