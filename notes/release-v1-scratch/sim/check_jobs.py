"""Evaluate the real jobs.<id>.if expressions of build.yml for trigger scenarios.
Small GitHub Actions expression evaluator: literals, context dereference/index,
! == != < > && || ( ), always/success/failure/cancelled/startsWith/endsWith/contains/fromJSON.
Usage: check_jobs.py <repo> [missing=empty|null]"""
import json,os,re,subprocess,sys,tempfile,yaml
from pathlib import Path
repo=Path(sys.argv[1]);MISSING="" if (sys.argv[2:] or ["empty"])[0]=="empty" else None
wf=yaml.safe_load((repo/".github/workflows/build.yml").read_text());jobs=wf["jobs"]
on=wf.get("on",wf.get(True));dispatch_inputs=on["workflow_dispatch"]["inputs"]
TOK=re.compile(r"\s*(?:(?P<num>\d+(?:\.\d+)?)|(?P<str>'(?:[^']|'')*')|(?P<op>&&|\|\||==|!=|<=|>=|[!<>()\[\].,])|(?P<id>[A-Za-z_][A-Za-z0-9_-]*))")
def lex(s):
  out=[];i=0;s=s.strip()
  if s.startswith("${{"):s=s[3:-2]
  while i<len(s):
    m=TOK.match(s,i)
    if not m or m.end()==i:
      if s[i:].strip()=="":break
      raise SyntaxError(s[i:])
    k=m.lastgroup;out.append((k,m.group(k)));i=m.end()
  return out
def truthy(v):return not (v is None or v is False or v=="" or v==0)
def num(v):
  if v is None:return 0
  if isinstance(v,bool):return int(v)
  if isinstance(v,(int,float)):return v
  if isinstance(v,str):
    try:return float(v) if v.strip() else 0
    except ValueError:return float("nan")
  return float("nan")
def eq(a,b):
  if type(a)==type(b) or (isinstance(a,(int,float)) and isinstance(b,(int,float)) and not isinstance(a,bool) and not isinstance(b,bool)):
    return a.lower()==b.lower() if isinstance(a,str) else a==b
  if isinstance(a,(dict,list)) or isinstance(b,(dict,list)):return a is b
  return num(a)==num(b)
class P:
  def __init__(s,toks,ctx,fn):s.t=toks;s.i=0;s.ctx=ctx;s.fn=fn
  def peek(s):return s.t[s.i] if s.i<len(s.t) else (None,None)
  def take(s,v=None):
    k,x=s.peek()
    if v is not None and x!=v:raise SyntaxError(f"expected {v} got {x}")
    s.i+=1;return k,x
  def expr(s):
    v=s.andx()
    while s.peek()[1]=="||":
      s.take();r=s.andx();v=v if truthy(v) else r
    return v
  def andx(s):
    v=s.cmp()
    while s.peek()[1]=="&&":
      s.take();r=s.cmp();v=r if truthy(v) else v
    return v
  def cmp(s):
    v=s.unary()
    while s.peek()[1] in ("==","!=","<",">","<=",">="):
      op=s.take()[1];r=s.unary()
      v={"==":lambda:eq(v,r),"!=":lambda:not eq(v,r),"<":lambda:num(v)<num(r),">":lambda:num(v)>num(r),"<=":lambda:num(v)<=num(r),">=":lambda:num(v)>=num(r)}[op]()
    return v
  def unary(s):
    if s.peek()[1]=="!":s.take();return not truthy(s.unary())
    return s.postfix()
  def get(s,v,key):
    if isinstance(v,dict):
      for k in v:
        if isinstance(key,str) and k.lower()==key.lower():return v[k]
      return MISSING
    if isinstance(v,list) and isinstance(key,(int,float)) and not isinstance(key,bool):
      return v[int(key)] if 0<=int(key)<len(v) else None
    return None
  def postfix(s):
    v=s.primary()
    while s.peek()[1] in (".","["):
      if s.take()[1]==".":v=s.get(v,s.take()[1])
      else:k=s.expr();s.take("]");v=s.get(v,k)
    return v
  def primary(s):
    k,x=s.take()
    if x=="(":v=s.expr();s.take(")");return v
    if k=="num":return float(x) if "." in x else int(x)
    if k=="str":return x[1:-1].replace("''","'")
    if x in ("true","false"):return x=="true"
    if x=="null":return None
    if s.peek()[1]=="(":
      s.take();args=[]
      if s.peek()[1]!=")":
        args.append(s.expr())
        while s.peek()[1]==",":s.take();args.append(s.expr())
      s.take(")");return s.fn(x.lower(),args)
    return s.ctx.get(x,MISSING)
