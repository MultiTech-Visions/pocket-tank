import math, random
CELL=16
CH={0:'.',1:'-',2:'=',3:'*'}

class Art:
    def __init__(s,cw,ch):
        s.cw,s.ch=cw,ch; s.w,s.h=cw*CELL,ch*CELL
        s.g=[[0]*s.w for _ in range(s.h)]
    def put(s,x,y,v):
        x=int(x); y=int(y)
        if 0<=x<s.w and 0<=y<s.h and v>s.g[y][x]: s.g[y][x]=v
    def disc(s,cx,cy,r,v=2):
        for y in range(int(cy-r-1),int(cy+r+2)):
            for x in range(int(cx-r-1),int(cx+r+2)):
                d=math.hypot(x-cx,y-cy)
                if d<=r: s.put(x,y,v)
    def limb(s,x,y,ang,length,thick,rng,taper=0.86):
        """a branch that wobbles and thins - the whole point is that it is not straight"""
        for i in range(int(length)):
            ang+=rng.uniform(-0.22,0.22)
            x+=math.cos(ang); y+=math.sin(ang)
            t=max(0.8,thick*(taper**(i/6.0)))
            s.disc(x,y,t,2)
        return x,y,ang,max(0.8,thick*(taper**(length/6.0)))
    def shade(s):
        """outline the underside dark, catch the light on the upper left"""
        out=[row[:] for row in s.g]
        for y in range(s.h):
            for x in range(s.w):
                if not s.g[y][x]: continue
                nb=[(x+dx,y+dy) for dx,dy in ((1,0),(-1,0),(0,1),(0,-1))]
                edge=any(not(0<=a<s.w and 0<=b<s.h and s.g[b][a]) for a,b in nb)
                up = y==0 or not s.g[y-1][x]
                if edge and not up: out[y][x]=1
                elif up: out[y][x]=3
                elif (x==0 or not s.g[y][x-1]): out[y][x]=3
        # interior texture: sparse, and clustered rather than a polka dot -
        # a regular lattice of single bright pixels reads as spots, not coral
        for y in range(s.h):
            for x in range(s.w):
                if out[y][x]!=2: continue
                n=(x*2654435761 ^ y*40503) & 0xffff
                if n%37==0: out[y][x]=3
                elif n%53==0: out[y][x]=1
        s.g=out
    def rows(s): return [''.join(CH[v] for v in row) for row in s.g]
    def trim_bottom_to_floor(s): pass

def staghorn(seed):
    r=random.Random(seed); a=Art(2,3); base=a.h-1
    for k in range(3):
        x=a.w/2+r.uniform(-6,6)
        x,y,ang,t=a.limb(x,base,-math.pi/2+r.uniform(-0.3,0.3),r.randint(22,30),3.4,r)
        for _ in range(r.randint(2,3)):
            a.limb(x,y,ang+r.choice([-1,1])*r.uniform(0.5,1.0),r.randint(10,16),t,r)
        a.limb(x,y,ang+r.uniform(-0.2,0.2),r.randint(8,14),t,r)
    a.shade(); return a

def brain(seed):
    r=random.Random(seed); a=Art(2,2)
    cx,cy=a.w/2,a.h*0.62
    for i in range(14):
        ang=r.uniform(0,math.tau); d=r.uniform(0,9)
        a.disc(cx+math.cos(ang)*d, cy+math.sin(ang)*d*0.7, r.uniform(6,10))
    for i in range(6):                     # the grooves
        y=int(cy-8+i*3.2)
        for x in range(a.w):
            if a.g[y][x] and (x+i)%7<2: a.g[y][x]=0
    a.shade(); return a

def fan(seed):
    """a sea fan: ribs spreading from a short stem, webbed across, so the
    silhouette is a fan and not a sprig"""
    r=random.Random(seed); a=Art(2,2); base=a.h-1
    sx,sy,_,_=a.limb(a.w/2,base,-math.pi/2,6,2.6,r,taper=0.99)
    for i in range(11):
        ang=-math.pi/2+(i-5)*0.155
        x,y=sx,sy
        for k in range(r.randint(17,22)):
            ang+=r.uniform(-0.06,0.06)
            x+=math.cos(ang); y+=math.sin(ang)
            a.disc(x,y,1.5)
        if r.random()<0.5: a.limb(x,y,ang+r.choice([-1,1])*0.5,r.randint(3,6),1.2,r,taper=0.99)
    for ring in (7,12,17):                  # the webbing, following the spread
        for i in range(-11,12):
            ang=-math.pi/2+i*0.075
            a.disc(sx+math.cos(ang)*ring, sy+math.sin(ang)*ring, 1.0)
    a.shade(); return a

