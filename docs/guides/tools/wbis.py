"""
RotS warrior best-in-slot calculator.

Faithful re-implementation of the live combat math (src/utility.cpp, fight.cpp,
profs.cpp, char_utils.cpp, weapon_master_handler.cpp, handler.cpp) for evaluating
weapon/armor/shield gear for warrior archetypes.

All warrior archetypes assume weapon-skill knowledge = 100 (user addendum), plus
maxed parry/twohanded/attack/extra-damage/berserk knowledge. Character level = 30
(endgame; the level-scaling OB term and HP mini-level term saturate at 30).
"""
import sys, glob, math, random
from dataclasses import dataclass, field

random.seed(1234)

# ---- enums -----------------------------------------------------------------
ITEM = {1:'LIGHT',2:'SCROLL',3:'WAND',4:'STAFF',5:'WEAPON',6:'FIREWEAPON',7:'MISSILE',
8:'TREASURE',9:'ARMOR',10:'POTION',11:'WORN',12:'OTHER',13:'TRASH',14:'TRAP',
15:'CONTAINER',16:'NOTE',17:'DRINKCON',18:'KEY',19:'FOOD',20:'MONEY',21:'PEN',
22:'BOAT',23:'FOUNTAIN',24:'SHIELD',25:'LEVER'}
APPLY = {0:'NONE',1:'STR',2:'DEX',3:'INT',4:'WILL',5:'CON',6:'LEA',12:'MANA',13:'HIT',
14:'MOVE',17:'DODGE',18:'OB',19:'DAMROLL',20:'SAVE',21:'WILLPOWER',22:'REGEN',24:'SPEED',
25:'PERCEP',26:'ARMOR',29:'MANAREGEN',30:'RESIST',31:'VULN',32:'MAUL',33:'BEND',
38:'SPELLPEN',39:'SPELLPOW'}
# weapon type id -> (name, skill, is_two_handed, is_ranged)
WT = {2:('whip','WHIP',False,False),5:('flail','WHIP',False,False),3:('slash','SLASH',False,False),
4:('slash2h','SLASH',True,False),6:('bludgeon','CONCUSSION',False,False),
7:('bludgeon2h','CONCUSSION',True,False),8:('cleave','AXE',False,False),
9:('cleave2h','AXE',True,False),10:('spear','SPEARS',False,False),
11:('pierce','PIERCE',False,False),12:('smite','CONCUSSION',False,False),
13:('bow','ARCHERY',False,True),14:('crossbow','ARCHERY',False,True)}

# wear slot indices (structs.h)
WEAR = dict(LIGHT=0,FINGER_R=1,FINGER_L=2,NECK_1=3,NECK_2=4,BODY=5,HEAD=6,LEGS=7,FEET=8,
HANDS=9,ARMS=10,SHIELD=11,ABOUT=12,WAISTE=13,WRIST_R=14,WRIST_L=15,WIELD=16,HOLD=17,
BACK=18,BELT_1=19,BELT_2=20,BELT_3=21)
MAX_WEAR=22
# wear-flag bits (ITEM_WEAR_*) -> slot(s)
WEARBIT = {2:'FINGER',4:'NECK',8:'BODY',16:'HEAD',32:'LEGS',64:'FEET',128:'HANDS',
256:'ARMS',512:'SHIELD',1024:'ABOUT',2048:'WAISTE',4096:'WRIST',8192:'WIELD',16384:'HOLD'}

LIGHT_CUTOFF = 235  # LIGHT_WEAPON_WEIGHT_CUTOFF

