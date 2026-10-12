@echo off
if not exist C:\Users\GRA\work\ccsref mkdir C:\Users\GRA\work\ccsref
cd /d C:\Users\GRA\work\ccsref
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\Users\GRA\work\ccsref\ws7 -application com.ti.ccstudio.apps.projectCreate -ccs.help > help7.txt 2>&1
C:\ti\ccsv7\eclipse\eclipsec.exe -noSplash -data C:\Users\GRA\work\ccsref\ws7 -application com.ti.ccstudio.apps.projectBuild -ccs.help > helpb7.txt 2>&1
C:\ti\ccsv5\eclipse\eclipsec.exe -noSplash -data C:\Users\GRA\work\ccsref\ws55 -application com.ti.ccstudio.apps.projectCreate -ccs.help > help55.txt 2>&1
C:\ti\ccsv5\eclipse\eclipsec.exe -noSplash -data C:\Users\GRA\work\ccsref\ws55 -application com.ti.ccstudio.apps.projectBuild -ccs.help > helpb55.txt 2>&1
