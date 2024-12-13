# D2GIv2 - D2 Graphic Improvements v2

[Read on English](README-EN.md)

Слегка доработанный вариант враппера *D2GI* (авторы оригинального проекта - [REDPOWAR](https://github.com/REDPOWAR) и [NewEXE](https://github.com/NewEXE)). Отличается тем, что исправлена возможность сохранения скриншотов (добавлен перехват соответствующей функции), но только в игре версии 8.2 (в 8.1 сохранение скриншотов работать не будет). Кроме того, в данный проект включён файл CPatch.h из [D2InputWrapper](https://github.com/Voron295/rignroll-dinput-wrapper) за авторством [Voron295](https://github.com/Voron295).  

### Скачать  

[Скачать](https://github.com/aleko2144/D2GI_v2/releases)  

### Установка  

Нужно распаковать файлы в папку с игрой.

### Настройки в файле d2gi.ini  
 
Секция `VIDEO`:  
* `Width` - ширина изображения (`0` - автоматически)  
* `Height` - высота изображения (`0` - автоматически)  
* `WindowMode` - режим окна. `windowed` - оконный, `borderless` - окно без рамки, `fullscreen` - полноэкранный  
* `EnableVSync` - включить или выключить вертикальную синхронизацию  

Секция `HOOKS`:  
* `EnableHooks` - включить перехват функций игры (нужно для коррекции матрицы проекции при произвольном соотношении сторон)

Секция `SCREENSHOTS`: 
* `screenshots_path` - папка, в которую будут сохранены скриншоты
* `image_format` - формат скриншотов (bmp/png/jpg)
