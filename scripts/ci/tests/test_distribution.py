import importlib.util,json,os,platform,shutil,struct,subprocess,sys,tarfile,tempfile,unittest,zipfile
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location("distribution",ROOT/"scripts/ci/distribution.py")
d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)
class ReleaseContractTests(unittest.TestCase):
 def setUp(self):
  self.actual_version=d.version();self.version_patch=patch.object(d,"version",return_value="1.0.0");self.version_patch.start();self.temp=tempfile.TemporaryDirectory();self.t=Path(self.temp.name)
 def tearDown(self):self.temp.cleanup();self.version_patch.stop()
 def test_zip_path_traversal_is_rejected(self):
  z=self.t/"bad.zip"
  with zipfile.ZipFile(z,"w") as f:f.writestr("../escaped","bad")
  with self.assertRaises(ValueError):d.extract(z,self.t/"extract")
  self.assertFalse((self.t/"escaped").exists())
 def test_application_version_is_stable(self):
  self.assertRegex(self.actual_version,r"^[0-9]+[.][0-9]+[.][0-9]+$")
 def test_archive_round_trip_preserves_payload_and_top_level_name(self):
  stage=self.t/"stage";(stage/"bin").mkdir(parents=True);(stage/"bin/payload").write_bytes(b"portable-runtime")
  out=self.t/"out";d.archive_tree(stage,out,"linux","arm64")
  name=d.config()["app"]+"-1.0.0-linux-arm64"
  with tarfile.open(out/(name+".tar.gz")) as t:
   item=t.extractfile(name+"/bin/payload");self.assertEqual(item.read(),b"portable-runtime")
  with zipfile.ZipFile(out/(name+".zip")) as z:self.assertEqual(z.read(name+"/bin/payload"),b"portable-runtime")
 def test_installer_catalog_has_one_download_per_system_cpu_kind(self):
  out=self.t/"release";out.mkdir();entries=[]
  for system,cpu,ext in [("windows","x64","zip"),("windows","arm64","msi"),("linux","x64","tar.gz"),("linux","arm64","tar.gz"),("macos","arm64","zip")]:
   name=f'{d.config()["app"]}-1.0.0-{system}-{cpu}.{ext}';p=out/name;p.write_bytes((system+cpu).encode())
   entries.append(dict(name=name,sha256=d.digest(p),os=system,arch=cpu,version="1.0.0",experimental=False,glibc="2.35" if system=="linux" else None))
  (out/"manifest-test.json").write_text(json.dumps(entries))
  d.finalize(out,"example/test-repository")
  rows=[s.split("\t") for s in (out/"install-index.tsv").read_text().splitlines()]
  self.assertEqual(len(rows),5);self.assertEqual(len({tuple(r[1:4]) for r in rows}),5)
  self.assertTrue(all(len(r)==8 and len(r[5])==64 for r in rows))
  self.assertIn('example/test-repository',(out/"install.sh").read_text())
  self.assertNotIn("'example/test-repository'|OWNER",(out/"install.sh").read_text())
 def test_tampered_candidate_is_not_finalized(self):
  out=self.t/"release";out.mkdir();p=out/f'{d.config()["app"]}-1.0.0-windows-x64.zip';p.write_bytes(b"original")
  e=dict(name=p.name,sha256=d.digest(p),os="windows",arch="x64",version="1.0.0",experimental=False,glibc=None)
  (out/"manifest-test.json").write_text(json.dumps([e]));p.write_bytes(b"tampered")
  with self.assertRaises(ValueError):d.finalize(out,"example/test")
 def test_stamp_lists_only_release_assets_and_finalize_succeeds(self):
  out=self.t/"release";out.mkdir();app=d.config()["app"];base=f"{app}-1.0.0-windows-arm64"
  assets=[base+s for s in (".zip",".tar.gz",".msi","-setup.exe")]
  for n in assets+[base+".wixpdb",base+".msi.log",base+".zip.part",f"{app}-1.0.0-windows-x64.msi"]:(out/n).write_bytes(n.encode())
  d.stamp_assets(out,"windows","arm64")
  names=[e["name"] for e in json.loads((out/"manifest-windows-arm64.json").read_text())]
  self.assertEqual(sorted(names),sorted(assets))
  # Upload only what CI's upload-artifact globs keep: the stray files are left behind.
  # The other target's MSI has no manifest here, and finalize refuses unlisted files.
  for n in (base+".wixpdb",base+".msi.log",base+".zip.part",f"{app}-1.0.0-windows-x64.msi"):(out/n).unlink()
  d.finalize(out,"example/test")
  self.assertEqual(len(json.loads((out/"release-manifest.json").read_text())["assets"]),4)
 def test_finalize_refuses_files_no_manifest_lists(self):
  out=self.t/"release";out.mkdir();app=d.config()["app"];base=f"{app}-1.0.0-windows-x86"
  for n in (base+".zip",base+".msi",base+".wixpdb",base+".pdb",base+".msi.log",base+".zip.part"):(out/n).write_bytes(n.encode())
  d.stamp_assets(out,"windows","x86")
  with self.assertRaisesRegex(ValueError,"refusing to publish: .*wixpdb"):d.finalize(out,"example/test")
  self.assertFalse((out/"SHA256SUMS.txt").exists())
 def test_finalize_refuses_stray_zips_that_match_the_upload_globs(self):
  # Each of these passes the *.zip upload glob, so finalize is what keeps it out.
  app=d.config()["app"];base=f"{app}-1.0.0-windows-x64"
  strays=(f"{app}-0.9.0-windows-x64.zip",base+"-debug.zip",f"{app}-1.0.0-windows-riscv64.zip")
  for stray in strays:
   with self.subTest(unlisted=stray):
    out=self.t/("unlisted-"+stray);out.mkdir()
    for n in (base+".zip",base+".msi",stray):(out/n).write_bytes(n.encode())
    d.stamp_assets(out,"windows","x64")
    self.assertNotIn(stray,[e["name"] for e in json.loads((out/"manifest-windows-x64.json").read_text())])
    with self.assertRaisesRegex(ValueError,"refusing to publish: "+stray.replace(".","[.]")+"$"):d.finalize(out,"example/test")
    self.assertFalse((out/"SHA256SUMS.txt").exists())
  # A stray manifest cannot vouch for them either: only this version's configured names pass.
  for stray,version,system,cpu in ((strays[0],"0.9.0","windows","x64"),(strays[0],"1.0.0","windows","x64"),(strays[1],"1.0.0","windows","x64"),(strays[2],"1.0.0","windows","riscv64")):
   with self.subTest(listed=stray,version=version):
    out=self.t/(f"listed-{version}-"+stray);out.mkdir()
    for n in (base+".zip",stray):(out/n).write_bytes(n.encode())
    d.stamp_assets(out,"windows","x64");p=out/stray
    (out/f"manifest-stray-{system}-{cpu}.json").write_text(json.dumps([dict(name=stray,sha256=d.digest(p),size=p.stat().st_size,os=system,arch=cpu,version=version,glibc=None,experimental=False)]))
    with self.assertRaisesRegex(ValueError,"Not a 1[.]0[.]0 release asset, refusing to publish: "+stray.replace(".","[.]")):d.finalize(out,"example/test")
    self.assertFalse((out/"SHA256SUMS.txt").exists())
 def test_finalize_accepts_every_configured_asset_name(self):
  # The name check must not refuse anything build.py legitimately stamps.
  c=d.config();out=self.t/"release";out.mkdir();entries=[]
  for n in sorted(d.release_asset_names(c,"1.0.0")):
   p=out/n;p.write_bytes(n.encode());entries.append(dict(name=n,sha256=d.digest(p),os="source" if n.endswith("source.tar.gz") else n.split("-")[2],arch="all",version="1.0.0",experimental=False,glibc=None))
  (out/"manifest-all.json").write_text(json.dumps(entries));d.finalize(out,"example/test")
  self.assertEqual(len(json.loads((out/"release-manifest.json").read_text())["assets"]),len(entries))
 def test_release_architecture_overrides_are_validated(self):
  with patch.dict(os.environ,RELEASE_ARCH="made-up"):
   with self.assertRaises(ValueError):d.arch()
 @unittest.skipUnless(os.name=="nt","Windows PowerShell fixture")
 def test_windows_installer_download_and_checksum_failure_are_isolated(self):
  app=d.config()["app"];fixtures=self.t/"fixtures";fixtures.mkdir();dest=self.t/"downloads";dest.mkdir()
  asset=f"{app}-1.0.0-windows-arm64.zip"
  with zipfile.ZipFile(fixtures/asset,"w") as z:z.writestr("fixture","test")
  row="\t".join(["1.0.0","windows","arm64","portable",asset,d.digest(fixtures/asset),"-","stable"])+"\n"
  (fixtures/"install-index.tsv").write_text(row)
  command=["powershell.exe","-NoProfile","-ExecutionPolicy","Bypass","-File",str(ROOT/"install.ps1"),"-Repository","example/test","-Portable","-DownloadOnly","-Architecture","arm64","-FixtureDirectory",str(fixtures),"-DownloadDirectory",str(dest)]
  r=subprocess.run(command,capture_output=True,text=True)
  self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual((dest/asset).read_bytes(),(fixtures/asset).read_bytes())
  (fixtures/asset).write_bytes(b"corrupt");r=subprocess.run(command,capture_output=True,text=True)
  self.assertNotEqual(r.returncode,0);self.assertIn("SHA256",r.stdout+r.stderr)
 @unittest.skipUnless(os.name!="nt" and shutil.which("sh"),"POSIX installer fixture")
 def test_unix_download_requires_no_python_or_jq_and_checks_sha(self):
  app=d.config()["app"];fixtures=self.t/"fixtures";fixtures.mkdir();dest=self.t/"download";dest.mkdir();asset=f"{app}-1.0.0-linux-arm64.tar.gz"
  (fixtures/asset).write_bytes(b"archive-only-fixture")
  (fixtures/"install-index.tsv").write_text("\t".join(["1.0.0","linux","arm64","portable",asset,d.digest(fixtures/asset),"2.35","stable"])+"\n")
  cmd=["sh",str(ROOT/"install.sh"),"--repo","example/test","--fixture-directory",str(fixtures),"--fixture-os","linux","--arch","arm64","--download-only","--download-dir",str(dest)]
  r=subprocess.run(cmd,capture_output=True,text=True)
  self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertTrue((dest/asset).is_file())
  (fixtures/asset).write_bytes(b"tampered");r=subprocess.run(cmd,capture_output=True,text=True);self.assertNotEqual(r.returncode,0)

 def test_mislabeled_native_architectures_are_rejected(self):
  binary=self.t/"native"
  for cpu,elf_class,machine in [("x64",2,62),("arm64",2,183),("x86",1,3),("armv7",1,40)]:
   header=bytearray(64);header[:6]=b"\x7fELF"+bytes([elf_class,1]);struct.pack_into("<H",header,18,machine);binary.write_bytes(header)
   d.verify_binary(binary,"linux",cpu)
   with self.assertRaises(ValueError):d.verify_binary(binary,"linux","arm64" if cpu!="arm64" else "x64")
  binary.write_bytes(b"\xcf\xfa\xed\xfe"+struct.pack("<I",0x100000c)+bytes(56))
  d.verify_binary(binary,"macos","arm64")
  with self.assertRaises(ValueError):d.verify_binary(binary,"macos","x64")


 @unittest.skipUnless(os.name=="nt","Windows installer authoring fixture")
 def test_shared_start_menu_has_product_specific_uninstall_and_safe_cleanup(self):
  import xml.etree.ElementTree as ET
  config=d.config();app=config["app"];ns={"w":"http://schemas.microsoft.com/wix/2006/wi"}
  self.assertEqual(config["startMenuFolder"],"8bitPrograms")
  for cpu,machine,folder in [("x64",0x8664,"System64Folder"),("arm64",0xaa64,"System64Folder"),("x86",0x14c,"SystemFolder")]:
   with self.subTest(cpu=cpu):
    stage=self.t/cpu;out=self.t/("output-"+cpu);(stage/"bin").mkdir(parents=True)
    header=bytearray(512);header[:2]=b"MZ";struct.pack_into("<I",header,0x3c,0x80);header[0x80:0x84]=b"PE\0\0";struct.pack_into("<H",header,0x84,machine)
    (stage/"bin"/(app+".exe")).write_bytes(header)
    files=["bin/Apply-Update.ps1","bin/Qt6Core.dll","bin/msvcp140.dll","bin/vcruntime140.dll","bin/qt.conf","plugins/platforms/qwindows.dll","Start-Portable.cmd","profile/settings.ini"]
    files+=["bin/"+n for n in (["Qt6Qml.dll","Qt6Quick.dll","Qt6Network.dll","Qt6Svg.dll","puttygen.exe","openssh/ssh.exe","openssh/sftp.exe","openssh/ssh-keygen.exe"] if app=="8bitShell" else ["8p.exe","8bitpad.exe","8bitCodeFileIcons.dll","Qt6Widgets.dll","Qt6Network.dll","Qt6Svg.dll","Qt6Core5Compat.dll"])]
    for name in files:
     p=stage/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b"fixture")
    command=["powershell.exe","-NoProfile","-ExecutionPolicy","Bypass","-File",str(ROOT/"scripts/ci/package-windows.ps1"),"-Stage",str(stage),"-OutputDirectory",str(out),"-Architecture",cpu,"-GenerateOnly"]
    env=os.environ.copy();env["BIT8_PYTHON"]=sys.executable
    path_key=next(key for key in env if key.lower()=="path")
    env[path_key]=str(Path(sys.executable).parent)+os.pathsep+env[path_key]
    result=subprocess.run(command,capture_output=True,text=True,env=env)
    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    product=ET.parse(result.stdout.strip().splitlines()[-1]).getroot().find("w:Product",ns)
    self.assertEqual(product.get("Manufacturer"),"8bitPrograms")
    self.assertEqual(product.find("w:Package",ns).get("InstallScope"),"perUser")
    path_entry=product.find(".//w:Environment",ns)
    self.assertIsNotNone(path_entry)
    self.assertEqual(path_entry.attrib,dict(Id="UserCommandPathValue",Name="PATH",Action="set",Part="last",System="no",Permanent="no",Value="[INSTALLDIR]bin"))
    menu=product.find('.//w:Directory[@Id="ProgramMenuFolder"]/w:Directory[@Id="MenuDir"]',ns)
    self.assertEqual(menu.get("Name"),"8bitPrograms")
    shortcuts=product.findall(".//w:Shortcut",ns);self.assertEqual(len(shortcuts),2)
    launch=next(x for x in shortcuts if x.get("Id")=="AppShortcut")
    remove=next(x for x in shortcuts if x.get("Id")=="UninstallShortcut")
    self.assertEqual(launch.get("Name"),app);self.assertEqual(launch.get("Target"),"[#APP_EXE]")
    self.assertEqual(remove.get("Name"),"Uninstall "+app)
    self.assertEqual(remove.get("Target"),"["+folder+"]msiexec.exe")
    self.assertEqual(remove.get("Arguments"),"/x [ProductCode]")
    self.assertEqual(product.find('w:Property[@Id="ARPNOMODIFY"]',ns).get("Value"),"1")
    cleanup=product.findall(".//w:RemoveFolder",ns)
    self.assertTrue(all(x.get("On")=="uninstall" for x in cleanup))
    self.assertFalse(any(x.get("Directory") in ("Programs","ProgramMenuFolder","LocalAppDataFolder") for x in cleanup))
    self.assertFalse(product.findall(".//w:RemoveFile",ns))
    payload=[x.get("Source") for x in product.findall(".//w:File",ns)]
    icon_server=product.find('.//w:Component[@Id="PadFileIconHandler"]',ns)
    if app=="8bitCode":
     self.assertTrue(any(Path(x).name=="8bitCodeFileIcons.dll" for x in payload))
     self.assertIsNotNone(icon_server)
     self.assertEqual(icon_server.get("Win64"),"no" if cpu=="x86" else "yes")
     registry=icon_server.findall("w:RegistryValue",ns)
     self.assertEqual(len(registry),3)
     self.assertTrue(all(x.get("Root")=="HKCU" for x in registry))
     self.assertTrue(all(x.get("Key").startswith("Software\\Classes\\CLSID\\{D325870F-4B1C-4D26-A03C-3575B1607635}") for x in registry))
     server=next(x for x in registry if x.get("KeyPath")=="yes")
     self.assertEqual(server.get("Value"),"[INSTALLDIR]bin\\8bitCodeFileIcons.dll")
     self.assertEqual(next(x for x in registry if x.get("Name")=="ThreadingModel").get("Value"),"Apartment")
    else:
     self.assertIsNone(icon_server)
    self.assertFalse(any("profile" in Path(x).parts or Path(x).name=="Start-Portable.cmd" for x in payload))

if __name__=="__main__":unittest.main()
