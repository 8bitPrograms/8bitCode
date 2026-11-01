"""Parse 8bitShell build.yml and walk the job graph for dispatch/tag/push scenarios.
Expressions are emulated for exactly the forms used in this workflow."""
import json,os,subprocess,sys,tempfile,yaml
from pathlib import Path
repo=Path(sys.argv[1]);wf=yaml.safe_load((repo/".github/workflows/build.yml").read_text())
jobs=wf["jobs"]
print("jobs:",list(jobs));print("native.if:",jobs["native"].get("if"));print("native.needs:",jobs["native"]["needs"])
print("draft-release.needs:",jobs["draft-release"]["needs"]);print("draft-release.if:",jobs["draft-release"]["if"])
for j in ("legacy-linux","legacy-windows"):print(j+".needs:",jobs[j]["needs"],"if:",jobs[j]["if"])
assert jobs["native"]["if"]=="fromJSON(needs.plan.outputs.native).include[0] != null"
def plan(event,ref,target):
  with tempfile.NamedTemporaryFile("r",delete=False) as f:pass
  env=dict(os.environ,GITHUB_EVENT_NAME=event,GITHUB_REF=ref,BUILD_TARGET=target,GITHUB_OUTPUT=f.name)
  subprocess.run([sys.executable,repo/"scripts/ci/matrix.py"],env=env,check=True)
  line=Path(f.name).read_text().strip();os.unlink(f.name);assert line.startswith("native=");return line[len("native="):]
def native_guard(out):  # fromJSON(x).include[0] != null ; GitHub returns null for out-of-range index
  inc=json.loads(out).get("include",[]);return (inc[0] if inc else None) is not None
def legacy(event,target,which,legacy32):
  return legacy32 and (event!="workflow_dispatch" or target in ("all",which))
def draft(native_res,ll,lw,ref,publish_draft):
  return native_res=="success" and ll in ("success","skipped") and lw in ("success","skipped") and (ref.startswith("refs/tags/v") or publish_draft)
rows=[("push","refs/tags/v1.0.0","all",False,False),("workflow_dispatch","refs/tags/v1.0.0","macos",False,False),("workflow_dispatch","refs/tags/v1.0.0","macos",True,True),
      ("workflow_dispatch","refs/tags/v1.0.0","windows",False,True),("workflow_dispatch","refs/tags/v1.0.0","linux",True,False),("push","refs/heads/main","all",False,False),
      ("pull_request","refs/pull/12/merge","all",False,False),("workflow_dispatch","refs/heads/main","macos",False,False),("workflow_dispatch","refs/heads/release/v1.0.0-prep","all",False,True)]
print("\n| event | ref | target | legacy32 | publish_draft | plan output | native | legacy-linux | legacy-windows | draft-release |")
print("|---|---|---|---|---|---|---|---|---|---|")
for ev,ref,tgt,l32,pd in rows:
  out=plan(ev,ref,tgt if ev=="workflow_dispatch" else "all");g=native_guard(out)
  n="runs ("+str(len(json.loads(out)["include"]))+" legs)" if g else "SKIPPED"
  ll="runs" if legacy(ev,tgt,"linux",l32) else "skipped";lw="runs" if legacy(ev,tgt,"windows",l32) else "skipped"
  nres="success" if g else "skipped"
  d=draft(nres,"success" if ll=="runs" else "skipped","success" if lw=="runs" else "skipped",ref,pd and ev=="workflow_dispatch")
  short=out if len(out)<40 else out[:37]+"..."
  print(f"| {ev} | {ref} | {tgt} | {l32} | {pd} | `{short}` | {n} | {ll} | {lw} | {'runs (if native succeeded)' if d else 'skipped'} |")
