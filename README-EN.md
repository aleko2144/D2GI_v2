# D2GI v2 - D2 Graphic Improvements v2 

[Читать на русском](README.md)

A slightly modified version of the *D2GI* wrapper (authors of the original project are [REDPOWAR](https://github.com/REDPOWAR) and [NewEXE](https://github.com/NewEXE)). It differs in that the interface size is corrected and also the ability to save screenshots has been fixed (added hooks of the game functions). The innovations are only functional in the game version 8.2. In addition, this project includes the CPatch.h file from [D2InputWrapper](https://github.com/Voron295/rignroll-dinput-wrapper) authored by [Voron295](https://github.com/Voron295).

### Download  

[Download](https://github.com/aleko2144/D2GI_v2/releases)  

### Installation  

You need to unzip the files into a folder with the game.

### Settings  

The `d2gi.ini` file has the following settings. 
`VIDEO` section:
* `Width` - Screen width like `1920` (`0` - auto)  
* `Height` - Screen height like `1080` (`0` - auto)  
* `WindowMode` - Window mode. Possible values: `windowed`, `borderless`, `fullscreen`.
* `EnableVSync` - Turn vertical sync on or off (`1` and `0` accordingly)

`HOOKS` section:
* `EnableHooks` - enable game functions hooking (matrix projection correction with any aspect ratio) 

`SCREENSHOTS` section: 
* `screenshots_path` - the folder where screenshots will be saved
* `image_format` - screenshots format (bmp/png/jpg)
