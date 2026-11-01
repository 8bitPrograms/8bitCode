#!/usr/bin/env python3
"""Build-time release contracts; target machines never need Python."""
from __future__ import annotations
import argparse,hashlib,json,os,platform,re,shutil,struct,subprocess,tarfile,urllib.request,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
ARCHES={"amd64":"x64","x86_64":"x64","aarch64":"arm64","arm64":"arm64","i386":"x86","i686":"x86","x86":"x86","armv7l":"armv7"}
SYSTEM_LIB=re.compile(r"^(linux-vdso|ld-linux|ld64|lib(c|m|dl|pthread|rt|resolv|util|nss_[a-z_]+)\.so)")
def config():return json.loads((ROOT/"packaging/release.json").read_text())
def run(args,**kw):return subprocess.run([str(a) for a in args],check=True,**kw)
def digest(p):
    h=hashlib.sha256()
    with Path(p).open("rb") as f:
        for b in iter(lambda:f.read(1048576),b""):h.update(b)
    return h.hexdigest()
def version():
    c=config();text=(ROOT/c["versionFile"]).read_text()
    v=text.strip() if c["versionFile"]!="CMakeLists.txt" else re.search(r"VERSION\s+(\d+\.\d+\.\d+)",text).group(1)
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)",v):raise ValueError("Stable three-part version required")
    return v
def arch():
    a=os.environ.get("RELEASE_ARCH") or ARCHES.get(platform.machine().lower())
    if a not in {"x64","arm64","x86","armv7"}:raise ValueError(f"Unsupported CPU: {a}")
    return a
def verify_binary(path,system,cpu):
    """Reject a mislabeled native application before packaging."""
    path=Path(path)
    with path.open("rb") as f:
        header=f.read(64)
        if system=="windows":
            if len(header)<64 or header[:2]!=b"MZ":raise ValueError("Invalid Windows executable")
            f.seek(struct.unpack_from("<I",header,60)[0]);pe=f.read(6)
            if len(pe)!=6 or pe[:4]!=b"PE\0\0":raise ValueError("Invalid PE header")
            actual=struct.unpack_from("<H",pe,4)[0]
            expected={"x64":0x8664,"arm64":0xaa64,"x86":0x14c}.get(cpu)
        elif system=="linux":
            if len(header)<20 or header[:4]!=b"\x7fELF" or header[5] not in (1,2):raise ValueError("Invalid ELF executable")
            actual=(header[4],struct.unpack_from("<H" if header[5]==1 else ">H",header,18)[0])
            expected={"x64":(2,62),"arm64":(2,183),"x86":(1,3),"armv7":(1,40)}.get(cpu)
        elif system=="macos":
            endian={b"\xcf\xfa\xed\xfe":"<",b"\xfe\xed\xfa\xcf":">"}.get(header[:4])
            if not endian or len(header)<8:raise ValueError("Expected a native 64-bit Mach-O application")
            actual=struct.unpack_from(endian+"I",header,4)[0]
            expected={"x64":0x1000007,"arm64":0x100000c}.get(cpu)
        else:raise ValueError("Unknown executable platform")
    if expected is None or actual!=expected:raise ValueError(f"Wrong application architecture: {system}/{cpu} ({actual})")

def download(url,dest,expected=None):
    if not url.startswith("https://"):raise ValueError("HTTPS required")
    dest=Path(dest);dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():
        tmp=dest.with_suffix(dest.suffix+".part")
        try:
            req=urllib.request.Request(url,headers={"User-Agent":"8bit-release/1.0"})
            with urllib.request.urlopen(req,timeout=120) as r,tmp.open("wb") as f:shutil.copyfileobj(r,f)
            tmp.replace(dest)
        finally:tmp.unlink(missing_ok=True)
    if expected and digest(dest)!=expected:raise ValueError(f"SHA256 mismatch: {dest.name}")
    return dest
