# cs2_offsets_dumper

Автономный дампер оффсетов Counter-Strike 2 (Source 2, x64, client.dll).
Не инжектит, не хукает, процесс игры читает только в `--source=live`.

## Сборка

```
dumper\build.bat
```

Готовый exe: `bin\cs2_offsets_dumper.exe`. Требуется MSVC x64 (VS 18 Community, vcvars64).
Альтернатива: `cmake -B build -A x64 && cmake --build build --config Release`.

## Запуск

```
cs2_offsets_dumper.exe --dll="C:\...\game\csgo\bin\win64\client.dll"
```

Дефолты: `--out=<exe_dir>\config.json`, `--history-dir=<exe_dir>\config_history`,
схема `--schema=<exe_dir>\schema_client.json` (если файл есть рядом с exe).

Флаги:

| флаг | смысл |
| --- | --- |
| `--source=file` | читать client.dll с диска (дефолт) |
| `--source=live` | читать образ client.dll из cs2.exe (нужен `SeDebugPrivilege`, лучше от админа) |
| `--source=sig` | только сигнатурный скан (CI/офлайн), без live-схемы |
| `--dll=<path>` | путь к client.dll для file/sig |
| `--process=<name>` / `--pid=<n>` | цель для live (дефолт cs2.exe) |
| `--out=<path>` | config.json |
| `--history-dir=<path>` | каталог версий |
| `--schema=<path>` | дамп схемы (netvars) в формате `client_dll.json` |
| `--ini=<path>` | дополнительно записать legacy offsets.ini для текущего оверлея |
| `--verify-only` | ничего не писать, только exit code |
| `--verbose` | подробный лог |

Exit codes: `0` всё резолвнуто и записано (или up to date), `1` часть оффсетов unresolved,
`2` фатальная ошибка (нет файла, битый PE, нет доступа).

## Как это работает

**dw-оффсеты** — пелит-паттерны по `.text` (`48 89 05 ${'}` = RIP-relative операнд,
его target сохраняется как RVA). Все сигнатуры в `src/offsets.cpp` проверены на билде
14197 (2026-11-12). Производные (`dwViewAngles`, `dwLocalPlayerPawn`) считаются от
якорного глобала + смещение из кода рядом — обе ветки в одном паттерне.

**Netvars** — два источника, в порядке приоритета:

1. `--source=live`: обход schema system в памяти процесса
   (`schemasystem.dll` → `SchemaSystem.type_scopes` → `UtlTsHash` → `SchemaClassInfoData`).
2. JSON-дамп схемы (`--schema`, по умолчанию `schema_client.json` рядом с exe).

Нет ни того, ни другого → netvars остаются `unresolved`, exit 1. Нули не пишутся никогда.

**Валидация**: глобалы обязаны попасть в `.data` внутри `SizeOfImage` и быть
8-байтно выровнены; netvars в пределах 0x2000–0x8000 в зависимости от класса;
член-оффсеты (`highestEntityIndex`) в пределах 0x10000. Не прошло — unresolved + лог.

**Уверенность**: `1.0` уникальный паттерн/однозначное поле; `0.95` паттерн нашёлся
несколько раз; `0.8` netvar найден не в том классе, что ожидался; `0.5–0.6` цепочки
и layout-константы (`m_modelState`, `m_boneArray`, `m_aimPunchAngle`).

## Формат config.json

```json
{
  "version": 3,
  "game_build": "<sha256 client.dll>",
  "timestamp": "2026-11-17T05:00:00Z",
  "offsets": {
    "dwEntityList": {
      "value": "0x2709010", "module": "client.dll", "section": ".data",
      "source": "sig", "confidence": 1,
      "sig": "48890d${'} e9${} cc"
    },
    "m_aimPunchAngle": {
      "value": "0x1598", "class": "C_CSPlayerPawn",
      "source": "netvar+chain", "chain": ["0x50"],
      "confidence": 0.6, "note": "CCSPlayer_AimPunchServices::m_predictableBaseAngle"
    }
  }
}
```

`chain` — дополнительные разыменования поверх `value`:
`(pawn + value) -> +chain[0] -> +chain[1] ...`. Пример `m_boneArray`:
`pGameSceneNode(0x330) -> +0x140 (m_modelState) -> +0x80 (bone array)`.

## Автообновление

- `game_build` = SHA-256 client.dll. Совпал с записанным и все оффсеты сошлись —
  файл не трогается (`up to date`, exit 0).
- Иначе: version+1, diff в stderr и `update.log`, атомарная запись
  (`config.json.tmp` → FlushFileBuffers → MoveFileEx REPLACE_EXISTING), история
  в `config_history/vN.json`, ротация последних 10.
- Unresolved оффсеты не затирают прошлые валидные значения: старый value остаётся,
  в `note` пишется причина.

## Обновление сигнатур к новой сборке игры

`tools/sigboot.py` — находит все RIP-relative ссылки на известные оффсеты в свежем
client.dll (задай актуальные значения в таблице `targets`), печатает кандидатов с
контекстом. `tools/testpats.py` — прогоняет текущие паттерны из `src/offsets.cpp`
по dll и сверяет результат с `tools/a2x_offsets.json`.

Порядок после крупного апдейта:

1. `python tools/testpats.py` — какие паттерны сломались.
2. Для сломанных: `python tools/sigboot.py`, выбрать уникальный контекст вокруг
   найденной ссылки, заменить паттерн в `src/offsets.cpp`.
3. Обновить `schema_client.json` свежим дампом схемы (если используете file-режим).
4. Пересобрать, запустить, проверить `--verify-only` → exit 0.

## Интеграция с оверлеем

`--ini=bin\offsets.ini` пишет ключи в формате, который читает текущий
`src/config.cpp` оверлея (`dwEntityList=0x...`). Для chain-оффсетов в ini уходит
последний хоп (`m_boneArray=0x80` — смещение внутри `CModelState`).

Оффсеты, которых нет в этом наборе (`entityChunksOffset`, `entityIdentityStride`,
`dwNetworkGameClient`, `signOnStateOffset`), в ini не пишутся — оверлей остаётся
на своих компилированных дефолтах, пока ключ отсутствует.

## Автообновление чита (--watch)

Дампер может сам держать оффсеты актуальными:

```
cs2_offsets_dumper.exe --source=live --watch=15 ^
  --out="...\dumper\bin\config.json" ^
  --ini="...\bin\offsets.ini"
```

- Каждые N секунд читается только PE-заголовок живого client.dll
  (TimeDateStamp, SizeOfImage, CodeSize) — без чтения 43 МБ образа.
- Полный ресолв запускается только при смене билда или адреса модуля
  (после рестарта игры). Тег билда в live-режиме считается по заголовку, а не по
  всему образу: `.data` мутирует в рантайме и давал ложные «новые билды».
- `offsets.ini` перезаписывается только при реальном изменении содержимого,
  поэтому простаивающий watcher не дёргает чит.
- Один watcher на сессию (мьютекс `Local\cs2_offsets_watch_mutex`).
  Остановка — событие `Local\cs2_offsets_watch_stop` (шлёт чит при выходе).

Чит `cs2_aim_dbg.exe` при старте сам поднимает watcher скрыто
(`..\dumper\bin\cs2_offsets_dumper.exe --source=live --watch=15 ...`) и
перечитывает `offsets.ini` на лету: `config::reloadIfChanged()` сравнивает
mtime/size, валидирует значения и подменяет смещения без рестарта.
