@echo off
cd /d C:\cxx1\ccsref
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\cxx1\ccsref\ws7 -application com.ti.ccstudio.apps.buildProject -ccs.projects P7misc -ccs.configuration Debug -ccs.buildType full > %1 2>&1
