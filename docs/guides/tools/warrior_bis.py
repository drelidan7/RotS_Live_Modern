import os
_REPO=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
import sys as _sys
_sys.path.insert(0,os.path.dirname(__file__))
import sys, glob, math
from wbis import *
from wbis import _armor_reduce

OBJDIR=os.path.join(_REPO,'lib/world/obj')
MOBDIR=os.path.join(_REPO,'lib/world/mob')
RECS={r['vnum']:r for r in load_all(OBJDIR) if isinstance(r['vnum'],int)}
def I(vnum): return RECS[vnum]

# ---------- minimal mob parser for PvE reference targets ----------
def parse_mob(path):
    raw=open(path,errors='replace').read().replace('\r','')
    L=raw.split('\n'); i=0; out=[]
    def nb(i):
        while i<len(L) and L[i].strip()=='':i+=1
        return i
    def rt(i):
        while i<len(L):
            l=L[i];i+=1
            if l.endswith('~'):break
        return i
    while i<len(L):
        if not L[i].strip().startswith('#'): i+=1; continue
        v=L[i].strip()[1:].strip()
        if v.startswith('99999') or v=='':break
        st=i;i+=1
        try:
            for _ in range(4): i=rt(nb(i))     # 4 strings
            i=nb(i); hdr=L[i].split(); i+=1     # action affected align typeletter (ONE line)
            typ=hdr[-1] if hdr else 'N'
            if typ=='N':
                i=rt(nb(i)); i=rt(nb(i))        # two death-cry strings
            i=nb(i); s1=[int(x) for x in L[i].split()]; i+=1  # level ob parry dodge hpc hpm dam ene
        except (ValueError,IndexError):
            i=st+1
            while i<len(L) and not L[i].strip().startswith('#'): i+=1
            continue
        if len(s1)>=8:
            out.append(dict(vnum=int(v) if v.isdigit() else v,level=s1[0],ob=s1[1],parry=s1[2],
                            dodge=s1[3],hp=s1[5],dam=s1[6],ene=s1[7],file=path.split('/')[-1]))
        # resync to next '#'
        while i<len(L) and not L[i].strip().startswith('#'): i+=1
    return out

mobs=[]
for p in glob.glob(MOBDIR+'/*.mob'): mobs+=parse_mob(p)
melee=[m for m in mobs if 20<=m['level']<=35 and m['hp']>200 and m['dam']>0]
melee.sort(key=lambda m:-(m['ob']+m['dam']))
print("=== candidate PvE reference mobs (lvl20-35, hp>200) ===")
for m in melee[:8]:
    print(f"#{m['vnum']} L{m['level']} OB{m['ob']} parry{m['parry']} dodge{m['dodge']} HP{m['hp']} dam{m['dam']} ene{m['ene']} ({m['file']})")

# ---------- reference PvP duelist (solid, not BiS) ----------
def mob_target(m):
    """wrap a parsed mob as a 'defender-like' object for damage calc (stored OB/parry/dodge, no armor)."""
    c=Char(name=f"mob{m['vnum']}", strength=18, dex=14, W=0,R=0, level=m['level'], spec='NONE')
    c._mob=m
    return c

# Patch Char defender reads to use stored mob stats when _mob present
_orig_dodge=Char.get_real_dodge; _orig_parry=Char.get_real_parry; _orig_ob=Char.get_real_OB
def gd(self):
    if getattr(self,'_mob',None): return self._mob['dodge']+self._mob.get('_dummy',0)
    return _orig_dodge(self)
def gp(self):
    if getattr(self,'_mob',None): return self._mob['parry']
    return _orig_parry(self)
def go(self):
    if getattr(self,'_mob',None):
        m=self._mob; return m['ob']+15+m['level']//2  # NPC get_real_OB approx (bal_str folded into stored ob)
    return _orig_ob(self)
Char.get_real_dodge=gd; Char.get_real_parry=gp; Char.get_real_OB=go
def mob_speed(self):
    if getattr(self,'_mob',None): return self._mob['ene']/5.0
    return _orig_speed(self)
_orig_speed=Char.speed; Char.speed=mob_speed
def mob_hp(self,constHit=70):
    if getattr(self,'_mob',None): return self._mob['hp']
    return _orig_hp(self,constHit)
_orig_hp=Char.max_hit; Char.max_hit=mob_hp
def mob_pdam(self):
    if getattr(self,'_mob',None): return self._mob['dam']
    return _orig_pdam(self)
_orig_pdam=Char.points_damage; Char.points_damage=mob_pdam

