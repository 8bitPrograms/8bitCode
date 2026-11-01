"""Simulate the 8bit release pipeline: per-runner stamp -> upload-artifact globs -> merged download -> release-check -> finalize.
usage: simulate.py <repo_root> <workdir> [app]"""
import fnmatch,importlib.util,json,os,shutil,subprocess,sys
from pathlib import Path
repo=Path(sys.argv[1]).resolve();work=Path(sys.argv[2]).resolve()
spec=importlib.util.spec_from_file_location("distribution",repo/"scripts/ci/distribution.py")
d=importlib.util.module_from_spec(spec);sys.path.insert(0,str(repo/"scripts/ci"));spec.loader.exec_module(d)
c=d.config();app=c["app"];v=d.version()
UPLOAD=["*.zip","*.tar.gz","*.msi","*.exe","*.dmg","*.deb","*.rpm","*.pkg.tar.zst","manifest-*.json"]
TARGETS={"windows":["x64","arm64"],"linux":["x64","arm64"],"macos":["x64","arm64"]}
EXT={"windows":[".zip",".tar.gz",".msi","-setup.exe"],"linux":[".zip",".tar.gz",".deb",".rpm",".pkg.tar.zst"],"macos":[".zip",".tar.gz",".dmg"]}
if os.environ.get("STABLE_ONLY")=="1":
  st=c["platforms"]["stable"];TARGETS={k:[a for a in v_ if f"{k}-{a}" in st] for k,v_ in TARGETS.items()};TARGETS={k:v_ for k,v_ in TARGETS.items() if v_}
shutil.rmtree(work,ignore_errors=True);merged=work/"merged/dist/release";merged.mkdir(parents=True)
for system,cpus in TARGETS.items():
  for cpu in cpus:
    out=work/f"runner-{system}-{cpu}";out.mkdir(parents=True)
    base=f"{app}-{v}-{system}-{cpu}"
    for e in EXT[system]:(out/(base+e)).write_bytes(f"{base}{e}".encode())
    # strays seen/possible on runners
    if system=="windows":(out/(base+".wixpdb")).write_bytes(b"wix pdb")          # light.exe default pdb (the failing file)
    if system=="linux":
      (out/f"{app}-{v}-runtime-{cpu}-source.tar.gz").write_bytes(b"rt-src")       # runtime_sources.collect
      if cpu=="x64":
        for n in (f"{app}-{v}-source.tar.gz",f"{app}-{v}-qt-{c['qtVersion']}-source.tar.gz"):(out/n).write_bytes(n.encode())
        ents=[{"name":p.name,"sha256":d.digest(p),"size":p.stat().st_size,"os":"source","arch":"all","version":v,"glibc":None,"experimental":False} for p in sorted(out.glob(f"{app}-{v}-*source.tar.gz"))]
      else:
        ents=[]
      (out/f"manifest-source-linux-{cpu}.json").write_text(json.dumps(ents))
    # other hypothetical strays with the platform prefix
    if os.environ.get("EXTRA_STRAYS")=="1":
      (out/(base+".msi.log")).write_bytes(b"log")
      (out/(base+".zip.part")).write_bytes(b"partial")
    try:d.stamp_assets(out,system,cpu)
    except Exception as ex:print(f"STAMP {system}-{cpu} FAILED: {ex!r}");raise
    names=[e["name"] for e in json.loads((out/f"manifest-{system}-{cpu}.json").read_text())]
    print(f"stamp {system}-{cpu}: {len(names)} entries; strays in manifest: {[n for n in names if not any(n==base+e for e in EXT[system])]}")
    for p in out.iterdir():
      if any(fnmatch.fnmatch(p.name,g) for g in UPLOAD):shutil.copy2(p,merged/p.name)
# release-check + finalize run from a copy of the repo with dist/release populated, like the draft-release job
rc=work/"repo";shutil.copytree(repo,rc,ignore=shutil.ignore_patterns(".git","dist"));(rc/"dist").mkdir();shutil.copytree(merged,rc/"dist/release")
env=dict(os.environ,GITHUB_REF=f"refs/tags/v{v}",PYTHONUTF8="1")
r1=subprocess.run([sys.executable,"scripts/ci/release-check.py"],cwd=rc,env=env,capture_output=True,text=True)
print("release-check rc=",r1.returncode,(r1.stdout+r1.stderr).strip().splitlines()[-1:])
r2=subprocess.run([sys.executable,"scripts/ci/distribution.py","finalize","--out","dist/release","--repository","8bitPrograms/"+app],cwd=rc,env=env,capture_output=True,text=True)
print("finalize rc=",r2.returncode,(r2.stdout+r2.stderr).strip().splitlines()[-1:])
if r2.returncode==0:
  m=json.loads((rc/"dist/release/release-manifest.json").read_text())
  print("release-manifest assets:",len(m["assets"]),"install-index rows:",len((rc/"dist/release/install-index.tsv").read_text().split()) and len((rc/"dist/release/install-index.tsv").read_text().strip().splitlines()))
  print("published files:",sorted(p.name for p in (rc/"dist/release").iterdir()))
sys.exit(r1.returncode or r2.returncode)