# base encumbrance tables (consts.cpp)
ENCUMB_TBL     = [0,0,0,0,0,1,1,0,0,2,2,1,1,0,0,0,1,0,0,0,0,0]
LEG_ENCUMB_TBL = [0,0,0,0,0,1,0,2,1,0,0,1,0,0,0,0,0,0,0,0,0,0]
# heavy fighting (char_utils.cpp)
HF_ENCUMB = [0,0,0,0,0,2,2,2,2,2,2,3,0,0,0,0,3,0,0,0,0,0]
HF_WEIGHT = [0,0,0,0,0,975,325,650,350,400,650,500,100,0,0,0,250,0,0,0,0,0]
# light fighting
LF_ENCUMB = [0,0,0,0,0,1,1,1,1,1,1,2,0,0,0,0,2,0,0,0,0,0]
LF_WEIGHT = [0,0,0,0,0,225,225,225,225,225,225,500,50,0,0,0,165,0,0,0,0,0]

# ---- object parsing --------------------------------------------------------
def _read_tilde(lines,i):
    buf=[]
    while i < len(lines):
        l=lines[i]; i+=1
        if l.endswith('~'): buf.append(l[:-1]); break
        buf.append(l)
    return '\n'.join(buf).strip(), i

def _nextnb(lines,i):
    while i < len(lines) and lines[i].strip()=='' : i+=1
    return i

def parse_file(path):
    with open(path,errors='replace') as f:
        raw=f.read().replace('\r','')
    lines=raw.split('\n'); i=0; recs=[]
    while i < len(lines):
        if not lines[i].strip().startswith('#'):
            i+=1; continue
        vnum=lines[i].strip()[1:].strip()
        if vnum.startswith('99999') or vnum=='':
            break
        start=i; i+=1
        try:
            name,i=_read_tilde(lines,i); short,i=_read_tilde(lines,i)
            desc,i=_read_tilde(lines,i); act,i=_read_tilde(lines,i)
            i=_nextnb(lines,i); t=lines[i].split(); i+=1
            typ=int(t[0]); wearflags=int(t[2])
            i=_nextnb(lines,i); v=[int(x) for x in lines[i].split()[:5]]; i+=1
            i=_nextnb(lines,i); wl=lines[i].split(); i+=1; weight=int(wl[0]); cost=int(wl[1])
            i=_nextnb(lines,i); ll=lines[i].split(); i+=1; level=int(ll[0]); material=int(ll[2])
        except (ValueError,IndexError):
            # malformed/odd record - resync to next '#'
            i=start+1
            while i<len(lines) and not lines[i].strip().startswith('#'): i+=1
            continue
        affects=[]
        while i<len(lines):
            s=lines[i].strip()
            if s.startswith('#') or s.startswith('$'): break
            if s=='': i+=1; continue
            tk=lines[i].split()
            if tk[0]=='A' and len(tk)>=3:
                try: affects.append((int(tk[1]),int(tk[2])))
                except ValueError: pass
                i+=1
            elif tk[0]=='E':
                i+=1; _,i=_read_tilde(lines,i); _,i=_read_tilde(lines,i)
            else:
                break
        recs.append(dict(vnum=int(vnum) if vnum.isdigit() else vnum,name=short or name,
            typ=typ,type_name=ITEM.get(typ,str(typ)),wearflags=wearflags,values=v+[0]*(5-len(v)),
            weight=weight,cost=cost,level=level,material=material,affects=affects,file=path.split('/')[-1]))
    return recs

def load_all(objdir):
    recs=[]
    for p in sorted(glob.glob(objdir+'/*.obj')):
        recs+=parse_file(p)
    return recs

def wears(item, slotname):
    """does item fit in slot (by wear-flag bit)?"""
    for bit,nm in WEARBIT.items():
        if nm==slotname and (item['wearflags'] & bit): return True
    return False

def apply_sum(item, code):
    return sum(m for loc,m in item['affects'] if APPLY.get(loc)==code)

# ---- armor / weapon item math ----------------------------------------------
def weight_coof(item):
    if item['wearflags'] & 8: return 3   # body
    if item['wearflags'] & 256: return 2 # arms
    if item['wearflags'] & 32: return 2  # legs
    return 1

