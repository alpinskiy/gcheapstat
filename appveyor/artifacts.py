import os
import re
import zipfile
import subprocess
import shutil

# Globals
name = 'gcheapstat'
configuration = 'Release'
version = os.getenv('APPVEYOR_BUILD_VERSION')

def linux():
  for tag in ['5.0-buster-slim-amd64']:
    bindir = os.path.join('build', tag, configuration)
    # Strip symbols
    symname = name + '.dbg'
    subprocess.call(['objcopy', '--only-keep-debug', name, symname], cwd=bindir)
    subprocess.call(['objcopy', '--strip-debug', '--strip-unneeded', name], cwd=bindir)
    subprocess.call(['objcopy', '--add-gnu-debuglink=' + symname, name], cwd=bindir)
    # Zip
    zipname = 'gcheapstat-' + version + '-dotnet-' + tag + '.zip'
    with zipfile.ZipFile(zipname, 'a', zipfile.ZIP_DEFLATED) as zipf:
      zipf.write(os.path.join(bindir, name), arcname=name)
    zipname_symbols = 'gcheapstat-' + version + '-dotnet-' + tag + '-symbols.zip'
    with zipfile.ZipFile(zipname_symbols, 'a', zipfile.ZIP_DEFLATED) as zipf:
      zipf.write(os.path.join(bindir, symname), arcname=symname)
    # Upload
    subprocess.call(['appveyor', 'PushArtifact', zipname])
    subprocess.call(['appveyor', 'PushArtifact', zipname_symbols])

format_re = re.compile(r'Format: RSDS, {([0-9a-fA-F\-]+)},\s*(\d+)')
def dumpbin(path):
  proc = subprocess.Popen([
    'dumpbin.exe', path, '/headers'], stdout=subprocess.PIPE)
  for line in iter(proc.stdout.readline, ''):
    match = format_re.search(line.decode('utf-8'))
    if match:
      groups = match.groups()
      guid = groups[0].replace('-', '')
      age = int(groups[1])
      return guid, age

def windows():
  zipname = name + '-' + version + '.zip'
  zipname_symbols = name + '-' + version + '-symbols.zip'
  for platform in ['x64', 'x86']:
    bindir = os.path.join('build', 'windows-' + platform, configuration)
    exename = name + '.exe'
    exepath = os.path.join(bindir, exename)
    # Zip
    with zipfile.ZipFile(zipname, 'a', zipfile.ZIP_DEFLATED) as zipf:
      zipf.write(exepath, arcname=name + {'x86':'32','x64':'64'}[platform] + '.exe')
    with zipfile.ZipFile(zipname_symbols, 'a', zipfile.ZIP_DEFLATED) as zipf:
      pdb_name = name + '.pdb'
      guid, age = dumpbin(exepath)
      zipf.write(os.path.join(bindir, pdb_name), r'%s\%s%x\%s' % (pdb_name, guid, age, pdb_name))
  # Upload
  subprocess.call(['appveyor', 'PushArtifact', zipname])
  subprocess.call(['appveyor', 'PushArtifact', zipname_symbols])

if __name__ == '__main__':
  if (os.getenv('APPVEYOR') == 'true'):
    linux()
  else:
    windows()
