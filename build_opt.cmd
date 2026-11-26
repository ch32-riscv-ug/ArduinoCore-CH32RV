@echo off
rem A sketch's build_opt.h into the build directory (platform.txt, recipe.hooks.prebuild on Windows).
rem   build_opt.cmd <sketch folder> <build folder> <file name>
rem The objects are kept between builds (-MMD), and an @file is no dependency, so
rem the copy is replaced only when its contents change - and then the core's,
rem the libraries' and the sketch's objects are deleted, so every file is
rem compiled again with the new options. No sketch build_opt.h: an empty one.
rem No parenthesised blocks: a path with ")" in it (Program Files (x86)) would
rem end one early.
setlocal
set "SRC=%~1\%~3"
set "OUT=%~2"
set "DST=%~2\%~3"
if not exist "%SRC%" goto empty
copy /y "%SRC%" "%DST%.new" >nul
goto compare
:empty
type nul > "%DST%.new"
:compare
if not exist "%DST%" goto replace
fc /b "%DST%.new" "%DST%" >nul 2>&1
if errorlevel 1 goto replace
del /q "%DST%.new"
exit /b 0
:replace
move /y "%DST%.new" "%DST%" >nul
if exist "%OUT%\core" rmdir /s /q "%OUT%\core"
if exist "%OUT%\libraries" rmdir /s /q "%OUT%\libraries"
if exist "%OUT%\sketch" del /s /q "%OUT%\sketch\*.o" >nul 2>&1
exit /b 0
