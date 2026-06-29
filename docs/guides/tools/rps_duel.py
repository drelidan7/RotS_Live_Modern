import os
_REPO=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
import sys as _sys
_sys.path.insert(0,os.path.dirname(__file__))
"""Rock-paper-scissors duel sim: Heavy vs Light vs Wild in preferred stances.
Time-stepped (0.25s) mutual combat: auto-attacks (energy loop + wild rage),
armor-ignoring active skills (kick / wild-swing), riposte, defender block,
find-weakness, light double-strike, per-location armor."""
import sys, random, math
from wbis import *
from wbis import _armor_reduce, PARRY_EFF

OBJDIR=os.path.join(_REPO,'lib/world/obj')
RECS={r['vnum']:r for r in load_all(OBJDIR) if isinstance(r['vnum'],int)}
def I(v): return RECS[v]
PLATE=dict(BODY=6227,HEAD=6221,LEGS=6256,FEET=6246,HANDS=6244,ARMS=6254)
LEATHER=dict(BODY=11119,HEAD=11104,LEGS=11108,FEET=11120,HANDS=11116,ARMS=11124)
ACC=dict(ABOUT=6316,WAISTE=6040,WRIST_R=6647,WRIST_L=6667,NECK_1=6649,NECK_2=6954,HOLD=6955)
RINGS=dict(FINGER_R=6602,FINGER_L=6602)
def build(spec,W,R,strv,dex,con,armor,weapon,shield=None,tactics='NORMAL',race='HUMAN',grip2h=False):
    c=Char(spec=spec,W=W,R=R,strength=strv,dex=dex,con=con,level=30,tactics=tactics,race=race)
    eq={}
    for nm,v in armor.items(): eq[WEAR[nm]]=I(v)
    for nm,v in ACC.items(): eq[WEAR[nm]]=I(v)
    for nm,v in RINGS.items(): eq[WEAR[nm]]=I(v)
    eq[WEAR['WIELD']]=I(weapon)
    if shield: eq[WEAR['SHIELD']]=I(shield)
    c.equip=eq
    if grip2h: c._grip2h=True
    return c

def rage_mult(att, hpfrac):
    if att.spec!='WILD' or att.tactics!='BERSERK' or hpfrac>0.45: return 1.0
    # +15% at 45% HP up to ~+59% at 1% HP (linear)
    f=(0.45-max(hpfrac,0.01))/(0.45-0.01)
    return 1.0+0.15+0.44*f

def riposte_dmg(defn, atk, rng):
    w=defn.equip.get(WEAR['WIELD'])
    if not w or w['values'][2]>3 or defn.R<=0: return 0
    prob=(defn.k_riposte + defn.k_stealth + defn.e_dex()*5)*defn.R//200
    if rng.randint(0,99)<=prob:
        return weapon_damage(w)*min(defn.e_dex(),20)//rng.randint(50,100)
    return 0

