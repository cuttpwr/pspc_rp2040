# PicoStarPowerCalc (PSPC) — Raspberry Pi Pico + LittleFS + Expression Evaluator

**PicoStarPowerCalc (PSPC)** — пользователь **сам
вписывает формулы и статические данные** через USB-клавиатуру (или Serial),
**сохраняет их как pipeline-файлы** в энергонезависимой памяти (LittleFS в
flash), и **исполняет на устройстве**.

Формулы — полноценные арифметические выражения с переменными, условным
оператором (`?:`), сравнениями (`== != < > <= >=`) и функциями
(`sqrt abs min max pow sin cos exp log floor ceil`). Pipeline — текстовый файл,
где строки `var = expr` задают переменные, а `print expr "unit"` — вывод.

## Архитектура

```
USB-клавиатура / Serial
        │
        ▼
    Shell ──── LS / NEW / EDIT / RUN / CAT / DEL / VARS / MEM / HELP
        │           │
        │           ▼
        │      Editor ─── строковый редактор (.s .q .l .d N)
        │           │
        ▼           ▼
    LittleFS (flash) ─── pipeline-файлы: /vodogreyny, /parovoy, ...
        │
        ▼ (RUN: читает файл → парсит → исполняет)
    Pipeline parser + ExprEval (recursive descent)
        │
        ▼
    BoilerTTY (tee: Serial + кольцевой буфер для OLED)
        │
        ▼
    SSD1306 OLED: HOME / EDIT / RUN / TERM + 4 кнопки
```

| Модуль | Назначение |
|---|---|
| `boiler_expr.h/.cpp` | Вычислитель выражений (recursive descent). Pure C++, host-тестирован. |
| `boiler_pipeline.h/.cpp` | Парсер pipeline-текста → операторы; исполнитель с VarTable. |
| `boiler_fs.h/.cpp` | Обёртка LittleFS: list / exists / read / write / delete. |
| `boiler_editor.h/.cpp` | Строковый редактор: дописывает строки, `.команды`, сохранение в flash. |
| `boiler_shell.h/.cpp` | Командный шелл: разбор LS/NEW/EDIT/RUN/... |
| `boiler_display.h/.cpp` | OLED: 4 экрана (HOME/EDIT/RUN/TERM) + 4 кнопки. |
| `boiler_tty.h/.cpp` | Print-subclass: тиражирует вывод в Serial + ring buffer для OLED. |
| `boiler_keyboard.h/.cpp` | USB HID-клавиатура через Pico-PIO-USB (опц.). |
| `boiler_pico.ino` | Точка входа: связывает модули, сидинг демо-pipeline. |

## Формат pipeline

Текстовый файл, построчный. Комментарии — `#`. Пустые строки игнорируются.

```
# Водогрейная котельная v15
# --- статические данные (константы) ---
Qov = 34
Qgv = 10
t1 = 110
t2 = 50
eta = 0.98
Kts = 2
c = 4.19

# --- формулы (RHS вычисляется, результат写到 переменную) ---
Q = Qov + Qgv
G = Q * 1000 / (c * (t1 - t2))
Gpod = G / Kts
dG = abs(G - Gpod) / G * 100

# --- условная логика (тернарный оператор) ---
# scheme = 1  (parallel), 2 (sequential)
scheme = 1
Gnet = (scheme == 1) ? Gpod : G

# --- вывод ---
print "=== Vodogreyny v15 ==="
print Q " МВт"
print G " т/ч"
print Gpod " т/ч"
print dG " %"
print eta
```

### Операторы (приоритет низкий → высокий)

| Уровень | Операторы |
|---|---|
| 1 (низший) | `? :` (тернарный: `cond ? a : b`) |
| 2 | `== != < > <= >=` (возвращают 1/0) |
| 3 | `+ -` |
| 4 | `* /` |
| 5 (высший) | унарный `-` `+`, `(...)`, вызовы функций |

### Функции