def extract(archive,dest):
    dest=Path(dest);dest.mkdir(parents=True,exist_ok=True)
    if str(archive).endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for m in z.infolist():
                p=Path(m.filename)
                if p.is_absolute() or ".." in p.parts or "\\" in m.filename:raise ValueError("Unsafe ZIP path")
            z.extractall(dest)
    else:
        with tarfile.open(archive) as t:t.extractall(dest,filter="data")
def safe_links(root):
    root=Path(root).resolve()
    for p in root.rglob("*"):
        if p.is_symlink() and not p.resolve().is_relative_to(root):raise ValueError(f"External symlink: {p}")
def elf(p):
    with p.open("rb") as f:return f.read(4)==b"\x7fELF"
def bundle_linux(stage):
    stage=Path(stage).resolve();lib=stage/"lib";lib.mkdir(exist_ok=True);seen=set()
    while True:
        pending=[p for p in stage.rglob("*") if p.is_file() and not p.is_symlink() and elf(p) and str(p) not in seen]
        if not pending:break
        for p in pending:
            seen.add(str(p));r=run(["ldd",p],capture_output=True,text=True)
            if "not found" in r.stdout:raise ValueError(f"Unresolved dependency: {p}\n{r.stdout}")
            for line in r.stdout.splitlines():
                m=re.search(r"(?:=>\s+)?(/[^\s]+)",line)
                if m:
                    src=Path(m.group(1));dst=lib/src.name
                    if not SYSTEM_LIB.match(src.name) and not dst.exists():shutil.copy2(src.resolve(),dst)
    for p in stage.rglob("*"):
        if p.is_file() and not p.is_symlink() and elf(p):
            rel=os.path.relpath(lib,p.parent)
            run(["patchelf","--set-rpath","$ORIGIN" if rel=="." else "$ORIGIN/"+rel,p])
    fonts=stage/"fonts";fonts.mkdir(exist_ok=True)
    for n in ("DejaVuSans.ttf","DejaVuSansMono.ttf"):shutil.copy2(Path("/usr/share/fonts/truetype/dejavu")/n,fonts/n)
    for p in [Path("/usr/share/doc/fonts-dejavu-core/copyright"),Path("/usr/share/doc/fonts-dejavu/copyright")]:
        if p.exists():shutil.copy2(p,fonts/"LICENSE");break
    safe_links(stage)
def launcher(stage):
    c=config();app=c["app"];stage=Path(stage)
    head='#!/bin/sh\nset -eu\nbase=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)\n'
    script=head+'export QT_PLUGIN_PATH="$base/plugins"\nexport QML_IMPORT_PATH="$base/qml"\nexport QML2_IMPORT_PATH="$base/qml"\nexport QT_QPA_FONTDIR="$base/fonts"\n'
    # Portable archives may be unpacked on case-insensitive filesystems: no case-only duplicates.
    for name in [n for n in dict.fromkeys([app]+c.get("commandAliases",[])) if n==app or n.casefold()!=app.casefold()]:
        p=stage/name;p.write_text(script+f'exec "$base/bin/{app}" "$@"\n');p.chmod(0o755)
    p=stage/"Start-Portable.sh";p.write_text(head+f'export {c["environment"]}_PROFILE="$base/profile"\nexec "$base/{app}" "$@"\n');p.chmod(0o755)
    (stage/"bin/qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\nTranslations=translations\n")
def root_owner(info):
    info.uid=0;info.gid=0;info.uname="root";info.gname="root"
    return info
def archive_tree(stage,out,system,cpu):
    stage=Path(stage);out=Path(out);out.mkdir(parents=True,exist_ok=True);base=f'{config()["app"]}-{version()}-{system}-{cpu}';safe_links(stage)
    with tarfile.open(out/(base+".tar.gz"),"w:gz",dereference=system!="macos") as t:t.add(stage,arcname=base,filter=root_owner)
    if system=="macos":run(["ditto","-c","-k","--sequesterRsrc","--keepParent",stage,out/(base+".zip")])
    else:
        with zipfile.ZipFile(out/(base+".zip"),"w",zipfile.ZIP_DEFLATED) as z:
            for p in sorted(stage.rglob("*")):
                if p.is_file():z.write(p,Path(base)/p.relative_to(stage))
