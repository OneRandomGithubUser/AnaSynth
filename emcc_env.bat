wt -d %~dp0 powershell -NoExit Add-Content -path (Get-PSReadlineOption).HistorySavePath 'cls\; emcc AnaSynth.cpp -o AnaSynth.js -s USE_BOOST_HEADERS=1 -std=c++20 -lembind -g -sNO_DISABLE_EXCEPTION_CATCHING'