| Функция | Аргументы | Пример |
|---|---|---|
| `sqrt` | 1 | `sqrt(16)` → 4 |
| `abs` | 1 | `abs(-7)` → 7 |
| `floor`, `ceil` | 1 | `floor(3.7)` → 3 |
| `sin`, `cos`, `tan` | 1 (рад) | `sin(1.57)` |
| `exp`, `log` | 1 | `exp(1)` → 2.718 |
| `min`, `max` | 2 (через запятую) | `max(3, 7)` → 7 |
| `pow` | 2 | `pow(2, 10)` → 1024 |
| `mod` | 2 | `mod(10, 3)` → 1 |

### Переменные

Буквенно-цифровые + `_`, начинаются с буквы/`_`. До 16 символов.
VarTable вмещает до 96 переменных. Обращение к undefined-переменной — ошибка
с указанием строки.

### Обработка ошибок

- **Парсер**: синтаксические ошибки (`ожидалось '='`, `незакрытая кавычка`, ...)
  с номером строки.
- **Исполнитель**: ошибки выполнения (`неизвестная переменная`, `деление на 0`,
  `неизвестная функция`) с номером строки. Выполнение останавливается.

## Команды шелла

| Команда | Действие |
|---|---|
| `LS` | Список pipeline-файлов в flash (имя + размер) |
| `NEW <name>` | Создать новый pipeline, войти в редактор |
| `EDIT <name>` | Редактировать существующий |
| `RUN <name>` | Исполнить pipeline, вывести результаты |
| `CAT <name>` | Показать содержимое файла |
| `DEL <name>` | Удалить файл |
| `VARS` | Переменные последнего прогона |
| `MEM` | Статистика flash (used / total) |
| `SCREEN home\|edit\|run\|term\|clear` | Управление экраном OLED |
| `HELP` | Справка |

## Редактор (режим EDIT)

После `NEW`/`EDIT` каждая введённая строка дописывается в буфер. Управляющие
команды начинаются с `.`:

| Команда | Действие |
|---|---|
| `.` или `.s` | Сохранить и выйти |
| `.q` | Выйти без сохранения |
| `.l` | Показать строки с номерами |
| `.d N` | Удалить строку N (1-индексация) |
| `.h` | Помощь |

Вместимость: 80 строк × 96 символов. Пустая строка (Enter) — дописывается как
разделитель (игнорируется парсером, но полезна для читаемости).

## Экраны OLED + 4 кнопки

| Кнопка | Пин | Short | Long (>1.2 c) |
|---|---|---|---|
| NEXT | GP15 | след. файл / след. страница | **RUN** выбранного файла |
| PREV | GP14 | пред. файл / пред. страница | **цикл экранов** |
| DEC | GP13 | — | **EDIT** выбранного файла |
| INC | GP12 | — | — |

Экраны (переключаются длинным GP14 по кругу):

- **HOME** — список pipeline-файлов, курсор `>` выбирает. Внизу подсказка.
  GP15 long → RUN, GP13 long → EDIT.
- **EDIT** — состояние редактора: текущая вводимая строка, подсказки `.команд`.
- **RUN** — результаты последнего прогона (7/страница): `label value unit`
  или текстовые заметки. Заголовок: `RESULTS 1/N`.
- **TERM** — 7 строк вывода CLI (с транслитом кириллицы) + `> [ввод]` внизу.

## Аппаратные подключения

| Компонент | Pi Pico | Примечание |
|---|---|---|
| USB CDC (Serial) | встроенный | 115200, всегда |
| SSD1306 SDA | GP4 | I²C 0x3C |
| SSD1306 SCL | GP5 | I²C |
| SSD1306 VCC | 3V3 (не 5V!) | |
| SSD1306 GND | GND | |
| Кнопка NEXT | GP15 → GND | INPUT_PULLUP, active-low |
| Кнопка PREV | GP14 → GND | INPUT_PULLUP, active-low |
| Кнопка DEC | GP13 → GND | INPUT_PULLUP, active-low |
| Кнопка INC | GP12 → GND | INPUT_PULLUP, active-low |
| USB-A клав. D+ | GP0 | + 15kΩ pulldown (опц.) |
| USB-A клав. D- | GP1 | + 15kΩ pulldown (опц.) |

