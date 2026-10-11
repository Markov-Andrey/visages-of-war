# Памятка агенту: командные маски

Код обслуживания ассетов; в игру не встраивается. Используй готовый процесс вместо нового разового скрипта.

1. Посмотри PNG и маски похожего ассета. Красный — командная ткань; кожу, волосы, металл и фон сохраняй.
2. Скопируй [шаблон профиля](scripts/mask-profiles/template.json). Укажи `assetRoot` от корня проекта,
   в `jobs` — исходники, маски и размеры. [Послушница](scripts/mask-profiles/novice.json) — проверенный пример.
3. Запусти [общий обработчик](scripts/make-team-masks.ps1) командой ниже. Он создаёт белые альфа-маски
   и превью четырёх цветов в `out/team-masks/<профиль>/`; исходники не меняет.
4. Проверь все кадры, смерть, иконку и портрет. Лишнее исключи через `excludePolygons`
   (`"100,200 180,200 170,280"`); красный фон — через `backgroundSeedRects` (`[x,y,w,h]`).
   Пороги красного — `selection`, допустимый заход на контур — `edge`. Координаты — пиксели исходника.
5. Повтори обработку с `-Overwrite`. Ручную очистку сохраняй в профиле, а не только в PNG.
   `-PreviewOnly` проверяет существующие маски без перезаписи. SHA-256 масок в существующем манифесте обновляется автоматически.
6. Подключи `teamMask` кадров/смерти/стадий здания, `iconMask`, `portraitMask` в соответствующих JSON.
   Сохрани происхождение арта по [ASSETS.md](docs/ASSETS.md); контракт кадров — [UNIT_SPRITES.md](docs/UNIT_SPRITES.md).
7. Проверь изменение и упакуй билд. `-Package` сам обновляет ярлыки; готовая игра — `out/package/RTS/`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/make-team-masks.ps1 -Profile scripts/mask-profiles/novice.json -Overwrite
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Debug -Test
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release -Package
```

Алгоритм: [TeamMaskAuthoring.cs](scripts/lib/TeamMaskAuthoring.cs). PowerShell 5.1 + встроенный System.Drawing;
ИИ/Python не нужны. Профиль адаптируется к новому рисунку; автоматическое выделение всегда проверяй глазами.
Сборка копирует готовые маски, но не перегенерирует их. Превью и временные файлы оставляй в `out/`.
