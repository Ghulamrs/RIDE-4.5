@echo off
cd /d C:\cxx1\ccsref
set E=C:\ti\ccsv5\eclipse\eclipsec.exe -noSplash -data C:\cxx1\ccsref\ws55
for %%P in (K6747c K6747cpp) do for %%C in (Debug Release) do %E% -application com.ti.ccstudio.apps.projectBuild -ccs.projects %%P -ccs.configuration %%C -ccs.buildType full > build55-%%P-%%C.txt 2>&1
