import importlib.util,json,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('installer',Path(__file__).resolve().parents[1]/'install.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class InstallerTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.home=Path(self.temp.name)
  p=self.home/'.config/openvr/openvrpaths.vrpath';p.parent.mkdir(parents=True);p.write_text('{"external_drivers": []}')
  self.launch=self.home/'.local/bin/recline-mode';self.launch.parent.mkdir(parents=True);self.launch.write_text('original launcher');self.launch.chmod(0o755)
  self.calls=[]
 def run_command(self,args,**kwargs):
  self.calls.append(args)
  if args[0]==m.VRPATH:
   p=self.home/'.config/openvr/openvrpaths.vrpath';data=json.loads(p.read_text());drivers=data['external_drivers']
   if args[1]=='adddriver' and args[2] not in drivers:drivers.append(args[2])
   if args[1]=='removedriver' and args[2] in drivers:drivers.remove(args[2])
   p.write_text(json.dumps(data))
 def test_install_update_uninstall_preserves_original(self):
  m.install(self.home,self.run_command)
  self.assertIn('steam-frame-recline',self.launch.read_text())
  m.install(self.home,self.run_command)
  m.install(self.home,self.run_command,uninstall=True)
  self.assertEqual(self.launch.read_text(),'original launcher')
  self.assertFalse((self.home/'.local/share/steam-frame-recline/recline-mode').exists())
 def test_failure_restores_files(self):
  failed=False
  def run(args,**kwargs):
   nonlocal failed
   if args[0]=='systemctl' and not failed:failed=True;raise RuntimeError('restart failed')
  with self.assertRaisesRegex(RuntimeError,'restart failed'):m.install(self.home,run)
  self.assertEqual(self.launch.read_text(),'original launcher')
  self.assertFalse((self.home/'.local/state/recline-installer/original.json').exists())
 def test_symlink_rejected_before_mutation(self):
  self.launch.unlink();dest=self.home/'untouched';dest.write_text('unchanged');self.launch.symlink_to(dest)
  with self.assertRaisesRegex(RuntimeError,'symlink'):m.install(self.home,self.run_command)
  self.assertEqual(dest.read_text(),'unchanged');self.assertEqual(self.calls,[])
 def test_prior_canonical_registration_restored(self):
  target=self.home/'.local/share/steam-frame-recline'
  self.run_command([m.VRPATH,'adddriver',str(target)])
  m.install(self.home,self.run_command)
  m.install(self.home,self.run_command,uninstall=True)
  state=json.loads((self.home/'.config/openvr/openvrpaths.vrpath').read_text())
  self.assertIn(str(target),state['external_drivers'])
 def test_failed_update_retains_registration(self):
  m.install(self.home,self.run_command)
  failed=False
  def run(args,**kwargs):
   nonlocal failed
   if args[0]=='systemctl' and not failed:failed=True;raise RuntimeError('restart failed')
   self.run_command(args,**kwargs)
  with self.assertRaises(RuntimeError):m.install(self.home,run)
  state=json.loads((self.home/'.config/openvr/openvrpaths.vrpath').read_text())
  self.assertIn(str(self.home/'.local/share/steam-frame-recline'),state['external_drivers'])
  m.install(self.home,self.run_command,uninstall=True)
  self.assertEqual(self.launch.read_text(),'original launcher')
 def test_payload_verifies(self):m.verify_payload()
if __name__=='__main__':unittest.main()
