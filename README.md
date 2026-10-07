# Factorio Proto

Упрощённый прототип Factorio на C++ / raylib.

## Что умеет
- Меню (New Game / Settings / Exit)
- Добыча руды, конвейеры, плавильни
- Волны врагов, турели
- HUD, победа, поражение

## Управление
- 1 — Бур
- 2 — Конвейер
- 3 — Плавильня
- 4 — Турель
- 5 — Удалить
- R — повернуть
- U — апгрейд бура
- P — пауза
- ESC — в меню

## Сборка (Windows, w64devkit + raylib)
g++ main.cpp -o game.exe -I/raylib/raylib/src -L/raylib/raylib/src -lraylib -lopengl32 -lgdi32 -lwinmm -std=c++17
./game.exe

## Стек
- C++17
- raylib 5.x
