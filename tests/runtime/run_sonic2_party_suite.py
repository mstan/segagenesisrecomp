"""Serial owner-ROM acceptance fixtures; no GUI or concurrent runtime instances."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__)
for name in ("exe","rom","amy","s3k","out"):
    p.add_argument(f"--{name}",type=Path,required=True)
a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parent
common=["--exe",str(a.exe.resolve()),"--rom",str(a.rom.resolve())]
donors=["--amy",str(a.amy.resolve()),"--s3k",str(a.s3k.resolve())]
cases=[("options","run_sonic2_options.py",[]),("roles","run_sonic2_roles.py",[])]
for name,extra in (("four",[]),("native-extras",["--native-extras"]),("four-wide",["--widescreen","16:9"])):
    cases.append((name,"run_sonic2_four_players.py",donors+extra))
for character,donor in (("amy",a.amy),("knuckles",a.s3k)):
    base=["--character",character,"--donor",str(donor.resolve()),"--p2","sonic"]
    for mode,extra in (("campaign",[]),("vs",["--vs"]),("vs-p2",["--vs","--swap-roles"])):
        cases.append((f"{character}-{mode}","run_sonic2_character_smoke.py",base+extra))
for scene in ("spring","combat","monitor","recovery","boss","climb","special"):
    cases.append((scene,"run_sonic2_interactions.py",donors+["--scene",scene]))
cases.append(("special-swap","run_sonic2_interactions.py",donors+["--scene","special","--swap"]))
cases.append(("special-solo","run_sonic2_interactions.py",donors+["--scene","special","--solo"]))
cases.append(("special-stock","run_sonic2_interactions.py",donors+["--scene","special","--stock"]))
cases.append(("checkpoint","run_sonic2_checkpoint.py",donors))
cases.append(("checkpoint-native-extras","run_sonic2_checkpoint.py",donors+["--native-extras"]))
results=[]
for name,script,extra in cases:
    command=[sys.executable,str(root/script)]+common+extra+["--out",str(a.out.resolve()/name)]
    result=subprocess.run(command,check=False)
    results.append({"case":name,"exit":result.returncode})
    (a.out/"results.json").write_text(json.dumps(results,indent=2)+"\n")
    if result.returncode:
        raise SystemExit(f"FAILED {name}; retained diagnostics under {a.out/name}")
# Identical native half-pipe presentation, including after P2 input, regardless
# of campaign characters, swapped roles, or a solo roster with P2=NONE.
for frame in ("halfpipe.png","input.png"):
    reference=(a.out/"special-stock"/frame).read_bytes()
    for case in ("special","special-swap","special-solo"):
        assert (a.out/case/frame).read_bytes()==reference, f"{case}: non-native SS presentation at {frame}"
print(f"PASS {len(results)} serial live party cases; human visual/audio/controller approval still required")
