@echo off
cd /d C:\cxx1\ccsref
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\cxx1\ccsref\wsprobe7 -application com.ti.ccstudio.apps.createProject -ccs.name Probe -ccs.device bogus > dev7.txt 2>&1
C:\ti\ccsv5\eclipse\eclipsec.exe -noSplash -data C:\cxx1\ccsref\wsprobe55 -application com.ti.ccstudio.apps.projectCreate -ccs.name Probe -ccs.device bogus > dev55.txt 2>&1