# mob as attacker: damage = mob dam stat; use simple model (mob dam *10 base, OB vs my def)
def mob_dps_vs(mobchar, me, samples=3000):
    import random
    m=mobchar._mob
    OB=m['ob']+15+m['level']//2
    DB=me.get_real_dodge(); PB=me.get_real_parry()
    base=m['dam']*10  # mob weapon dam approx (mobs: dam stat ~ weapon rating/10)
    tot=0;hit=0
    for _ in range(samples):
        roll=random.randint(1,35); ob=OB+random.randint(1,55+OB//4)+roll; ob=ob*7//8-40
        crit=roll==35
        if crit: ob+=100
        ob-=DB
        if ob<0 and not crit: continue
        ob-=PB
        if crit: ob=max(ob,0)
        if ob<0: continue
        hit+=1
        F=10000+random.randint(0,100)**2+133*18
        dam=base*(ob+100)*F//13300000//2  # mobs have weapon dam halved
        # armor + block on me
        dam=_armor_reduce(me,dam,False,mobchar)
        if me.spec=='DEFENDER' and me.equip.get(WEAR['SHIELD']):
            ch=max(me.W,me.R)+min(me.W,me.R)//2
            if random.random()<min(100,ch)/100.0: dam=int(dam*0.7)
        tot+=max(0,dam)
    return tot/samples*mobchar.speed()/60.0

# Named end-game bosses (parsed from lib/world/mob) as PvE reference targets.
BOSS_STATS = {
 'Kraken':        dict(vnum=7706, level=30, ob=210, parry=60, dodge=30, hp=2300, dam=30, ene=160),
 'PaleLady':      dict(vnum=15302,level=67, ob=150, parry=20, dodge=20, hp=5000, dam=32, ene=202),
 'Nargul(balrog)':dict(vnum=1814, level=40, ob=250, parry=80, dodge=50, hp=3500, dam=21, ene=150),
 'cold-drake':    dict(vnum=2757, level=35, ob=200, parry=50, dodge=20, hp=5000, dam=27, ene=220),
 'snow-troll king':dict(vnum=25604,level=48, ob=152, parry=96, dodge=48, hp=3500, dam=23, ene=190),
}
BOSSES = {k:mob_target(v) for k,v in BOSS_STATS.items()}
PVE = {'bruiser':BOSSES['Kraken'], 'hitter':BOSSES['Nargul(balrog)']}

# reference PvP duelist: W33/R15 STR22 DEX16 CON20, chain + golden shield + broadsword, normal
def chain_set():
    return {WEAR['BODY']:I(11000),WEAR['HEAD']:I(11104),WEAR['LEGS']:I(11021),
            WEAR['FEET']:I(11120),WEAR['HANDS']:I(11116),WEAR['ARMS']:I(11124)}
REF=Char(name='RefDuelist',strength=22,dex=16,con=20,W=33,R=15,level=30,spec='NONE',tactics='NORMAL')
REF.equip=dict(chain_set()); REF.equip[WEAR['WIELD']]=I(5044); REF.equip[WEAR['SHIELD']]=I(6510)

def show(c, label):
    ob=c.get_real_OB(); pb=c.get_real_parry(); db=c.get_real_dodge(); hp=c.max_hit(); spd=c.speed()
    d_pvp,avg,hf,sw=dps(c,REF)
    line=f"{label:<34} OB{ob:>4} PB{pb:>4} DB{db:>4} HP{hp:>5} spd{spd:>4.0f}  PvP-dps{d_pvp:>6.1f}"
    ttk = REF.max_hit()/d_pvp if d_pvp>0 else 9999
    line+=f" TTK{ttk:>5.0f}s"
    # TTD vs ref attacker
    din,_,_,_=dps(REF,c)
    ttd=hp/din if din>0 else 9999
    line+=f" TTD{ttd:>5.0f}s"
    if PVE:
        dpe,_,_,_=dps(c,PVE['bruiser'])
        line+=f"  PvE-dps{dpe:>6.1f}"
    print(line)
    return dict(ob=ob,pb=pb,db=db,hp=hp,spd=spd,dps=d_pvp,ttk=ttk,ttd=ttd)

print("\n=== reference duelist (the PvP sparring target) ===")
show(REF,"RefDuelist W33/R15 chain+shield")
print(f"REF detail: OB{REF.get_real_OB()} PB{REF.get_real_parry()} DB{REF.get_real_dodge()} HP{REF.max_hit()} spd{REF.speed():.0f}")

# ============================================================================
#  ARCHETYPE ANALYSIS
# ============================================================================
PLATE   = dict(BODY=6227,HEAD=6221,LEGS=6256,FEET=6246,HANDS=6244,ARMS=6254)   # max absorb (heavy)
CHAIN   = dict(BODY=11000,HEAD=11104,LEGS=11021,FEET=11120,HANDS=11116,ARMS=11124) # mid (enc2-3)
LEATHER = dict(BODY=11119,HEAD=11104,LEGS=11108,FEET=11120,HANDS=11116,ARMS=11124) # light (enc1-2)
DODGEKIT= dict(BODY=6200,HEAD=6201,LEGS=6203,FEET=6205)                          # +dodge thin metal
ACC     = dict(ABOUT=6316,WAISTE=6040,WRIST_R=6647,WRIST_L=6667,NECK_1=6649,NECK_2=6954,HOLD=6955) # OB sticks
RINGS_R = dict(FINGER_R=6602,FINGER_L=6602)        # ivory OB+2 (realistic)
RINGS_A = dict(FINGER_R=5065,FINGER_L=1610)        # Vilya(OB150/DR20)+Narya(OB100/DR15) artifact ceiling

def build(spec,W,R,strv,dex,con,armor,weapon,shield=None,tactics='NORMAL',race='HUMAN',
          rings=RINGS_R,acc=True,grip2h=False,C=0):
    c=Char(spec=spec,W=W,R=R,strength=strv,dex=dex,con=con,level=30,tactics=tactics,race=race,C=C)
    eq={}
    for nm,v in armor.items(): eq[WEAR[nm]]=I(v)
    if acc:
        for nm,v in acc and ACC.items(): eq[WEAR[nm]]=I(v)
        for nm,v in rings.items(): eq[WEAR[nm]]=I(v)
    if weapon: eq[WEAR['WIELD']]=I(weapon)
    if shield: eq[WEAR['SHIELD']]=I(shield)
    c.equip=eq
    if grip2h: c._grip2h=True
    return c

def report(c,label):
    ob=c.get_real_OB();pb=c.get_real_parry();db=c.get_real_dodge();hp=c.max_hit();spd=c.speed()
    dpvp,avg,hf,sw=dps(c,REF); ttk=REF.max_hit()/dpvp if dpvp>0 else 9999
    din,_,_,_=dps(REF,c); ttd=hp/din if din>0 else 9999
    bru=PVE['bruiser']; dpe,_,_,_=dps(c,bru); ttk_e=bru.max_hit()/dpe if dpe>0 else 9999
    di_e=mob_dps_vs(bru,c); ttd_e=hp/di_e if di_e>0 else 9999
    print(f"{label:<40} OB{ob:>4} PB{pb:>4} DB{db:>4} HP{hp:>5} sp{spd:>3.0f} | PvP dps{dpvp:>5.1f} TTK{ttk:>4.0f} TTD{ttd:>4.0f} | PvE dps{dpe:>5.1f} TTK{ttk_e:>4.0f} TTD{ttd_e:>4.0f}")
    return dict(ob=ob,pb=pb,db=db,hp=hp,spd=spd,dpvp=dpvp,ttk=ttk,ttd=ttd,dpe=dpe,ttde=ttd_e)

print("\n############ 1. HEAVY FIGHTING: 1H+shield vs 2H (race-agnostic, 36w6r, STR22/DEX14/CON20) ############")
report(build('HEAVY',36,6,22,14,20,PLATE,5044,shield=26806),"Heavy 1H broadsword + numenorean shield(+HIT100)")
report(build('HEAVY',36,6,22,14,20,PLATE,5033,shield=26806),"Heavy 1H obsidian + numenorean shield")
report(build('HEAVY',36,6,22,14,20,PLATE,5346,shield=9080),"Heavy 1H smite-maul + heater shield")
for wv,wn in [(5226,'2H ice cleave d13 ob15'),(5224,'2H mithril greataxe d12'),(5320,'2H Durin sceptre d11.7 bulk8'),(5104,'2H ornate scimitar d10.2'),(5611,'2H iridescent flail d11.3')]:
    report(build('HEAVY',36,6,22,14,20,PLATE,wv,grip2h=True),f"Heavy {wn}")

print("\n############ 2. LIGHT FIGHTING weapon + armor (30w21r, DEX22/STR18/CON18) ############")
for wv,wn in [(5410,'rapier d5.1 ob6 pa7 b2'),(5435,'marble dagger d5.4 b1'),(5426,'sickle ob12 b1'),(5437,'silver dagger ob10 pa10 b1')]:
    report(build('LIGHT',30,21,18,22,18,LEATHER,wv),f"Light {wn} +leather")
report(build('LIGHT',30,21,18,22,18,DODGEKIT,5410),"Light rapier + DODGE thin-metal kit")
report(build('LIGHT',30,21,18,22,18,LEATHER,5410,rings=RINGS_A),"Light rapier +leather +ARTIFACT rings")

print("\n############ 3. WILD FIGHTING (36w6r, STR22) Berserk vs Normal ############")
report(build('WILD',36,6,22,14,20,PLATE,5226,grip2h=True,tactics='NORMAL'),"Wild 2H cleave NORMAL")
report(build('WILD',36,6,22,14,20,PLATE,5226,grip2h=True,tactics='BERSERK'),"Wild 2H cleave BERSERK")
report(build('WILD',36,6,22,14,20,PLATE,5044,tactics='BERSERK'),"Wild 1H broadsword BERSERK")

print("\n############ 4. WEAPON MASTER (36w6r) by weapon/proc ############")
report(build('WM',36,6,22,14,20,PLATE,5410),"WM pierce rapier (ignore armor)")
report(build('WM',36,6,22,14,20,PLATE,5226,grip2h=True),"WM 2H cleave (reroll +15%)")
report(build('WM',36,6,22,14,20,PLATE,5346,grip2h=True),"WM smite maul (+10OB haze)")
report(build('WM',36,6,22,14,20,PLATE,5044),"WM 1H slash (+5OB/PB)")
report(build('WM',36,6,22,14,20,PLATE,5514,grip2h=True),"WM spear (armor pierce)")

print("\n############ 5. DEFENDER (33w15r, CON22/STR20) shields ############")
for sh,sn in [(26806,'numenorean d15/p15 +HIT100'),(6510,'golden d15/p15'),(9080,'heater d8/p22'),(6530,'blackshield d10/p20')]:
    report(build('DEFENDER',33,15,20,14,22,PLATE,5044,shield=sh),f"Defender broadsword + {sn}")
report(build('DEFENDER',30,21,20,14,22,PLATE,5044,shield=26806),"Defender 30w21r (more block) + numenorean")

print("\n############ 6. CLASS-COMBO sensitivity (Light Fighting, rapier+leather) ############")
for W,R in [(36,6),(33,15),(30,21),(27,9),(20,9)]:
    report(build('LIGHT',W,R,18,22,18,LEATHER,5410),f"Light {W}w/{R}r")
print("   -- Defender block scales with min(W,R):")
for W,R in [(36,6),(33,15),(30,21),(27,9),(20,9)]:
    report(build('DEFENDER',W,R,20,14,22,PLATE,5044,shield=26806),f"Defender {W}w/{R}r")

print("\n############ 7. RACIAL BUCKETS (Heavy 2H cleave #5226, 36w6r base; spear set for Haradrim) ############")
report(build('HEAVY',36,6,22,14,20,PLATE,5226,grip2h=True,race='HUMAN'),"Light-side baseline (Human) 2H cleave")
report(build('HEAVY',36,6,22,14,24,PLATE,5226,grip2h=True,race='OLOGHAI'),"Olog-Hai (STR+4eff,CON+4) 2H cleave")
report(build('HEAVY',36,6,22,14,20,PLATE,5224,grip2h=True,race='DWARF'),"Dwarf + 2H mithril greataxe (axe regen)")
report(build('HEAVY',36,6,22,16,20,PLATE,5514,grip2h=True,race='HARADRIM'),"Haradrim + 2H spear (spear regen)")
report(build('HEAVY',36,6,22,14,20,PLATE,5226,grip2h=True,race='URUK'),"Uruk-Hai 2H cleave (no daylight)")
report(build('HEAVY',36,6,21,14,19,PLATE,5226,grip2h=True,race='ORC'),"Orc 2H cleave (maxwar20,no daylight)")

print("\n############ 8. PvE vs NAMED BOSSES — TTK (kill) / TTD (survive), seconds ############")
champs = {
 'Heavy-2H (ice cleave)':  build('HEAVY',36,6,22,14,20,PLATE,5226,grip2h=True),
 'Heavy-1H+shield':        build('HEAVY',36,6,22,14,20,PLATE,5044,shield=26806),
 'Wild (berserk 2H)':      build('WILD',36,6,22,14,20,PLATE,5226,grip2h=True,tactics='BERSERK'),
 'WeaponMaster (2H cleave)':build('WM',36,6,22,14,20,PLATE,5226,grip2h=True),
 'Light (rapier)':         build('LIGHT',30,21,18,22,18,LEATHER,5410),
 'Defender (numen.)':      build('DEFENDER',33,15,20,14,22,PLATE,5044,shield=26806),
}
hdr="%-26s"%"archetype"
for bn in BOSS_STATS: hdr+=f"| {bn[:13]:>13} "
print(hdr)
for label,c in champs.items():
    row="%-26s"%label
    for bn,boss in BOSSES.items():
        dpe,_,_,_=dps(c,boss); ttk=boss.max_hit()/dpe if dpe>0 else 9999
        di=mob_dps_vs(boss,c); ttd=c.max_hit()/di if di>0 else 9999
        row+=f"| {ttk:>5.0f}/{ttd:>4.0f}  " if ttk<9000 else f"|   inf/{ttd:>4.0f}  "
    print(row)
print("(cell = TTK / TTD in seconds; TTK=time to kill boss, TTD=time boss takes to kill you)")
