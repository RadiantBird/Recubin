#!/usr/bin/env python3
import argparse, math, os, sys, tempfile
from pathlib import Path
import yaml

TOPOLOGY=[("RootJoint","Root","Torso"),("Neck","Torso","Head"),("LeftShoulder","Torso","LeftArm"),("RightShoulder","Torso","RightArm"),("LeftHip","Torso","LeftLeg"),("RightHip","Torso","RightLeg")]
PARTS={p for _,a,b in TOPOLOGY for p in (a,b)}

def frame(node):
    p=node.get("Properties",{}); pos=p.get("Position"); q=p.get("Rotation")
    if not isinstance(pos,list) or len(pos)!=3 or not isinstance(q,list) or len(q)!=4: raise ValueError(f"invalid CFrame for {node.get('Name','?')}")
    v=[float(x) for x in pos+q]
    if not all(map(math.isfinite,v)): raise ValueError(f"non-finite CFrame for {node.get('Name','?')}")
    n=math.sqrt(sum(x*x for x in v[3:]));
    if n<1e-6: raise ValueError(f"invalid quaternion for {node.get('Name','?')}")
    return v[:3],[x/n for x in v[3:]]

def qmul(a,b):
    ax,ay,az,aw=a; bx,by,bz,bw=b
    return [aw*bx+ax*bw+ay*bz-az*by,aw*by-ay*0+ay*bw+az*bx-ax*bz,aw*bz+az*bw+ax*by-ay*bx,aw*bw-ax*bx-ay*by-az*bz]

def qrot(q,v):
    return qmul(qmul(q,[*v,0]),[-q[0],-q[1],-q[2],q[3]])[:3]

def relative(a,b):
    ap,aq=a; bp,bq=b; inv=[-aq[0],-aq[1],-aq[2],aq[3]]
    return {"Position":qrot(inv,[bp[i]-ap[i] for i in range(3)]),"Rotation":qmul(inv,bq)}
def ref(name): return f"StarterCharacter\\{name}"

def find_starter_characters(node):
    result = []

    if not isinstance(node, dict):
        return result

    if node.get("ClassName") == "StarterCharacter":
        result.append(node)

    for child in node.get("Children", []):
        result.extend(find_starter_characters(child))

    return result


def migrate(doc):
    changed=False

    # Root直下だけではなく、
    # Root -> Workspace -> StarterCharacter のような階層も再帰探索する
    starters=find_starter_characters(doc.get("Root",{}))

    for s in starters:
        ch=s.setdefault("Children",[])
        names=[x.get("Name") for x in ch]

        if len(names)!=len(set(names)):
            raise ValueError("duplicate StarterCharacter child name")

        by={x.get("Name"):x for x in ch}
        present={
            x.get("Name")
            for x in ch
            if x.get("ClassName")=="Motor6D"
        } & {x[0] for x in TOPOLOGY}

        if present:
            if present!={x[0] for x in TOPOLOGY} or "RootGyro" not in by:
                raise ValueError("partial Character Rig v2")
            continue

        if not PARTS<=set(by):
            raise ValueError("incomplete R6 StarterCharacter")

        changed=True
        frames={n:frame(by[n]) for n in PARTS}

        ch[:]=[
            x for x in ch
            if not (
                x.get("ClassName")=="Weld"
                and {
                    x.get("Properties",{}).get("Cube0","").split("\\")[-1],
                    x.get("Properties",{}).get("Cube1","").split("\\")[-1]
                }<=PARTS
            )
        ]

        for name,a,b in TOPOLOGY:
            ch.append({
                "ClassName":"Motor6D",
                "Name":name,
                "Properties":{
                    "Part0":ref(a),
                    "Part1":ref(b),
                    "C0":relative(frames[a],frames[b]),
                    "C1":{
                        "Position":[0,0,0],
                        "Rotation":[0,0,0,1]
                    },
                    "Transform":{
                        "Position":[0,0,0],
                        "Rotation":[0,0,0,1]
                    },
                    "Frequency":10,
                    "DampingRatio":1
                }
            })

            rag=by.get(name+"Ragdoll")

            if rag and rag.get("ClassName")=="BallSocket":
                rag.setdefault("Properties",{})["Enabled"]=False
            else:
                ch.append({
                    "ClassName":"BallSocket",
                    "Name":name+"Ragdoll",
                    "Properties":{
                        "Cube0":ref(a),
                        "Cube1":ref(b),
                        "Enabled":False
                    }
                })

        ch.append({
            "ClassName":"Gyro",
            "Name":"RootGyro",
            "Properties":{
                "Part":ref("Root"),
                "TargetRotation":frames["Root"][1],
                "Frequency":8,
                "DampingRatio":1,
                "MaxTorque":10000
            }
        })

        for n in PARTS:
            by[n].setdefault("Properties",{}).update({
                "Anchored":False,
                "CanCollide":n=="Root",
                "LockFlags":[]
            })

    return doc,changed

def files(paths):
    for p in map(Path,paths):
        if p.is_dir(): yield from sorted(x for x in p.rglob("*") if x.suffix.lower() in (".yaml",".yml",".rcbn"))
        else: yield p

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("paths",nargs="+"); ap.add_argument("--write",action="store_true"); a=ap.parse_args()
    pending=[]
    errors=0
    seen=set()
    for p in files(a.paths):
        key=str(p.resolve()).lower()
        if key in seen: continue
        seen.add(key)
        try:
            old=p.read_text(encoding="utf-8"); doc,changed=migrate(yaml.safe_load(old)); new=yaml.safe_dump(doc,sort_keys=False,allow_unicode=True) if changed else old
        except Exception as e:
            print(f"ERROR {p}: {e}",file=sys.stderr); errors+=1; continue
        if not changed: print(f"unchanged {p}"); continue
        print(("write " if a.write else "would write ")+str(p))
        pending.append((p,new))
    if a.write:
        for p,new in pending:
            tmp=None
            try:
                fd,tmp=tempfile.mkstemp(dir=p.parent,prefix=p.name+"."); os.close(fd)
                Path(tmp).write_text(new,encoding="utf-8"); os.replace(tmp,p)
            except Exception as e:
                print(f"ERROR {p}: {e}",file=sys.stderr); errors+=1
            finally:
                if tmp and os.path.exists(tmp): os.unlink(tmp)
    return 1 if errors else 0
if __name__=="__main__": raise SystemExit(main())
