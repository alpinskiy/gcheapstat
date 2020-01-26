import os
import sys
import zipfile
import subprocess

def zip_binary(platform, configuration):
  if platform == 'x86':
    name = 'gcheapstat32.exe'
  elif platform == "x64":
    name = 'gcheapstat64.exe'
  else:
    sys.exit('Expected platform either x86 or x64')
  path = os.path.join("out", platform, configuration, "gcheapstat.exe")
  zipf = zipfile.ZipFile('gcheapstat' + os.getenv('APPVEYOR_REPO_TAG_NAME') + '.zip', 'a', zipfile.ZIP_DEFLATED)
  zipf.write(path, name)
  zipf.close()

def build(platform, configuration):
  subprocess.call(['msbuild',
    'gcheapstat.sln',
    '/p:Platform=' + platform,
    '/p:Configuration=' + configuration,
    '/l:C:\\Program Files\\AppVeyor\\BuildAgent\\Appveyor.MSBuildLogger.dll'])

if __name__ == '__main__':
  print(sys.argv)
  build('x86', 'Release')
  build('x64', 'Release')
  zip_binary('x86', 'Release')
  zip_binary('x64', 'Release')
