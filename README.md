# Лабораторная работа №1 — основы 3D-графики

Вариант 11: правильный икосаэдр. Реализация выполнена на C++20 с Vulkan, GLFW и ImGui.

## Что реализовано

- индексированный правильный икосаэдр: 12 вершин, 20 треугольных граней, 30 рёбер;
- один общий vertex/index buffer для двух объектов;
- два независимых object `VkDescriptorSet` и UBO с отдельными model matrix и цветом;
- `std140`-совместимые структуры `GlobalUniforms` и `ObjectUniforms`;
- перспективная Vulkan-проекция с depth range `0..1` и Y-инверсией;
- глубинный тест и back-face culling;
- процедурный цвет вершины от её локальной позиции; выбранный цвет из `ColorEdit3` умножается на этот цвет;
- интерактивное управление переносом, поворотом, масштабом, цветом и параметрами траектории;
- два режима сложной анимации: Lissajous и epicyclic, Play/Pause, скорость, радиус, амплитуда, частота, фаза и Reset;
- CPU-тесты геометрии, матриц, проекции, топологии и часов анимации.

## Зависимости macOS Apple Silicon

Установите Vulkan loader/headers, MoltenVK, validation layers, `glslc`, CMake, Ninja и GLFW. Например, через Homebrew:

```sh
brew install vulkan-headers vulkan-loader vulkan-tools \
  vulkan-validationlayers shaderc glfw ninja
```

Проверка:

```sh
which glslc
which vulkaninfo
vulkaninfo --summary
```

`vulkaninfo` должен показывать физическое устройство Apple через MoltenVK и `VK_LAYER_KHRONOS_validation`.

## Сборка и запуск

Команды выполняются из корня репозитория:

```sh
cmake --preset debug -DBUILD_TESTING=ON
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
./build-debug/vulkan-starter-app
```

CMake компилирует `shaders/lab.vert` и `shaders/lab.frag` в `build-debug/shaders/*.spv` через `glslc` и передаёт приложению путь к ним через `LAB_SHADER_DIR`.

Для проверки SPIR-V:

```sh
spirv-val build-debug/shaders/lab.vert.spv
spirv-val build-debug/shaders/lab.frag.spv
```

## Управление

Панель ImGui находится слева:

- **Play/Pause** — запустить или остановить анимацию;
- **Reset** — сбросить время, позицию и поворот;
- **Speed** — скорость анимации;
- **Selected** — выбрать объект A или B;
- **Tint** — цвет объекта, умножаемый на процедурный цвет вершин;
- **Translation / Rotation / Scale** — преобразования выбранного объекта;
- **Trajectory** — Lissajous или epicyclic;
- **Radius / Amplitude / Frequency / Phase** — параметры траектории.

## Структура

- `source/application.cpp` — Vulkan-ресурсы лабораторной, UBO, descriptor sets, pipeline, ImGui и draw commands;
- `source/scene_math.hpp` — математика, генерация икосаэдра и анимация;
- `shaders/lab.vert`, `shaders/lab.frag` — GLSL 450 shaders;
- `tests/scene_tests.cpp` — автономные CPU-тесты;
- `source/graphics_internal.cpp` — инфраструктура шаблона: instance, surface, swapchain, render pass, depth, submit/present и ImGui.
