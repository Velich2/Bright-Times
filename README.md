# Bright Times — After Effects plug-in

Временное цветоделение с оптическим наложением кадров (принцип «Harris shutter» /
оптической печати). Эффект берёт целевой кадр и соседние кадры назад и вперёд по
времени (как CC Wide Time), делит их на группы, окрашивает каждую группу своим
цветом с плавным регулируемым затуханием и накладывает. В режиме **Optical**
цветовые фильтры нормируются так, что в сумме дают белый: всё неподвижное
сохраняет исходный цвет, а всё движущееся расслаивается на цветные «призраки».

![preview](docs/preview.png)

*Синтетическая сцена из `tools/preview.cpp`: статичный фон остаётся без изменений, движущийся объект раскладывается в спектр.*

## Параметры

| Параметр | Что делает |
|---|---|
| **Frames Backward** | Сколько кадров брать назад (0–50): числовое поле + слайдер, анимируется ключами. |
| **Frames Forward** | Сколько кадров брать вперёд (0–50), независимо от Backward. |
| **Frame Step** | Шаг между взятыми кадрами (1 = соседние, 3 = каждый третий…). |
| **Include Current Frame** | Участвует ли текущий кадр в наложении. |
| **Keep Current Untinted** | Текущий кадр не окрашивается (белый фильтр) и не входит в группы. |
| **Groups & Colors → Number of Groups** | На сколько цветовых групп делятся кадры (1–16). |
| **Group Order** | *Sequential* — группы идут по времени (прошлое → будущее, как радуга на референсе); *Cyclic* — кадры раздаются группам по кругу (классический RGB-«Harris shutter»). |
| **Group Softness** | Плавный переход цвета между соседними группами (0 % — жёсткие ступени). |
| **Color Mode** | *Spectrum* (Hue Start / Hue Range / Saturation), *RGB Tri-Color*, *Custom Palette* (Palette Size + Color 1…6). |
| **Reverse Colors** | Развернуть порядок цветов. |
| **Falloff → Backward / Forward Falloff** | Сила затухания веса кадров с удалением от текущего, отдельно назад и вперёд. |
| **Falloff Curve** | Форма кривой затухания (<1 — быстрый спад сразу, >1 — дальние кадры гаснут резче). |
| **Tint Mode** | *Color Filter* — канал × цвет (как светофильтр); *Luminance Tint* — яркость × цвет. |
| **Blend Mode** | *Optical (Balanced)* — по-канальная нормировка, статичное не меняется; *Additive*; *Screen*; *Lighten*. |
| **Intensity** | Усиление. |
| **Mix with Original** | Смешивание с исходным кадром. |

## Как устроено

```
src/
  BrightTimesCore.h        математика: список кадров, цвета групп, веса, накопление (без SDK)
  BrightTimes.cpp/.h       AE SmartFX: параметры, PreRender (checkout кадров), SmartRender CPU/GPU
  BrightTimesPiPL.r        PiPL-ресурс (флаги должны совпадать с GlobalSetup)
  gpu/
    BrightTimesKernels.h   ОДИН исходник ядер для CUDA / OpenCL / Metal
    BrightTimesGPU.*       выбор бэкенда
    BrightTimesCUDA.cu     CUDA (nvcc, статически)
    BrightTimesOpenCL.cpp  OpenCL (компиляция ядра при GPU_DEVICE_SETUP)
    BrightTimesMetal.mm    Metal (компиляция ядра при GPU_DEVICE_SETUP)
tests/                     офлайн-тесты: мок-хост AE + заглушки SDK
tools/preview.cpp          офлайн-превью алгоритма
```

* **SmartFX + Multi-Frame Rendering**, 8 / 16 / 32 bpc на CPU.
* **GPU (32-bit float)**: CUDA, OpenCL, Metal — AE сам выбирает, если в *Project Settings → Video Rendering and Effects* стоит Mercury GPU Acceleration. Если фреймворк не поддержан сборкой (например, DirectX), AE автоматически рендерит на CPU.
* Время: `PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT` — AE сам отслеживает зависимость от соседних кадров для кэша.
* Альфа: прозрачность учитывается корректно (накопление в premultiplied, вывод straight).

## Сборка

Нужен **After Effects SDK 2022 или новее** (developer.adobe.com → After Effects → SDK) и CMake ≥ 3.21.

### Windows (Visual Studio 2022)

Нужно: Visual Studio 2022 с «Разработка классических приложений на C++», CMake ≥ 3.21,
для GPU — CUDA Toolkit (даёт и CUDA, и заголовки OpenCL).

1. Склонировать репозиторий и перейти в ветку:
   ```bat
   git clone https://github.com/velich2/bright-times.git
   cd bright-times
   git checkout claude/compassionate-goldberg-4jxvsv
   ```
2. Проверить SDK и инструменты (из корня репозитория):
   ```bat
   powershell -ExecutionPolicy Bypass -File tools\check_sdk.ps1 -SdkRoot "C:\Users\Velich\AfterEffectsSDK_26.5_win"
   ```
3. Собрать — **все команды запускаются в корне репозитория** (папка с `CMakeLists.txt`),
   в «x64 Native Tools Command Prompt for VS 2022» или обычном терминале:
   ```bat
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAE_SDK_ROOT="C:/Users/Velich/AfterEffectsSDK_26.5_win" -DBT_ENABLE_CUDA=ON
   cmake --build build --config Release
   ```
   Без CUDA Toolkit — убрать `-DBT_ENABLE_CUDA=ON`. Папка `Examples` ищется автоматически,
   даже если архив SDK распакован с лишней вложенной папкой; при нехватке файлов CMake
   перечислит, чего не хватает.

* OpenCL включён по умолчанию (нужны заголовки/`OpenCL.lib`, например из CUDA Toolkit или Khronos OpenCL SDK; `OpenCL.dll` грузится отложенно).
* `-DBT_ENABLE_CUDA=ON` — нужен CUDA Toolkit; архитектуры по умолчанию `61;75;86;89` (`-DCMAKE_CUDA_ARCHITECTURES=...`).
* PiPL собирается автоматически: `cl /EP` → `PiPLtool.exe` → `.rc`.

Результат: `build/Release/BrightTimes.aex` → скопировать в
`C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\`.

### macOS (Xcode)

```sh
cmake -S . -B build -G Xcode -DAE_SDK_ROOT=$HOME/AE_SDK
cmake --build build --config Release
```

Metal включён по умолчанию, бандл универсальный (x86_64 + arm64), PiPL собирается через `Rez`.
Результат: `BrightTimes.plugin` → `/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/`
(для распространения — подписать/нотаризовать).

После установки: *Effect → Time → Bright Times*.

## Тесты (без After Effects)

```sh
cmake -S . -B build-tests -DBT_BUILD_TESTS=ON
cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
```

* `cpu_host` — мок-хост прогоняет `EffectMain` (GLOBAL_SETUP → PARAMS_SETUP → UPDATE_PARAMS_UI → PRE_RENDER → SMART_RENDER) в 8/16/32 bpc для всех режимов; проверяет совпадение флагов с PiPL, баланс checkout/checkin и что в Optical статичные пиксели не меняются.
* `opencl_host` — тот же путь на GPU через OpenCL (подойдёт и CPU-реализация, например pocl) и сравнение с CPU: расхождение ≤ 2·10⁻⁷.
* `bt_preview <dir>` — рендер синтетической сцены в PPM.

`tests/sdk_stub/` — минимальные собственные заглушки SDK только для тестов; сам плагин всегда собирается с официальным SDK.