def linux_packages(stage,out,cpu):
    c=config();app=c["app"];slug=c["slug"];v=version();stage=Path(stage).resolve();out=Path(out).resolve()
    da={"x64":"amd64","arm64":"arm64","x86":"i386","armv7":"armhf"}[cpu]
    ra={"x64":"x86_64","arm64":"aarch64","x86":"i686","armv7":"armv7hl"}[cpu]
    former=", ".join(c.get("replaces",[]))  # earlier package names this package upgrades in place
    deb_rel=f"Conflicts: {former}\nReplaces: {former}\n" if former else ""
    rpm_rel="".join(f"Obsoletes: {n}\nConflicts: {n}\n" for n in c.get("replaces",[]))
    pac_rel="".join(f"replaces = {n}\nconflict = {n}\n" for n in c.get("replaces",[]))
    aa={"x64":"x86_64","arm64":"aarch64","x86":"i686","armv7":"armv7h"}[cpu]
    work=out/("native-"+cpu);payload=work/"root";payload.mkdir(parents=True,exist_ok=False)
    shutil.copytree(stage,payload/"opt"/slug/v,symlinks=False)
    bindir=payload/"usr/bin";bindir.mkdir(parents=True)
    for name in dict.fromkeys([app]+c.get("commandAliases",[])):
        wrapper=bindir/name;wrapper.write_text(f'#!/bin/sh\nexec "/opt/{slug}/{v}/{app}" "$@"\n');wrapper.chmod(0o755)
    apps=payload/"usr/share/applications";apps.mkdir(parents=True)
    (apps/(slug+".desktop")).write_text(f'[Desktop Entry]\nType=Application\nName={app}\nExec={app} %F\nIcon={slug}\nTerminal=false\nCategories=Development;Utility;\n')
    icons=payload/"usr/share/icons/hicolor/scalable/apps";icons.mkdir(parents=True)
    shutil.copy2(ROOT/c["iconSvg"],icons/(slug+".svg"))
    control=work/"control";control.mkdir()
    (control/"control").write_text(f'Package: {slug}\nVersion: {v}\nArchitecture: {da}\nMaintainer: {c["maintainer"]}\nSection: utils\nPriority: optional\n{deb_rel}Depends: libc6 (>= {c.get("linuxGlibcByArch",{}).get(cpu,c["linuxGlibc"])})\nDescription: {c["description"]}\n Qt and non-system runtime libraries are bundled privately.\n')
    with tarfile.open(work/"control.tar.gz","w:gz") as t:t.add(control/"control",arcname="./control",filter=root_owner)
    with tarfile.open(work/"data.tar.gz","w:gz",dereference=True) as t:
        for p in payload.iterdir():t.add(p,arcname="./"+p.name,filter=root_owner)
    (work/"debian-binary").write_bytes(b"2.0\n")
    with (out/f'{app}-{v}-linux-{cpu}.deb').open("wb") as f:
        f.write(b"!<arch>\n")
        for n in ("debian-binary","control.tar.gz","data.tar.gz"):
            b=(work/n).read_bytes();header=f'{n+"/":<16}{0:<12}{0:<6}{0:<6}{"100644":<8}{len(b):<10}'+chr(96)+"\n"
            f.write(header.encode("ascii"));f.write(b)
            if len(b)%2:f.write(b"\n")
    top=work/"rpmbuild"
    for d in ("BUILD","BUILDROOT","RPMS","SOURCES","SPECS","SRPMS"):(top/d).mkdir(parents=True)
    spec=top/"SPECS/app.spec"
    spec.write_text(f'''Name: {slug}
Version: {v}
Release: 1
Summary: {c["description"]}
License: {c["license"]}
BuildArch: {ra}
AutoReqProv: no
{rpm_rel}Requires: glibc >= {c.get("linuxGlibcByArch",{}).get(cpu,c["linuxGlibc"])}
%global __os_install_post %{{nil}}
%description
{c["description"]}. Runtime libraries are bundled.
%install
mkdir -p %{{buildroot}}
cp -a "{payload}/." %{{buildroot}}/
%files
%defattr(-,root,root,-)
/opt/{slug}
/usr/bin/{app}
{chr(10).join("/usr/bin/"+alias for alias in c.get("commandAliases",[]))}
/usr/share/applications/{slug}.desktop
/usr/share/icons/hicolor/scalable/apps/{slug}.svg
''')
    run(["rpmbuild","-bb","--target",ra,"--define",f"_topdir {top}",spec])
    built=list((top/"RPMS").rglob("*.rpm"))
    if len(built)!=1:raise ValueError("Expected one RPM")
    shutil.copy2(built[0],out/f'{app}-{v}-linux-{cpu}.rpm')
    (payload/".PKGINFO").write_text(f'pkgname = {slug}\npkgbase = {slug}\n{pac_rel}pkgver = {v}-1\npkgdesc = {c["description"]}\nurl = https://github.com/{os.environ.get("GITHUB_REPOSITORY","UNCONFIGURED")}\nbuilddate = {os.environ.get("SOURCE_DATE_EPOCH","0")}\npackager = {c["maintainer"]}\nsize = {sum(p.stat().st_size for p in payload.rglob("*") if p.is_file())}\narch = {aa}\nlicense = {c["license"]}\ndepend = glibc\n')
    raw=work/"package.tar"
    with tarfile.open(raw,"w",dereference=True) as t:
        for p in payload.iterdir():t.add(p,arcname=p.name,filter=root_owner)
    run(["zstd","-q","-T0","-f",raw,"-o",out/f'{app}-{v}-linux-{cpu}.pkg.tar.zst']);shutil.rmtree(work)
