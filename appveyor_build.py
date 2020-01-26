import os
import sys
import zipfile
import subprocess

def zip_binary(zipname, platform, configuration):
  if platform == 'x86':
    name = 'gcheapstat32.exe'
  elif platform == "x64":
    name = 'gcheapstat64.exe'
  else:
    sys.exit('Expected platform either x86 or x64')
  path = os.path.join("out", platform, configuration, "gcheapstat.exe")
  zipf = zipfile.ZipFile(zipname, 'a', zipfile.ZIP_DEFLATED)
  zipf.write(path, name)
  zipf.close()
  return zipname

def build(platform, configuration):
  subprocess.call(['msbuild',
    'gcheapstat.sln',
    '/p:Platform=' + platform,
    '/p:Configuration=' + configuration,
    '/l:C:\\Program Files\\AppVeyor\\BuildAgent\\Appveyor.MSBuildLogger.dll'])

def push_artifact(name):
  subprocess.call(['appveyor', 'PushArtifact', name])

if __name__ == '__main__':
  print(sys.argv)
  build('x86', 'Release')
  build('x64', 'Release')
  tag = os.getenv('APPVEYOR_REPO_TAG_NAME') or ''
  zipname = 'gcheapstat' + tag + '.zip';
  zip_binary(zipname, 'x86', 'Release')
  zip_binary(zipname, 'x64', 'Release')
  push_artifact(zipname)