def evaluate(e,ctx,needs_results):
  def fn(name,a):
    if name=="always":return True
    if name=="success":return all(r=="success" for r in needs_results)
    if name=="failure":return any(r=="failure" for r in needs_results)
    if name=="cancelled":return False
    if name=="startswith":return str(a[0] or "").lower().startswith(str(a[1]).lower())
    if name=="endswith":return str(a[0] or "").lower().endswith(str(a[1]).lower())
    if name=="contains":return (str(a[1]).lower() in str(a[0]).lower()) if isinstance(a[0],str) else any(eq(x,a[1]) for x in a[0] or [])
    if name=="fromjson":return json.loads(a[0])
    raise NameError(name)
  p=P(lex(e),ctx,fn);v=p.expr()
  if p.i!=len(p.t):raise SyntaxError("trailing tokens in "+e)
  return truthy(v)
def plan_output(event,ref,target):
  f=tempfile.NamedTemporaryFile("w",delete=False);f.close()
  env=dict(os.environ,GITHUB_EVENT_NAME=event,GITHUB_REF=ref,BUILD_TARGET=target or "all",GITHUB_OUTPUT=f.name,SKIP_MACOS="false")
  subprocess.run([sys.executable,repo/"scripts/ci/matrix.py"],env=env,check=True)
  out={}
  for line in Path(f.name).read_text().splitlines():k,_,v=line.partition("=");out[k]=v
  os.unlink(f.name);return out
STATUS=re.compile(r"\b(always|success|failure|cancelled)\s*\(")
def run(event,ref,given):
  inputs={}
  if event=="workflow_dispatch":
    inputs={k:v.get("default") for k,v in dispatch_inputs.items()};inputs.update(given)
  ctx={"github":{"event_name":event,"ref":ref,"actor":"ops"},"inputs":inputs,"vars":{}}
  results={};order=list(jobs)
  while len(results)<len(order):
    for j in order:
      if j in results:continue
      n=jobs[j].get("needs",[]);n=[n] if isinstance(n,str) else n
      if any(x not in results for x in n):continue
      ctx["needs"]={x:{"result":results[x],"outputs":outs.get(x,{}) if results[x]=="success" else {}} for x in n}
      cond=str(jobs[j].get("if","success()"))
      if not STATUS.search(cond):cond=f"success() && ({cond})"
      ok=evaluate(cond,ctx,[results[x] for x in n])
      results[j]="success" if ok else "skipped"
      if j=="plan" and ok:outs["plan"]=plan_output(event,ref,inputs.get("target"))
  return results,ctx
outs={}
def upload_step(ctx):
  for st in jobs["native"]["steps"]:
    if st.get("name")=="Upload package candidates":return evaluate(st["if"],ctx,["success"])
SC=[("tag push","push","refs/tags/v1.0.0",{}),
    ("tag dispatch, no dry-run","workflow_dispatch","refs/tags/v1.0.0",{}),
    ("tag dispatch, no dry-run, publish_draft","workflow_dispatch","refs/tags/v1.0.0",{"publish_draft":True}),
    ("tag dispatch, dry-run","workflow_dispatch","refs/tags/v1.0.0",{"finalize_dry_run":True}),
    ("tag dispatch, dry-run, publish_draft","workflow_dispatch","refs/tags/v1.0.0",{"finalize_dry_run":True,"publish_draft":True}),
    ("tag dispatch, dry-run, legacy32","workflow_dispatch","refs/tags/v1.0.0",{"finalize_dry_run":True,"legacy32":True}),
    ("branch dispatch, dry-run","workflow_dispatch","refs/heads/fix/release-asset-allowlist",{"finalize_dry_run":True}),
    ("branch dispatch, dry-run, publish_draft","workflow_dispatch","refs/heads/fix/release-asset-allowlist",{"finalize_dry_run":True,"publish_draft":True}),
    ("main push","push","refs/heads/main",{}),
    ("pull request","pull_request","refs/pull/10/merge",{})]
if "finalize_dry_run" not in dispatch_inputs:SC=[s for s in SC if "dry-run" not in s[0]]+[("tag dispatch, publish_draft=false (default)","workflow_dispatch","refs/tags/v1.0.0",{"publish_draft":False}),("branch dispatch, publish_draft","workflow_dispatch","refs/heads/main",{"publish_draft":True})]
cols=[j for j in ("native","legacy-linux","legacy-windows","draft-release","finalize-dry-run") if j in jobs]
print(f"missing context values evaluate to {MISSING!r}")
print("| scenario | "+" | ".join(cols)+" | candidates uploaded |");print("|---"*(len(cols)+2)+"|")
for name,ev,ref,inp in SC:
  r,ctx=run(ev,ref,inp)
  print(f"| {name} | "+" | ".join("runs" if r[c]=="success" else "skipped" for c in cols)+f" | {'yes' if r['native']=='success' and upload_step(ctx) else 'no'} |")