# Release assets each target publishes, as suffixes after "<app>-<version>-<os>-<cpu>".
# Anything else a packaging tool leaves in the output folder (WiX .wixpdb,
# logs, partial downloads) is not a release asset and must not be stamped.
RELEASE_ASSETS={
    "windows":(".zip",".tar.gz",".msi","-setup.exe"),
    "linux":(".zip",".tar.gz",".deb",".rpm",".pkg.tar.zst"),
    "macos":(".zip",".tar.gz",".dmg"),
}
def stamp_assets(out,system,cpu):
    out=Path(out);c=config();v=version();entries=[]
    wanted={f'{c["app"]}-{v}-{system}-{cpu}{suffix}' for suffix in RELEASE_ASSETS[system]}
    for p in sorted(out.iterdir()):
        if p.is_file() and p.name in wanted:
            entries.append({"name":p.name,"sha256":digest(p),"size":p.stat().st_size,"os":system,"arch":cpu,"version":v,"glibc":c.get("linuxGlibcByArch",{}).get(cpu,c["linuxGlibc"]) if system=="linux" else None,"experimental":cpu in ("x86","armv7")})
    if not entries:raise ValueError("No packages produced")
    (out/f"manifest-{system}-{cpu}.json").write_text(json.dumps(entries,indent=2)+"\n")
# Files finalize writes. With the manifests and the assets they list, they are the
# only files draft-release may attach (gh release create dist/release/*).
FINALIZE_OUTPUTS=("release-manifest.json","install-index.tsv","install.sh","install.ps1","SHA256SUMS.txt")
def release_asset_names(c,v):
    # Every name a manifest entry may carry for version v: the RELEASE_ASSETS of each
    # configured target, and the source bundles build.py writes on Linux runners.
    # A stray manifest (an old version, an unknown CPU) cannot vouch for other names.
    names={f'{c["app"]}-{v}-source.tar.gz',f'{c["app"]}-{v}-qt-{c["qtVersion"]}-source.tar.gz'}
    for target in c["platforms"]["stable"]+c["platforms"]["experimental"]:
        system,cpu=target.split("-",1)
        names|={f'{c["app"]}-{v}-{target}{suffix}' for suffix in RELEASE_ASSETS[system]}
        if system=="linux":names.add(f'{c["app"]}-{v}-runtime-{cpu}-source.tar.gz')
    return names
