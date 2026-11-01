"""Simulate 8bitShell's release path with native and legacy (32-bit) candidates, stray files included.
Runners stamp with <code_repo>'s distribution.py, upload with the globs from <workflow_repo>'s build.yml,
then draft-release downloads candidate-*, runs release-check and finalize, and "publishes" dist/release/*.
usage: simulate_legacy.py <code_repo> <workflow_repo> <workdir>   (GLOB_STRAY=1 adds a stray that matches *.zip)
STRAY_ZIPS=unlisted puts an old-version zip, a -debug.zip and an unknown-CPU zip on the native windows-x64 runner;
STRAY_ZIPS=manifest also leaves a stray manifest-*.json there that lists them with correct digests."""
import fnmatch,importlib.util,json,os,shutil,subprocess,sys,yaml
from pathlib import Path
code=Path(sys.argv[1]).resolve();wfrepo=Path(sys.argv[2]).resolve();work=Path(sys.argv[3]).resolve()
spec=importlib.util.spec_from_file_location("distribution",code/"scripts/ci/distribution.py")
d=importlib.util.module_from_spec(spec);sys.path.insert(0,str(code/"scripts/ci"));spec.loader.exec_module(d)
c=d.config();app=c["app"];v=d.version()
jobs=yaml.safe_load((wfrepo/".github/workflows/build.yml").read_text())["jobs"]
def upload_globs(job):
  for s in jobs[job]["steps"]:
    if str(s.get("uses","")).startswith("actions/upload-artifact") and str(s["with"]["name"]).startswith("candidate-"):
      return [g.strip() for g in str(s["with"]["path"]).splitlines() if g.strip()]
GLOBS={"native":upload_globs("native"),"legacy-linux":upload_globs("legacy-linux"),"legacy-windows":upload_globs("legacy-windows")}
for k,g in GLOBS.items():print(f"upload globs {k}: {g}")
EXT={"windows":[".zip",".tar.gz",".msi","-setup.exe"],"linux":[".zip",".tar.gz",".deb",".rpm",".pkg.tar.zst"],"macos":[".zip",".tar.gz",".dmg"]}
RUNNERS=[("native",*t.split("-")) for t in c["platforms"]["stable"]]+\
        [("legacy-linux","linux","x86"),("legacy-linux","linux","armv7"),("legacy-windows","windows","x86")]
print("runners:",[f"{j}:{o}-{a}" for j,o,a in RUNNERS])
shutil.rmtree(work,ignore_errors=True);merged=work/"merged";merged.mkdir(parents=True);legit=set()
for job,system,cpu in RUNNERS:
  out=work/f"runner-{system}-{cpu}/dist/release";out.mkdir(parents=True);base=f"{app}-{v}-{system}-{cpu}"
  for e in EXT[system]:(out/(base+e)).write_bytes(f"{base}{e}".encode())
  if system=="linux":   # build.py: runtime_sources.collect, source archives on x64, then manifest-source-linux-<cpu>.json
    (out/f"{app}-{v}-runtime-{cpu}-source.tar.gz").write_bytes(cpu.encode())
    if cpu=="x64":
      for n in (f"{app}-{v}-source.tar.gz",f"{app}-{v}-qt-{c['qtVersion']}-source.tar.gz"):(out/n).write_bytes(n.encode())
    ents=[{"name":p.name,"sha256":d.digest(p),"size":p.stat().st_size,"os":"source","arch":"all","version":v,"glibc":None,"experimental":False} for p in sorted(out.glob(f"{app}-{v}-*source.tar.gz"))]
    (out/f"manifest-source-linux-{cpu}.json").write_text(json.dumps(ents))
  strays=[]
  if job!="native":    # junk a packaging tool or a failed retry can leave in dist/release on a legacy runner
    strays=[base+".pdb",base+".build.log",base+".zip.part"]+([base+".wixpdb",base+".msi.log"] if system=="windows" else [])
    if os.environ.get("GLOB_STRAY")=="1" and system=="windows":strays.append(base+"-symbols.zip")
  zips=[]
  if os.environ.get("STRAY_ZIPS") and job=="native" and f"{system}-{cpu}"=="windows-x64":
    zips=[f"{app}-0.9.0-windows-x64.zip",base+"-debug.zip",f"{app}-{v}-windows-riscv64.zip"];strays+=zips
  for n in strays:(out/n).write_bytes(b"junk")
  d.stamp_assets(out,system,cpu)
  for mf in out.glob("manifest-*.json"):legit|={e["name"] for e in json.loads(mf.read_text())}|{mf.name}
  if zips and os.environ["STRAY_ZIPS"]=="manifest":
    mf=out/"manifest-windows-x64-stale.json";strays.append(mf.name)
    mf.write_text(json.dumps([{"name":n,"sha256":d.digest(out/n),"size":(out/n).stat().st_size,"os":"windows","arch":n.split("-")[-1][:-4] if "riscv" in n else "x64","version":"0.9.0" if "-0.9.0-" in n else v,"glibc":None,"experimental":False} for n in zips]))
  up=[p for p in out.iterdir() if p.is_file() and any(fnmatch.fnmatch("dist/release/"+p.name,g) for g in GLOBS[job])]
  for p in up:shutil.copy2(p,merged/p.name)
  print(f"{job:14} {system}-{cpu:5}: stamped {len(json.loads((out/f'manifest-{system}-{cpu}.json').read_text()))}, uploaded {len(up)}, strays uploaded: {sorted(p.name for p in up if p.name in strays)}")