def auto_swing(atk, defn, rng):
    """one auto-attack. returns (dmg_to_def, riposte_to_atk)."""
    OB=atk.get_real_OB(); DB=defn.get_real_dodge(); PB=int(defn.get_real_parry()*PARRY_EFF)
    w=atk.equip.get(WEAR['WIELD'])
    roll=rng.randint(1,35); ob=OB+rng.randint(1,55+OB//4)+roll; ob=ob*7//8-40
    crit=(roll==35)
    if crit: ob+=100
    ob-=DB
    if ob<0 and not crit: return (0,0)         # dodged
    ob-=PB
    if crit: ob=max(ob,0)
    if ob<0:                                    # parried -> riposte
        return (0, riposte_dmg(defn,atk,rng))
    # hit
    base=weapon_damage(w)+atk.points_damage()*10
    twoh=atk.is_two_handed(); bs=atk.e_bal_str()
    wtid=w['values'][3]; spear=(wtid==10)
    droll=rng.randint(0,100)
    if atk.spec=='WM' and wtid in (8,9) and rng.random()<0.5: droll=max(droll,rng.randint(1,100))
    F=10000+droll*droll+(2 if twoh else 1)*133*bs
    dam=base*(ob+100)*F//13300000
    # find weakness
    fw=(atk.k_extra//3)*atk.W//30+(atk.W-30 if atk.W>30 else 0)
    if rng.randint(0,99)<min(fw,100): dam+=dam//2
    # wild rush
    if atk.spec=='WILD':
        rp={'NORMAL':0.05,'AGGR':0.10,'BERSERK':0.15}.get(atk.tactics,0)
        if rng.random()<rp: dam+=dam//2
    # heavy weapon bonus
    if atk.spec=='HEAVY' and w['values'][2]>=3 and w['weight']>LIGHT_CUTOFF: dam+=dam//20
    # defender block (defense side)
    if defn.spec=='DEFENDER' and defn.equip.get(WEAR['SHIELD']):
        ch=max(defn.W,defn.R)+min(defn.W,defn.R)//2
        if rng.randint(0,100)<=min(100,ch): dam=int(dam*0.7)
    # wm flat damage
    if atk.spec=='WM' and wtid in (8,9,5): dam+=dam*15//100
    # armor (per location)
    dam=_armor_reduce(defn,dam,spear,atk)
    return (max(0,dam),0)

def skill_hit(atk, defn, kind, hpfrac, rng):
    """kick or wild-swing; armor-ignored (calls damage() directly in game)."""
    M=atk.k_kick - defn.get_real_dodge()//2 - int(defn.get_real_parry()*PARRY_EFF)//2 + atk.get_real_OB()//2 + rng.randint(1,100) - 120
    if M<0: return 0
    S=(2+atk.W)*(100+M)//250
    if kind=='wild':
        S=S*3//2
        if atk.tactics=='BERSERK' and hpfrac<=0.25: S=S*133//100
    if atk.spec=='HEAVY': S+=S//5
    return max(0,S)

def duel(A,B,nsim=3000,maxT=300.0):
    hpA0=A.max_hit(); hpB0=B.max_hit()
    eneA=A.ene_regen(); eneB=B.ene_regen()
    aw=0; bw=0; dr=0; ttks=[]
    for s in range(nsim):
        rng=random.Random(s)
        hpA=hpA0; hpB=hpB0; enA=0.0; enB=0.0; t=0.0
        nsA=4.0+rng.random()*3; nsB=4.0+rng.random()*3
        while t<maxT and hpA>0 and hpB>0:
            t+=0.25
            enA+=eneA*rage_mult(A,hpA/hpA0); enB+=eneB*rage_mult(B,hpB/hpB0)
            if enA>=1200:
                enA-=1200
                d,rip=auto_swing(A,B,rng); hpB-=d; hpA-=rip
                if A.spec=='LIGHT' and A.light_weapon() and not A.is_two_handed() and rng.random()>=0.8:
                    d2,rip2=auto_swing(A,B,rng); hpB-=d2; hpA-=rip2
            if enB>=1200 and hpB>0:
                enB-=1200
                d,rip=auto_swing(B,A,rng); hpA-=d; hpB-=rip
                if B.spec=='LIGHT' and B.light_weapon() and not B.is_two_handed() and rng.random()>=0.8:
                    d2,rip2=auto_swing(B,A,rng); hpA-=d2; hpB-=rip2
            if t>=nsA and hpA>0 and hpB>0:
                hpB-=skill_hit(A,B,'wild' if A.spec=='WILD' else 'kick',hpA/hpA0,rng); nsA=t+4.0+rng.random()*3
            if t>=nsB and hpA>0 and hpB>0:
                hpA-=skill_hit(B,A,'wild' if B.spec=='WILD' else 'kick',hpB/hpB0,rng); nsB=t+4.0+rng.random()*3
        if hpA<=0 and hpB<=0: dr+=1
        elif hpB<=0: aw+=1; ttks.append(t)
        elif hpA<=0: bw+=1; ttks.append(t)
        else: dr+=1
    ttks.sort(); med=ttks[len(ttks)//2] if ttks else maxT
    return aw/nsim*100, bw/nsim*100, dr/nsim*100, med

# ---- duelists ----
HV2=build('HEAVY',36,6,22,14,20,PLATE,5226,grip2h=True,tactics='NORMAL')      # heavy 2H
HV1=build('HEAVY',36,6,22,14,20,PLATE,5044,shield=26806,tactics='NORMAL')      # heavy 1H+shield
LTc=build('LIGHT',30,21,18,22,18,LEATHER,5410,tactics='CAREFUL')              # light careful
LTn=build('LIGHT',30,21,18,22,18,LEATHER,5410,tactics='NORMAL')               # light normal
WLb=build('WILD',36,6,22,14,20,PLATE,5226,grip2h=True,tactics='BERSERK')      # wild berserk

LTd=build('LIGHT',30,21,18,22,18,LEATHER,5410,tactics='DEF')            # light defensive
DODGE=dict(BODY=6200,HEAD=6201,LEGS=6203,FEET=6205,HANDS=11116,ARMS=11124)
LTe=Char(spec='LIGHT',W=30,R=21,strength=18,dex=24,con=18,level=30,tactics='DEF',race='WOOD',k_stealth=100)
_eq={}
for nm,v in DODGE.items(): _eq[WEAR[nm]]=I(v)
for nm,v in ACC.items(): _eq[WEAR[nm]]=I(v)
for nm,v in RINGS.items(): _eq[WEAR[nm]]=I(v)
_eq[WEAR['WIELD']]=I(5410); LTe.equip=_eq   # Wood-Elf max-evasion light fighter

def line(name,A,B):
    aw,bw,dr,med=duel(A,B)
    tag='A wins' if aw>bw+5 else ('B wins' if bw>aw+5 else 'EVEN/stalemate')
    print(f"  {name:<46} A {aw:5.1f}% | B {bw:5.1f}% | draw {dr:5.1f}%  ({tag}, ~{med:.0f}s)")

print("HP/stats: HV2 OB%d DB%d HP%d | HV1 OB%d DB%d PB%d HP%d | LTc OB%d DB%d PB%d HP%d | WLb OB%d DB%d HP%d"%(
 HV2.get_real_OB(),HV2.get_real_dodge(),HV2.max_hit(),
 HV1.get_real_OB(),HV1.get_real_dodge(),HV1.get_real_parry(),HV1.max_hit(),
 LTc.get_real_OB(),LTc.get_real_dodge(),LTc.get_real_parry(),LTc.max_hit(),
 WLb.get_real_OB(),WLb.get_real_dodge(),WLb.max_hit()))
print("  LTd(defensive) OB%d DB%d PB%d HP%d | LTe(WoodElf maxEvade) OB%d DB%d PB%d HP%d"%(
 LTd.get_real_OB(),LTd.get_real_dodge(),LTd.get_real_parry(),LTd.max_hit(),
 LTe.get_real_OB(),LTe.get_real_dodge(),LTe.get_real_parry(),LTe.max_hit()))
print("\n=== INTENDED CYCLE  (want: Light>Heavy>Wild>Light) ===")
line("Light(careful)  vs  Heavy-2H(normal)", LTc, HV2)
line("Heavy-2H(normal)  vs  Wild(berserk)", HV2, WLb)
line("Wild(berserk)  vs  Light(careful)", WLb, LTc)
print("\n=== the three user hypotheses ===")
line("Wild(berserk)  vs  Light(careful)   [Wild>Light?]", WLb, LTc)
line("Heavy-2H(normal)  vs  Wild(berserk) [Heavy>Wild?]", HV2, WLb)
line("Heavy-1H+shield(norm) vs Wild(berserk)", HV1, WLb)
line("Light(careful)  vs  Heavy-2H(normal) [Light>Heavy?]", LTc, HV2)
line("Light(defensive) vs Heavy-2H(normal)", LTd, HV2)
line("Light(WoodElf max-evade) vs Heavy-2H", LTe, HV2)
line("Light(WoodElf max-evade) vs Heavy-1H+shield", LTe, HV1)
print("\n=== what each mechanic is worth (Light vs Heavy-2H) ===")
LTc_nr=build('LIGHT',30,21,18,22,18,LEATHER,5410,tactics='CAREFUL'); LTc_nr.k_riposte=0
line("Light(careful, NO riposte) vs Heavy-2H", LTc_nr, HV2)
HV2_nk=build('HEAVY',36,6,22,14,20,PLATE,5226,grip2h=True,tactics='NORMAL'); HV2_nk.k_kick=-9999
line("Light(careful) vs Heavy-2H(NO kick)", LTc, HV2_nk)
LTc_nk=build('LIGHT',30,21,18,22,18,LEATHER,5410,tactics='CAREFUL'); LTc_nk.k_kick=-9999
line("Light(careful, NO kick) vs Heavy-2H", LTc_nk, HV2)