def armor_absorb(item):
    v=item['values']
    if v[0]==-1: return 0
    w=max(item['weight'],0)
    encumb_points = v[2]*6 + (w//weight_coof(item))//20
    points = item['level'] + encumb_points
    if encumb_points < 30:
        points += encumb_points*(60-encumb_points)//90
    if v[2]: points += 3
    absorb = points - v[1]*9
    if absorb < 0: absorb=0
    if absorb > 50: absorb = 100 - 2500//absorb
    return absorb

def isqrt200(x):
    # do_squareroot(x) == int(200*sqrt(x)) per docs
    return int(200*math.sqrt(max(x,0)))

def weapon_damage(item, owner_level=30, skill=100):
    """get_weapon_damage (returns dmg*10). owner skill=100 => full obj_level."""
    v=item['values']; wtid=v[3]
    if wtid in (13,14): return 0  # bows: separate path, none in world anyway
    parry_coef=v[1]; OB_coef=v[0]; bulk=v[2]; obj_level=item['level']
    # owner level/skill adjustments
    if owner_level is not None:
        if obj_level > owner_level*4//3+7:
            obj_level -= (obj_level - owner_level*4//3 - 7)*2//3
        obj_level = obj_level*skill//100
    # per-type tilt
    if wtid==2: parry_coef+=8; OB_coef-=5
    elif wtid in (3,4): parry_coef-=2
    elif wtid in (6,7): parry_coef+=3
    if parry_coef<-7: parry_coef=parry_coef//3-1
    elif parry_coef<0: parry_coef=parry_coef//2
    if parry_coef>5: parry_coef=parry_coef*2-5
    if OB_coef<-7: OB_coef=OB_coef//2-1
    elif OB_coef<0: OB_coef=OB_coef*2//3
    if OB_coef>40: OB_coef=40
    dam=(40+obj_level-parry_coef)*(50-OB_coef)*4//3
    dam=dam*(20-abs(bulk-3))//20
    w=max(item['weight'],1)
    str_speed=2*20*2500000//(w*(bulk+3))
    null_speed=225
    tmp=1000000//(1000000//str_speed + 1000000//(null_speed*null_speed))
    ene=isqrt200(tmp/100)//20
    if ene<1: ene=1
    dam=dam//ene*3
    if dam>70: dam=70+(dam-70)*3//4
    if dam>90: dam=90+(dam-90)*3//4
    return dam

# ---- character -------------------------------------------------------------
TAC = {'DEF':0,'CAREFUL':1,'NORMAL':2,'AGGR':3,'BERSERK':4}

@dataclass
class Char:
    name: str='ref'
    strength:int=22; dex:int=14; con:int=18; intel:int=10; wil:int=10; lea:int=12
    W:int=30; R:int=15; C:int=0; M:int=0    # prof (class) levels
    level:int=30
    race:str='HUMAN'
    spec:str='NONE'                          # NONE/WILD/HEAVY/LIGHT/WM/DEFENDER
    tactics:str='NORMAL'
    # combat knowledges (addendum: weapon skills 100; others maxed)
    k_weapon:int=100; k_parry:int=100; k_twohanded:int=100; k_attack:int=100
    k_extra:int=100; k_berserk:int=100; k_dodge:int=80; k_stealth:int=0; k_natural:int=0
    k_kick:int=100; k_swing:int=100; k_riposte:int=100; k_bash:int=100
    equip: dict=field(default_factory=dict)  # slot index -> item

    # ---- derived ability helpers
    def bal_str(self):
        cap=20 if self.race=='HOBBIT' else 22
        s=self.strength
        return s if s<=cap else cap+(s-cap)//2

    def is_two_handed(self):
        w=self.equip.get(WEAR['WIELD'])
        if not w: return False
        wt=w['values'][3]
        if wt in (4,7,9): return True            # inherently 2h types
        return getattr(self,'_grip2h',False)     # explicit twohand on a bulk>=4 weapon

    def light_weapon(self):
        w=self.equip.get(WEAR['WIELD'])
        if not w: return False
        b=w['values'][2]
        return b<=2 or (b==3 and w['weight']<=LIGHT_CUTOFF)

    # ---- gear point aggregates (from equip)
    def points_OB(self):
        ob=0
        for slot,it in self.equip.items():
            if it['type_name']=='WEAPON' and slot==WEAR['WIELD']: ob+=it['values'][0]
            ob+=apply_sum(it,'OB')
            if APPLY.get(33) and apply_sum(it,'BEND'): ob+=apply_sum(it,'BEND')
        return ob
    def points_parry(self):
        p=0
        for slot,it in self.equip.items():
            if slot==WEAR['WIELD'] and it['type_name']=='WEAPON': p+=it['values'][1]
            if slot==WEAR['SHIELD'] and it['type_name']=='SHIELD': p+=it['values'][1]
        return p
    def points_dodge(self):
        d=0
        for slot,it in self.equip.items():
            if it['type_name']=='ARMOR': d+=it['values'][3]
            if slot==WEAR['SHIELD'] and it['type_name']=='SHIELD': d+=it['values'][0]
            d+=apply_sum(it,'DODGE')
        return d
    def points_damage(self):
        return sum(apply_sum(it,'DAMROLL') for it in self.equip.values())
    def apply_stat(self,code):
        return sum(apply_sum(it,code) for it in self.equip.values())

    # effective stats incl gear applies
    def eff(self,base,code):
        return base+self.apply_stat(code)
    def e_str(self): return self.strength+self.apply_stat('STR')
    def e_dex(self): return self.dex+self.apply_stat('DEX')
    def e_con(self): return self.con+self.apply_stat('CON')
    def e_bal_str(self):
        cap=20 if self.race=='HOBBIT' else 22
        s=self.e_str()
        return s if s<=cap else cap+(s-cap)//2

    # ---- encumbrance
    def _worn_weight(self):
        tot=0
        for slot,it in self.equip.items():
            w=it['weight']
            if self.spec=='HEAVY' and HF_WEIGHT[slot]>0 and w>HF_WEIGHT[slot]:
                w=HF_WEIGHT[slot]+(w-HF_WEIGHT[slot])//3
            elif self.spec=='LIGHT':
                w=max(w-LF_WEIGHT[slot],0)
            tot+=w
        return tot
    def _encumb(self,table,heavy_cap,light_red):
        tot=0
        for slot,it in self.equip.items():
            if table[slot]<=0: continue
            e=it['values'][2]
            if self.spec=='HEAVY' and heavy_cap[slot]>0 and e>heavy_cap[slot]:
                e=heavy_cap[slot]
            elif self.spec=='LIGHT':
                e=max(e-light_red[slot],0)
            tot+=e*table[slot]
        return tot
    def _encumb_weight(self):
        tot=0
        for slot,it in self.equip.items():
            w=it['weight']
            if self.spec=='HEAVY' and HF_WEIGHT[slot]>0 and w>HF_WEIGHT[slot]:
                w=HF_WEIGHT[slot]+(w-HF_WEIGHT[slot])//3
            elif self.spec=='LIGHT':
                w=max(w-LF_WEIGHT[slot],0)
            if ENCUMB_TBL[slot]: tot+=w*ENCUMB_TBL[slot]
            else: tot+=w//2
        return tot
    def skill_penalty(self):
        enc=self._encumb(ENCUMB_TBL,HF_ENCUMB,LF_ENCUMB)
        ew=self._encumb_weight()
        bs=max(self.e_bal_str(),1)
        return (enc*25 + ew//bs)//50
    def dodge_penalty(self):
        leg=self._encumb(LEG_ENCUMB_TBL,HF_ENCUMB,LF_ENCUMB)
        ww=self._worn_weight()
        bs=max(self.e_bal_str(),1)
        return (leg*20 + ww//bs)//20

    # ---- core combat numbers (utility.cpp)
    def get_real_OB(self):
        w=self.equip.get(WEAR['WIELD'])
        warrior=self.W; maxwar=20 if self.race=='ORC' else 30
        off=self.e_bal_str()
        if self.spec=='LIGHT' and w and self.light_weapon():
            off=max(off,self.e_dex()); warrior+=self.R//3
        ob_bonus=(warrior*3 + 3*maxwar*self.level//30)//2 + off
        tmpob=self.points_OB() - self.skill_penalty()
        tmpob+=self._wm_bonus_OB()
        wsk=0
        if w:
            wsk=self.k_weapon
            if self.is_two_handed():
                tmpob+=w['values'][2]*(200+self.k_twohanded)//100 - 15
                wsk=(wsk+self.k_twohanded)//2
            else:
                tmpob-=(w['values'][2]*2 - 6)
        # tactics
        t=self.tactics
        if t=='DEF': tmpob+=ob_bonus-ob_bonus//4-8; tac=4
        elif t=='CAREFUL': tmpob+=ob_bonus-ob_bonus//8-4; tac=6
        elif t=='NORMAL': tmpob+=ob_bonus; tac=8
        elif t=='AGGR': tmpob+=ob_bonus+ob_bonus//16+2; tac=10
        elif t=='BERSERK': tmpob+=ob_bonus+ob_bonus//16+5+self.k_berserk//8; tac=10
        if w: tmpob+=wsk*(w['values'][2]+20)*tac//1000
        else: tmpob+=wsk*(self.e_str()+20)*tac//1000
        return tmpob

    def get_real_parry(self):
        w=self.equip.get(WEAR['WIELD'])
        bonus=self.W*2 + min(30,self.level) + self.e_bal_str()
        tmpparry=self.points_parry()+self._wm_bonus_PB()
        wbonus=0
        if w:
            wbonus=w['values'][1]
            tmpskill=self.k_weapon
            if self.is_two_handed():
                tmpskill=(tmpskill+self.k_twohanded)//2
        else:
            tmpskill=self.k_natural if self.k_natural else 0
            if self.k_natural==0:
                return tmpparry + bonus//2
        tmpskill=(tmpskill+3*self.k_parry)//4
        if self.tactics=='BERSERK': tmpskill//=2
        t=self.tactics
        if t=='DEF': tmpparry+=bonus//2+3*bonus//16; tac=4
        elif t=='CAREFUL': tmpparry+=bonus//2+bonus//8; tac=6
        elif t=='NORMAL': tmpparry+=bonus//2; tac=8
        elif t=='AGGR': tmpparry+=bonus//2-bonus//8; tac=10
        elif t=='BERSERK': tmpparry+=bonus//2-bonus//8; tac=12
        tmpparry+=tmpskill*(wbonus+20)*(14-tac)//1000
        if self.is_two_handed(): tmpparry+=wbonus//2
        return tmpparry

    def get_real_dodge(self):
        dodge=((self.k_dodge + self.k_stealth//2 + 60)*self.R//200
               + (self.k_dodge + self.k_stealth//4)//20)
        dodge-=self.dodge_penalty()
        dodge+=3
        if self.race=='BEORNING': dodge+=20
        if self.tactics=='BERSERK': dodge//=2
        gd=self.points_dodge(); dx=self.e_dex()
        t=self.tactics
        if t=='DEF': return dodge+gd+6+dx
        if t=='CAREFUL': return dodge+gd+4+dx
        if t=='NORMAL': return dodge+gd+dx
        if t=='AGGR': return dodge+gd-4+dx
        if t=='BERSERK': return dodge+gd-4+dx//2
        return dodge+gd+dx

    def ene_regen(self):
        w=self.equip.get(WEAR['WIELD'])
        if not w:
            return 60+5*self.e_dex()
        bulk=w['values'][2]; wt=max(w['weight'],1)
        null_speed=3*self.e_dex() + 2*(self.k_attack + self.k_stealth//2)//3 + 100
        str_speed=self.e_bal_str()*2500000//(wt*(bulk+3))
        if self.is_two_handed(): str_speed*=2
        if bulk<4:
            dex_speed=self.e_dex()*2500000//(wt*(bulk+3))
            str_speed=max(str_speed, str_speed*bulk//5 + dex_speed*(5-bulk)//5)
        if str_speed<1: str_speed=1
        tmp=1000000//(1000000//str_speed + 1000000//(null_speed*null_speed))
        ene=isqrt200(tmp/100)//20
        wtid=w['values'][3]; sk=WT.get(wtid,('','',0,0))[1]
        if self.race=='DWARF' and sk=='AXE': ene+=min(ene//10,10)
        elif self.race=='HARADRIM' and sk=='SPEARS': ene+=min(ene//20,20)
        if self.spec=='WM' and wtid in (11,2): ene=int(ene*1.15)  # piercing/whipping
        ene+=self.apply_stat('SPEED')
        return ene

    def speed(self):  # swings per minute
        return self.ene_regen()/5.0

    def class_HP(self):
        wp=(self.W/3.0)**2; rp=(self.R/3.0)**2; cp=(self.C/3.0)**2
        h=3*wp+2*rp+cp
        if self.race=='ORC': h=h*4.0/7.0
        return int(math.sqrt(h)*200)

    def max_hit(self, constHit=70):
        con=self.e_con()
        h=10+min(30,self.level)+constHit*con//20+(self.class_HP()*(con+20)//14)*min(3000,self.level*100)//100000
        if self.spec=='DEFENDER': h+=h//10
        # stealth reduction
        h=max(h-(self.k_stealth*self.level + self.k_stealth*3)//33,10)
        h+=self.apply_stat('HIT')
        return h

    # ---- weapon master / spec helpers
    def _wm_bonus_OB(self):
        if self.spec!='WM': return 0
        w=self.equip.get(WEAR['WIELD'])
        if not w: return 0
        wt=w['values'][3]
        if wt in (6,7,12): return 10
        if wt in (3,4): return 5
        return 0
    def _wm_bonus_PB(self):
        if self.spec!='WM': return 0
        w=self.equip.get(WEAR['WIELD'])
        if not w: return 0
        wt=w['values'][3]
        if wt==10: return 10
        if wt in (3,4): return 5
        return 0


# ---- per-swing Monte-Carlo damage -----------------------------------------
# humanoid hit-location distribution -> wear slot for armor_effect (approx; stated assumption)
HIT_DIST = [(WEAR['BODY'],0.32),(WEAR['LEGS'],0.20),(WEAR['ARMS'],0.16),
            (WEAR['HEAD'],0.12),(WEAR['HANDS'],0.10),(WEAR['FEET'],0.10)]

def _armor_reduce(defender, damage, w_type_spear, attacker):
    # pick a hit location
    r=random.random(); acc=0; slot=WEAR['BODY']
    for s,p in HIT_DIST:
        acc+=p
        if r<=acc: slot=s; break
    it=defender.equip.get(slot)
    if not it or it['type_name'] not in ('ARMOR','WORN'): return damage
    v=it['values']
    dr=v[1]
    divisor=150 if w_type_spear else 100
    dr += ((damage-dr)*armor_absorb(it)+50)//divisor
    if defender.spec=='HEAVY': dr+=dr//10
    return max(0, damage-dr)

PARRY_EFF=0.8   # parry decays to 2/3 after first parry within a round; ~0.8 avg over a round

def expected_swing(attacker, defender, samples=4000):
    """Returns (avg_damage_per_swing, hit_fraction)."""
    OBbase=attacker.get_real_OB()
    DB=defender.get_real_dodge()
    PB=int(defender.get_real_parry()*PARRY_EFF)
    wdmg= weapon_damage(attacker.equip[WEAR['WIELD']]) if attacker.equip.get(WEAR['WIELD']) else 0
    base0=wdmg + attacker.points_damage()*10
    twoh=attacker.is_two_handed()
    bs=attacker.e_bal_str()
    wtid=attacker.equip.get(WEAR['WIELD'],{'values':[0,0,0,0,0]})['values'][3]
    spear=(wtid==10)
    # find weakness chance
    fw=(attacker.k_extra//3)*attacker.W//30
    if attacker.W>30: fw+=attacker.W-30
    fw=min(fw,100)
    # rush (wild)
    rush_p={'NORMAL':0.05,'AGGR':0.10,'BERSERK':0.15}.get(attacker.tactics,0.0) if attacker.spec=='WILD' else 0.0
    # defender block
    dblock=0.0
    if defender.spec=='DEFENDER' and defender.equip.get(WEAR['SHIELD']):
        ch=max(defender.W,defender.R)+min(defender.W,defender.R)//2
        dblock=min(100,ch)/100.0
    heavy=(attacker.spec=='HEAVY')
    w=attacker.equip.get(WEAR['WIELD'])
    heavy_ok=heavy and w and w['values'][2]>=3 and w['weight']>LIGHT_CUTOFF
    wm_cleave=(attacker.spec=='WM' and wtid in (8,9))
    wm_flaildmg=(attacker.spec=='WM' and wtid in (8,9,5))
    tot=0.0; hits=0
    for _ in range(samples):
        roll=random.randint(1,35)
        ob=OBbase + random.randint(1,55+OBbase//4) + roll
        ob=ob*7//8 - 40
        crit=(roll==35)
        if crit: ob+=100
        # dodge
        ob-=DB
        if ob<0 and not crit: continue
        # parry (full effectiveness assumed; decays to 2/3 across a round - noted)
        ob-=PB
        if crit: ob=max(ob,0)
        if ob<0: continue
        hits+=1
        droll=random.randint(0,100)
        if wm_cleave and random.random()<0.50:
            droll=max(droll,random.randint(1,100))
        F=10000 + droll*droll + (2 if twoh else 1)*133*bs
        dam=base0*(ob+100)*F//13300000
        if random.random()<fw/100.0: dam+=dam//2
        if rush_p and random.random()<rush_p: dam+=dam//2
        if heavy_ok: dam+=dam//20
        # defender block (defense side)
        blk=0.0
        if dblock and random.random()<dblock: blk+=0.30
        if blk>0: dam=int(dam*(1-blk))
        if wm_flaildmg: dam+=dam*15//100
        dam=_armor_reduce(defender,dam,spear,attacker)
        tot+=max(0,dam)
    return tot/samples, hits/samples

def dps(attacker, defender, samples=4000):
    avg,hf=expected_swing(attacker,defender,samples)
    sw=attacker.speed()
    if attacker.spec=='LIGHT' and attacker.light_weapon() and not attacker.is_two_handed():
        sw*=1.20  # double-strike ~20%
    return avg*sw/60.0, avg, hf, sw  # damage per second

# ---- validation ------------------------------------------------------------
def validate():
    print("=== VALIDATION (reproduce docs anchors at knowledge 80) ===")
    c=Char(strength=20,dex=14,con=18,W=30,R=15,C=12,M=9,level=30,spec='NONE',
           k_weapon=80,k_parry=80,k_attack=80,k_twohanded=80,k_dodge=60,k_stealth=40,k_extra=80)
    # representative weapon: 1H bulk3 weight150 value1(parry)=20 value0=0
    wpn=dict(vnum=0,name='ref sword',typ=5,type_name='WEAPON',wearflags=8192,
             values=[0,20,3,3,0],weight=150,cost=0,level=0,material=0,affects=[],file='-')
    c.equip={WEAR['WIELD']:wpn}
    print(f"ob_bonus check: expect 110 ->", (30*3+3*30*30//30)//2+20)
    print(f"OB  (doc 124.7): {c.get_real_OB()}")
    print(f"PB  (doc 88.6 illustrative): {c.get_real_parry()}")
    print(f"DB  (doc 31.0): {c.get_real_dodge()}")
    print(f"class_HP (doc 3826): {c.class_HP()}")
    print(f"max_hit (doc ~416): {c.max_hit()}")
    print(f"ene_regen/speed (doc ~156 / 31): {c.ene_regen()} / {c.speed():.1f}")

if __name__=='__main__':
    validate()