def tubes(seed):
    r=random.Random(seed); a=Art(2,2); base=a.h-1
    for i in range(5):
        x=4+i*6+r.uniform(-1.5,1.5); hgt=r.randint(10,24); w=r.uniform(2.4,3.6)
        for y in range(int(base),int(base-hgt),-1): a.disc(x+math.sin(y*0.18)*1.2,y,w)
        a.disc(x,base-hgt,w*0.9,2)
        for dx in range(-1,2): a.put(x+dx,base-hgt,0)   # the open mouth
    a.shade(); return a

def table(seed):
    r=random.Random(seed); a=Art(3,2); base=a.h-1
    a.limb(a.w/2,base,-math.pi/2,14,3.2,r,taper=0.99)
    top=base-14
    for i in range(13):
        ang=r.uniform(0,math.tau)
        a.disc(a.w/2+math.cos(ang)*r.uniform(0,18), top+math.sin(ang)*r.uniform(0,4)-2, r.uniform(3.5,6.5))
    a.shade(); return a

def fingers(seed):
    r=random.Random(seed); a=Art(2,2); base=a.h-1
    for i in range(6):
        x=3+i*5+r.uniform(-1,1)
        a.limb(x,base,-math.pi/2+r.uniform(-0.18,0.18),r.randint(8,20),2.6,r,taper=0.93)
    a.shade(); return a

def anemone(seed):
    r=random.Random(seed); a=Art(2,2); base=a.h-1
    a.disc(a.w/2,base-5,7.5)
    for i in range(12):
        ang=-math.pi/2+(i-5.5)*0.22+r.uniform(-0.08,0.08)
        a.limb(a.w/2+r.uniform(-4,4),base-9,ang,r.randint(7,14),1.6,r,taper=0.95)
    a.shade(); return a

def cabbage(seed):
    r=random.Random(seed); a=Art(2,2); base=a.h-1
    for i in range(5):
        y=base-3-i*4.5; wdt=13-i*1.4
        for x in range(int(a.w/2-wdt),int(a.w/2+wdt)):
            a.disc(x, y+math.sin(x*0.5+i)*2.0, 2.2)
    a.shade(); return a

def pillar(seed):
    r=random.Random(seed); a=Art(2,3); base=a.h-1
    x,y,ang,t=a.limb(a.w/2,base,-math.pi/2,32,4.6,r,taper=0.97)
    for _ in range(2): a.limb(x+r.uniform(-3,3),y+r.uniform(2,10),-math.pi/2+r.choice([-1,1])*0.5,r.randint(8,14),2.6,r)
    a.shade(); return a

def mound(seed):
    r=random.Random(seed); a=Art(2,1); base=a.h-1
    for i in range(10):
        a.disc(r.uniform(4,a.w-4), base-r.uniform(0,5), r.uniform(4,7))
    a.shade(); return a

def whip(seed):
    r=random.Random(seed); a=Art(1,3); base=a.h-1
    a.limb(a.w/2,base,-math.pi/2+0.2,40,2.2,r,taper=0.985)
    a.shade(); return a

def bubbles(seed):
    r=random.Random(seed); a=Art(2,2); base=a.h-1
    for i in range(11):
        a.disc(r.uniform(5,a.w-5), base-r.uniform(2,16), r.uniform(3,5.5))
    a.shade(); return a

SHAPES=[("STAGHORN",staghorn,3),("BRANCH",fingers,11),("BRAIN",brain,5),("FAN",fan,7),
        ("TUBES",tubes,2),("TABLE",table,9),("ANEMONE",anemone,4),("CABBAGE",cabbage,8),
        ("PILLAR",pillar,6),("MOUND",mound,1),("WHIP",whip,12),("BUBBLE",bubbles,10)]