rc=work/"repo";shutil.copytree(code,rc,ignore=shutil.ignore_patterns(".git","dist"));(rc/"dist").mkdir();shutil.copytree(merged,rc/"dist/release")
if "finalize-dry-run" in jobs:
  dr=work/"dryrun";shutil.copytree(code,dr,ignore=shutil.ignore_patterns(".git","dist"));(dr/"dist").mkdir();shutil.copytree(merged,dr/"dist/release")
  script=jobs["finalize-dry-run"]["steps"][-1]["run"]
  denv={k:x for k,x in os.environ.items() if k!="GITHUB_REF"};denv.update(GITHUB_REF="refs/heads/fix/release-asset-allowlist",RELEASE_REPOSITORY="8bitPrograms/"+app,PYTHONUTF8="1")
  denv["PATH"]=str(Path(sys.executable).parent)+os.pathsep+denv["PATH"]
  r=subprocess.run(["bash","-eo","pipefail","-c",script.replace("ls -l dist/release","ls dist/release | wc -l")],cwd=dr,env=denv,capture_output=True,text=True)
  print("finalize-dry-run job script rc=",r.returncode,(r.stdout+r.stderr).strip().splitlines()[-1:])
env=dict(os.environ,GITHUB_REF=f"refs/tags/v{v}",PYTHONUTF8="1")
r1=subprocess.run([sys.executable,"scripts/ci/release-check.py"],cwd=rc,env=env,capture_output=True,text=True)
print("release-check rc=",r1.returncode,(r1.stdout+r1.stderr).strip().splitlines()[-1:])
if r1.returncode:sys.exit(1)
r2=subprocess.run([sys.executable,"scripts/ci/distribution.py","finalize","--out","dist/release","--repository","8bitPrograms/"+app],cwd=rc,env=env,capture_output=True,text=True)
print("finalize rc=",r2.returncode,(r2.stdout+r2.stderr).strip().splitlines()[-1:])
if r2.returncode:print("=> gh release create is never reached; nothing published");sys.exit(2)
rel=rc/"dist/release";m=json.loads((rel/"release-manifest.json").read_text())
listed=legit|{"release-manifest.json","install-index.tsv","install.sh","install.ps1","SHA256SUMS.txt"}  # what the runners really built, not what finalize accepted
published=sorted(p.name for p in rel.iterdir())
sums=[l.split()[1] for l in (rel/"SHA256SUMS.txt").read_text().splitlines()]
print(f"release-manifest assets: {len(m['assets'])}; install-index rows: {len((rel/'install-index.tsv').read_text().strip().splitlines())}; files gh would attach: {len(published)}")
print("JUNK gh would attach:",[n for n in published if n not in listed]," (also in SHA256SUMS:",[n for n in sums if n not in listed],")")
