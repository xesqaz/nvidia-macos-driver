# Карта переноса GP107 для macOS 15

Это план следующего этапа отдельного форка `xesqaz/nvidia-macos-driver`. Ветка нацелена на настольную GTX 1050 GP107 и macOS 15 Sequoia x86_64. Карта модулей ниже помогает изучать порядок и зависимости Nouveau; это не готовая последовательность записей в регистры и не инструкция по слепому воспроизведению трассы.

## Что уже подтверждено

- Наша карта — PCI `10de:1c81`, subsystem `1458:3765`, GP107 revision A1; Linux-чтение `BOOT_0` дало `0x137000a1`.
- На этой же карте Linux NVK выполнил отдельные compute- и render-тесты с нулём неправильных результатов. Это доказывает работоспособность GPU под Linux, но ничего пока не доказывает про macOS.
- На Linux-хосте установлены файлы GP107 FECS/GPCCS и graphics context, перечисленные Nouveau. `sw_bundle_init` и `sw_method_init` в установленном пакете — ссылки в каталог GP102. Бинарники остаются локальными и в репозиторий не попадают; см. [baseline](BASELINE.md) и лицензию пакета на хосте.
- `PascalProbe.kext` пока только исходник. Его ещё не собрали и не проверили на Sequoia; теперь он намеренно читает идентификатор, `BOOT_0` и два снимка PTIMER, не записывая в GPU.

## Карта исходников Nouveau

| Область | Отправная точка | Что изучаем для GP107 |
|---|---|---|
| Определение чипа | [`engine/device/base.c`](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/drivers/gpu/drm/nouveau/nvkm/engine/device/base.c) | Привязку PCI ID и выбор семейств устройств. |
| Graphics / FECS / GPCCS | [`engine/gr/gp107.c`](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/drivers/gpu/drm/nouveau/nvkm/engine/gr/gp107.c) | Таблицу операций GP107, контекст, классы Pascal и ACR-загрузчики FECS/GPCCS. Файл перечисляет 12 firmware-файлов; базовая топология указана как 2 GPC, 3 TPC и 1 PPC, до применения fuse-конфигурации конкретного кристалла. |
| VRAM и framebuffer | [`subdev/fb/gp100.c`](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/drivers/gpu/drm/nouveau/nvkm/subdev/fb/gp100.c) | Семейство FB/RAM для Pascal, включая общую инициализацию памяти, страницы и remapper. Это отдельная сложная ступень после простого доступа к BAR0. |
| Очереди команд | [`engine/fifo/gp100.c`](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/drivers/gpu/drm/nouveau/nvkm/engine/fifo/gp100.c) | Runlist/каналы семейства GP100; используется Pascal GPFIFO class и ряд общих GK104/GM200 операций. |
| Таймер | [`subdev/timer/gk20a.c`](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/drivers/gpu/drm/nouveau/nvkm/subdev/timer/gk20a.c), [`nv04.c`](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/drivers/gpu/drm/nouveau/nvkm/subdev/timer/nv04.c), [`regsnv04.h`](https://github.com/torvalds/linux/blob/32c52ec87d6245ee2d12ed5dd509e40062939b50/drivers/gpu/drm/nouveau/nvkm/subdev/timer/regsnv04.h) | GP107 использует `gk20a_timer_new`; чтение времени делегируется общей реализации с регистрами `0x9400` и `0x9410`. В probe это только две ограниченные read-only выборки. |
| Системная память | `subdev/mmu/` и BAR/instmem модули Nouveau | DMA-адреса, таблицы страниц и контекст памяти. Их нельзя восстановить только из MMIO-трассы. |

Исходники Nouveau — Linux reference. До переноса любого фрагмента кода нужно проверить его лицензию и совместимость с лицензией проекта. В этом форке пока используются ссылки на исходники и независимые наблюдения; код Nouveau не скопирован.

## Соседний эксперимент Pascal-eGPU

Публичный [TheTom/pascal-egpu](https://github.com/TheTom/pascal-egpu) полезен как параллельное исследование, но это не драйвер для нашей карты. Его README подтверждает перечисление и BAR0-доступ к GTX 1060 GP106, чтение `BOOT_0=0x136000a1` и работающий таймер. PMU/ACR, MMU, FIFO и compute там отмечены как следующие или запланированные этапы. GTX 1050 GP107 присутствует в заявленном списке совместимости, но не в таблице испытанного оборудования.

В [описании ограничений](https://github.com/TheTom/pascal-egpu/blob/main/FINDINGS.md) автор связывает остановку Apple Silicon eGPU с тем, что прошивка Mac не запускает VBIOS Option ROM через Thunderbolt. На нашем настольном ПК GPU подключена напрямую к PCIe, а захваченный VBIOS содержит EFI-образ. Это убирает одно конкретное отличие среды, но само по себе ещё не доказывает, какие именно стадии POST выполнила плата или что macOS сможет продолжить инициализацию.

## Графика для установки Sequoia

Процессор i3-4330 содержит Intel HD 4600, но наша Linux-инвентаризация пока не показала её отдельным PCI-устройством. До собственного драйвера GTX 1050 это возможный запасной видеовыход: проверить в BIOS настройку встроенной графики и разъём монитора на материнской плате. Это только проверка конфигурации — менять BIOS или систему автоматически нельзя.

У HD 4600 есть отдельное ограничение по версии ОС: [руководство OpenCore](https://dortania.github.io/OpenCore-Install-Guide/config.plist/haswell.html) указывает её поддержку до macOS 12, а [документация OCLP](https://dortania.github.io/OpenCore-Legacy-Patcher/POST-INSTALL.html) относит Haswell graphics к root patching для Sequoia и требует подходящий MetallibSupportPkg. OCLP официально поддерживает перечисленные модели Intel Mac ([список моделей](https://dortania.github.io/OpenCore-Legacy-Patcher/MODELS.html)), поэтому его работа на этой обычной PC-сборке не гарантирована. Пока Pascal backend не заработал, видеоускорение Sequoia остаётся отдельной проблемой даже при доступном экране.

## Порядок следующей работы

1. Установить отдельную тестовую macOS 15 и собрать диагностический модуль на совместимом SDK. Первый критерий — точный PCI ID, `BOOT_0=0x137000a1` и увеличение PTIMER после загрузки без зависания.
2. Только после этого расширять диагностику ограниченными чтениями безопасных регистров и состояния после UEFI POST. Не писать в GPU и не пытаться проигрывать trace.
3. Составить отдельный проверяемый план FECS/GPCCS и ACR для GP107, сверяя Nouveau, локальный Linux firmware и состояние платы после POST. Не включать firmware blobs в GitHub; способ их поставки должен пройти лицензионную проверку.
4. Идти по зависимостям: память/VRAM → GPU page tables и MMU → FIFO/GPFIFO → минимальный compute-тест с проверкой результата. Отдельно потребуется графический scanout и интеграция с macOS WindowServer/Metal.

Минимальная целевая победа ещё до Metal — маленькая команда реально исполняется на нашей GTX 1050 в Sequoia, результат сверяется с CPU, а система переживает повторный тест и перезагрузку. До такой проверки у нас есть валидированная Linux-точка отсчёта и диагностический каркас, но не рабочий macOS-драйвер.
