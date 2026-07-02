@echo off
setlocal enabledelayedexpansion

echo Kerem valassza ki a konvertalando fajlt:
echo.

:: Fajlok listazasa tombkent
set count=0
for %%f in (*.mjpeg) do (
    set /a count+=1
    set "file[!count!]=%%f"
    echo !count!. %%f
)

if %count%==0 (
    echo Nem talalhato mjpeg fajl a mappaban.
    pause
    exit
)

echo.
set /p choice="Adja meg a szamot: "

:: Ellenorzes: letezik-e a megadott index
if defined file[%choice%] (
    set "target=!file[%choice%]!"
    
    :: Kiterjesztes nelkuli nev kinyerese
    for %%a in ("!target!") do set "basename=%%~na"
    
    echo.
    echo Konvertalas folyamatban: "!target!" -> "!basename!.mp4"
    echo.
    
    ffmpeg -framerate 1 -i "!target!" -c:v libx264 -pix_fmt yuv420p "!basename!.mp4"
    
    echo.
    echo ---------------------------
    echo Keszen van!
) else (
    echo Hibas valasztas.
)

pause