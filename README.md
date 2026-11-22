# ModernFpsBoost

Глубокая оптимизация FPS для **GTA:SA 1.0 US** + **SA-MP 0.3.7 R1 / R2 / R3-1 / R4 / R5-1** и **0.3.DL-R1**.
Каждый фикс вынесен в отдельную секцию `ModernFpsBoost.ini` (`enabled=1/0`) и управляется прямо в игре через меню **F9**.

Разработчик: [Softik Inquisitor](https://vk.ru/miurawr)

## Скачать

* **Прямая ссылка (последняя сборка CI):** [ModernFpsBoostBySoftikInquisitor.vk-miurawr.asi](https://github.com/xKumorio/ModernFpsBoost/releases/latest/download/ModernFpsBoostBySoftikInquisitor.vk-miurawr.asi)
* **Страница релизов:** [Releases](https://github.com/xKumorio/ModernFpsBoost/releases)
* **Сборки из исходников:** [GitHub Actions](https://github.com/xKumorio/ModernFpsBoost/actions)

### Установка
1. Поместите `ModernFpsBoostBySoftikInquisitor(vk-miurawr).asi` в корневую папку GTA:SA (рядом с `gta_sa.exe`).
2. `ModernFpsBoost.ini` со всеми описаниями создастся автоматически при первом запуске.
3. В игре нажмите **F9**, чтобы открыть меню настроек, живую статистику фиксов и встроенные профайлеры.

## Основные оптимизации

* **GTA:SA (SSE2 / SSE3 / SSE4.1 и движок):**
  * Коллизии лучей, сфер, треугольников, боксов и отрезков на SSE2 (`ColTriSSE`, `ColBoxSSE`, `ColLinesSSE`, `ColPlaneCache`)
  * Матрицы RenderWare и `CMatrix`, фрустум камеры, скиннинг и скелетная анимация на SSE2 (`MatrixSSE`, `CMatrixSSE`, `FrustumSSE`, `AnimMathSSE`, `AnimBlendSSE`, `FrameSyncFast`, `SkinMatrixSSE`)
  * Математика частиц `Fx_c`, `atan2`, `floor/ceil` (SSE4.1), `(int)float` (SSE3), `stricmp` (`FxParticlesSSE`, `FastAtan2SSE`, `FloorSSE4`, `FtolSSE3`, `FastStricmpSSE`)
  * Отключение программного CPU-occlusion 2004 года (`OcclusionOff`), расширенный пул ОЗУ стриминга до 3.5 ГБ с авто-проверкой LAA (`StreamMemBoost`), бюджет распаковки (`StreamBudget`), быстрый поиск моделей (`ModelNameLookup`)
  * Умный опрос очереди сообщений Windows с мгновенным пробуждением по `GetInputState` (`MessagePumpFix`), троттлинг `SetCursorPos` (`CursorCenterFix`), батчинг радара (`RadarBatch`), кеш отражений машин (`EnvMapCache`), LOD машин и дальность прорисовки (`VehicleLod`, `DrawDistance`, `PedDrawDistance`)
* **SA-MP (все 6 версий: 0.3.7 R1–R5 и 0.3.DL-R1):**
  * Кеширование текста `ID3DXFont::DrawText` и шрифтов материалов (`TextCache`, `MaterialFontCache`)
  * Батчинг полосок HP/брони в один вызов `DrawPrimitiveUP` (`HealthBarBatch`), лёгкий StateBlock вместо `D3DSBT_ALL` (`SampStateBlockFix`), ленивый `Begin/End` тегов (`PlayerTagState`)
  * Быстрый `CObjectPool::DrawLast` без обнуления 8 КБ и пузырьковой сортировки (`ObjectDrawLastFast`), SSE2-проверка 3D-текста на объектах (`ObjectMatTextFast`), отсечение объектов и ганг-зон вне экрана (`SampObjectCull`, `GangZoneCull`)
  * Кеш проверки видимости ников (`NametagLOSCache`), кеш поиска объектов и аксессуаров (`ObjectFindCache`, `AccessoryFindCache`), кеш `VirtualProtect` (`UnprotectCache`), кеш опроса F1/F5/F10 (`SampKeyPollCache`), снятие фризов `Sleep(1)` при загрузке моделей (`SampModelWait`)
* **Совместимость со сторонними модами:**
  * Ускорение прослойки `SAMPFUNCS` (`SampfuncsDrawFast`), убран мёртвый цикл `CObject::Render` в `SilentPatchSA` (`SilentPatchObjectFix`), кеш `VirtualProtect` для `MoonLoader` и ASI-плагинов (`LuaMemProtectCache`, `AsiMemProtectCache`), фикс `Sleep(1)` в BASS (`AudioSleepFix`), отключение лога `RealSkybox` (`SkyboxLogOff`).

## Сборка из исходников

Требуется Visual Studio 2022 (компонент C++ x86):

```bat
build.bat
```

Готовый файл появится в `build\ModernFpsBoostBySoftikInquisitor(vk-miurawr).asi`.

---

© 2026 [Softik Inquisitor](https://vk.ru/miurawr). Все права защищены.