def finalize(out,repository):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+",repository):raise ValueError("Real owner/repository required")
    out=Path(out);entries=[];manifests=sorted(out.glob("manifest-*.json"))
    for p in manifests:entries+=json.loads(p.read_text())
    if not entries:raise ValueError("No tested package manifests")
    v=version();allowed=release_asset_names(config(),v)
    foreign=sorted({str(e["name"]) for e in entries if e["name"] not in allowed or e.get("version")!=v})
    if foreign:raise ValueError(f"Not a {v} release asset, refusing to publish: "+", ".join(foreign))
    names=set()
    for e in entries:
        if Path(e["name"]).name!=e["name"] or "/" in e["name"] or "\\" in e["name"]:raise ValueError("Unsafe asset name")
        if e["name"] in names:raise ValueError("Duplicate release asset")
        names.add(e["name"])
        if digest(out/e["name"])!=e["sha256"]:raise ValueError("Package changed after validation")
    unexpected=sorted(p.name for p in out.iterdir() if p.name not in names|{m.name for m in manifests}|set(FINALIZE_OUTPUTS))
    if unexpected:raise ValueError("Not in any package manifest, refusing to publish: "+", ".join(unexpected))
    (out/"release-manifest.json").write_text(json.dumps({"app":config()["app"],"version":version(),"repository":repository,"assets":entries},indent=2)+"\n")
    lines=[]
    for e in entries:
        if (e["name"].endswith(".tar.gz") and e["os"]=="linux") or (e["name"].endswith(".zip") and e["os"] in ("windows","macos")) or e["name"].endswith(".msi"):
            lines.append("\t".join([e["version"],e["os"],e["arch"],"msi" if e["name"].endswith(".msi") else "portable",e["name"],e["sha256"],e["glibc"] or "-","experimental" if e["experimental"] else "stable"]))
    (out/"install-index.tsv").write_text("\n".join(lines)+"\n")
    for n in ("install.sh","install.ps1"):(out/n).write_text((ROOT/n).read_text().replace("@REPOSITORY@",repository))
    (out/"SHA256SUMS.txt").write_text("".join(f"{digest(p)}  {p.name}\n" for p in sorted(out.iterdir()) if p.is_file() and p.name!="SHA256SUMS.txt"))
def main():
    p=argparse.ArgumentParser();sub=p.add_subparsers(dest="command",required=True)
    sub.add_parser("version");sub.add_parser("validate")
    for command in ("bundle-linux","archive","linux-packages","stamp","finalize"):
        a=sub.add_parser(command)
        if command in ("bundle-linux","archive","linux-packages"):a.add_argument("--stage",required=True)
        if command!="bundle-linux":a.add_argument("--out",required=True)
        if command in ("archive","stamp"):a.add_argument("--os",required=True)
        if command in ("archive","stamp","linux-packages"):a.add_argument("--arch",required=True)
        if command=="finalize":a.add_argument("--repository",required=True)
    a=p.parse_args()
    if a.command=="version":print(version())
    elif a.command=="validate":
        c=config();v=version();ref=os.environ.get("GITHUB_REF","")
        if ref.startswith("refs/tags/") and ref!="refs/tags/v"+v:raise ValueError("Tag/version mismatch")
        print(f'{c["app"]} {v}: release configuration valid')
    elif a.command=="bundle-linux":bundle_linux(a.stage);launcher(a.stage)
    elif a.command=="archive":archive_tree(a.stage,a.out,a.os,a.arch)
    elif a.command=="linux-packages":linux_packages(a.stage,a.out,a.arch)
    elif a.command=="stamp":stamp_assets(a.out,a.os,a.arch)
    elif a.command=="finalize":finalize(a.out,a.repository)
if __name__=="__main__":main()