## LittleFS — настройка flash

LittleFS (BSD-licensed, GPL-совместима) входит в ядро Arduino-Pico
(earlephilhower). Flash под FS резервируется при прошивке:

1. В Arduino IDE: **Tools → Flash Size** → выберите размер с FS,
   например **"2MB (Sketch: 1MB, FS: 1MB)"**.
2. При первом запуске `LittleFS.begin(true)` автоматически форматирует FS,
   если она пустая/повреждена.
3. При первом старте (flash пуста) автоматически сеются 3 демо-pipeline:
   `vodogreyny`, `parovoy`, `demo_cond`.

Статистика flash: команда `MEM`.

## Библиотеки

**Обязательно:**
1. **Adafruit SSD1306**
2. **Adafruit GFX Library**
3. Ядро **Arduino-Pico (earlephilhower) ≥ 3.x** — включает LittleFS.

**Для USB-клавиатуры (опционально):**
4. **Pico-PIO-USB** — https://github.com/sekigon-gonnoc/Pico-PIO-USB
5. Build flags: `-DCFG_TUH_ENABLED=1 -DCFG_TUH_HID=4`
6. В `boiler_keyboard.h` раскомментируйте `#define KB_ENABLED`.

## Включение USB-клавиатуры

По умолчанию клавиатура **выключена** — код работает только с Serial.
Чтобы включить:

1. Установить Pico-PIO-USB.
2. В `boiler_keyboard.h`: `#define KB_ENABLED`.
3. Добавить build flags `-DCFG_TUH_ENABLED=1 -DCFG_TUH_HID=4`.
4. Выбрать плату «Raspberry Pi Pico», порт — USB CDC.

Без клавиатуры весь ввод идёт через Serial Monitor (115200), а формулы
набираются в нём же.

## Быстрый старт

1. Прошить (Flash Size с FS).
2. Открыть Serial Monitor (115200).
3. При первом старте увидите:
   ```
   Pipeline Engine v3.0
   Flash пуста — сею демо-pipeline...
     created: vodogreyny
     created: parovoy
     created: demo_cond
   > 
   ```
4. `LS` — увидеть файлы.
5. `RUN vodogreyny` — исполнить:
   ```
   === RUN: vodogreyny ===
   === Vodogreyny v15 ===
   Q = 44 МВт
   G = 175.02 т/ч
   Gpod = 87.51 т/ч
   dG = 50 %
   eta = 0.98
   === OK (5 результатов) ===
   ```
6. `NEW mycalc` — войти в редактор, набрать формулы, `.s` — сохранить.
7. `RUN mycalc` — исполнить свой pipeline.
8. На OLED: HOME показывает список, GP15 long — RUN выбранного.

## Лицензии

- **LittleFS**: BSD-3-Clause (GPL-совместима). Входит в Arduino-Pico core.
- **Adafruit SSD1306/GFX**: BSD.
- **Pico-PIO-USB**: MIT.
- Собственный код проекта: публикуется под GPL (на усмотрение автора).

## Структура проекта

```
Kotelnaya_PiPico_v3/
├── boiler_pico.ino       точка входа: setup/loop + сидинг демо
├── boiler_expr.h/.cpp    вычислитель выражений (recursive descent)
├── boiler_pipeline.h/.cpp парсер pipeline + исполнитель
├── boiler_fs.h/.cpp      обёртка LittleFS
├── boiler_editor.h/.cpp  строковый редактор
├── boiler_shell.h/.cpp   командный шелл
├── boiler_display.h/.cpp OLED: 4 экрана + 4 кнопки
├── boiler_tty.h/.cpp     tee-Print (Serial + ring buffer)
├── boiler_keyboard.h/.cpp USB HID-клавиатура (опц.)
└── README.md             этот файл
```


Данный проект частично содержит код сгеренированный ИИ.