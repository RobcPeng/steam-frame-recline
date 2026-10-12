#!/usr/bin/env python3
"""User-local Steam Frame installer; no root or per-game changes."""
import argparse, base64, hashlib, json, os, platform, shlex, subprocess
from pathlib import Path

SOURCE = Path(__file__).resolve().parent
VRPATH = '/opt/steamvr/bin/linuxarm64/vrpathreg'

def atomic(path, data, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.next')
    temp.write_bytes(data); temp.chmod(mode); temp.replace(path)

def snapshot(paths):
    result = {}
    for p in paths:
        if p.is_symlink():
            raise RuntimeError(f'Refusing to replace symlink: {p}')
        result[str(p)] = None if not p.exists() else {
            'data': base64.b64encode(p.read_bytes()).decode(), 'mode': p.stat().st_mode & 0o777}
    return result

def restore(files):
    for name, saved in files.items():
        p = Path(name)
        if saved is None: p.unlink(missing_ok=True)
        else: atomic(p, base64.b64decode(saved['data']), saved['mode'])

def verify_payload(source=SOURCE):
    expected = json.loads((source/'payload/SHA256.json').read_text())
    for name in ('recline-mode', 'driver_recline_shared.so'):
        if hashlib.sha256((source/'payload'/name).read_bytes()).hexdigest() != expected[name]:
            raise RuntimeError(f'Payload checksum mismatch: {name}')

def install(home, run=subprocess.run, source=SOURCE, uninstall=False):
    home = Path(home)
    target = home/'.local/share/steam-frame-recline'
    record = home/'.local/state/recline-installer/original.json'
    launcher = home/'.local/bin/recline-mode'
    desktop = home/'.local/share/applications/local.recline-mode.desktop'
    arm = home/'.local/state/recline-mode/recline-shared.enable'
    control = home/'.config/openvr/config/recline-driver-control.txt'
    paths = [target/'recline-mode', target/'bin/linuxarm64/driver_recline_shared.so',
             target/'driver.vrdrivermanifest', launcher, desktop, arm]
    def command(*args): return run(list(args), check=True)
    def restart(): command('systemctl', '--user', 'restart', 'steamvr.service')
    def reset():
        import time
        # Ask an existing panel to stop renewing its prior command, then replace control.
        import socket
        sock=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM)
        try: sock.sendto(b'restore',str(home/'.local/state/recline-mode/control.sock'))
        except OSError: pass
        finally: sock.close()
        atomic(control, f'{time.time_ns()} reset 0 0 {int(time.time())}\n'.encode(),0o600)
    if uninstall:
        if not record.exists(): raise RuntimeError('No saved installation to uninstall')
        saved=json.loads(record.read_text()); reset()
        command(VRPATH,'removedriver',str(target)); restore(saved['files'])
        for path in saved['drivers']: command(VRPATH,'adddriver',path)
        restart(); record.unlink()
        print('Removed Recline and restored the previous launchers and registration.'); return
    verify_payload(source)
    registered=json.loads((home/'.config/openvr/openvrpaths.vrpath').read_text()).get('external_drivers') or []
    old=[]
    for name in registered:
        p=Path(name)
        if p==target: continue
        manifest=p/'driver.vrdrivermanifest'
        if manifest.exists() and json.loads(manifest.read_text()).get('name')=='recline_shared': old.append(name)
    before=snapshot(paths)
    original_exists=record.exists()
    if not original_exists: atomic(record,json.dumps({'files':before,'drivers':old+([str(target)] if str(target) in registered else [])}).encode(),0o600)
    try:
        reset()
        for name in old: command(VRPATH,'removedriver',name)
        for name,dst in [('recline-mode',paths[0]),('driver_recline_shared.so',paths[1])]:
            atomic(dst,(source/'payload'/name).read_bytes(),0o755)
        atomic(paths[2],json.dumps({'alwaysActivate':True,'name':'recline_shared','directory':'','resourceOnly':False,'hmd_presence':[]}).encode())
        atomic(arm,b'full\n',0o600)
        atomic(launcher,('#!/bin/sh\nexec '+shlex.quote(str(target/'recline-mode'))+' "$@"\n').encode(),0o755)
        escaped=str(launcher).replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$')
        atomic(desktop,('[Desktop Entry]\nType=Application\nName=Recline Mode\nExec="'+escaped+'"\nIcon=preferences-desktop-display\nTerminal=false\nCategories=Game;\n').encode())
        command(VRPATH,'adddriver',str(target)); restart()
    except BaseException:
        # Roll back this transaction, retaining the original uninstall snapshot on updates.
        run([VRPATH,'removedriver',str(target)],check=False)
        restore(before)
        for name in old: command(VRPATH,'adddriver',name)
        if str(target) in registered: command(VRPATH,'adddriver',str(target))
        if not original_exists: record.unlink(missing_ok=True)
        restart(); raise
    print('Recline Mode installed. Open it from Applications. SteamVR has restarted.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uninstall',action='store_true')
    parser.add_argument('--check',action='store_true',help='Check this Frame and payload without installing')
    args=parser.parse_args()
    if platform.system()!='Linux' or platform.machine() not in ('aarch64','arm64'):
        raise RuntimeError('This build is for Linux ARM64 Steam Frame only')
    if os.geteuid()==0: raise RuntimeError('Run as your normal Steam user, without sudo')
    if not Path(VRPATH).exists(): raise RuntimeError('Expected SteamVR at /opt/steamvr')
    if not args.uninstall:
        verify_payload()
        for name in ('recline-mode','driver_recline_shared.so'):
            result=subprocess.run(['ldd',str(SOURCE/'payload'/name)],capture_output=True,text=True,check=True)
            if 'not found' in result.stdout: raise RuntimeError(result.stdout)
    if args.check: print('Frame runtime and payload checks passed.'); return
    install(Path.home(),uninstall=args.uninstall)

if __name__=='__main__':
    try: main()
    except Exception as e: raise SystemExit(f'Recline installer: {e}')
