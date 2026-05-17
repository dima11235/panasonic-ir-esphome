# Panasonic-Ir-ESPHome

Panasonic Climate IR компонент для ESPHome.
Сделано под кондиционер Panasonic CS-E12MKDW.
За основу взята версия кода https://github.com/flight4287/panasonic-air-ir-esphome

## Назначение

Компонент управляет кондиционером Panasonic CS-E12MKDW по ИК через ESPHome и отображается в Home Assistant как `climate`-сущность. Кондиционер получает полный state frame при каждой команде: режим, температуру, скорость вентилятора и положение жалюзи отправляются вместе.

### Особенности работы

- Реальные режимы Panasonic: `OFF`, `AUTO`, `COOL`, `HEAT`, `DRY`.
- `HEAT_COOL` используется как виртуальная команда Home Assistant: при выборе этого режима компонент восстанавливает последний реальный режим кондиционера до выключения. Сам кондиционер отдельного IR-статуса `HEAT_COOL` не имеет. Если предыдущего режима еще нет, используется `COOL`.
- `AUTO` - реальный режим Panasonic, отдельный от виртуального `HEAT_COOL`.
- Температурный диапазон: 16-30 °C, шаг 1 °C.
- В `AUTO` и `DRY` отправляется специальное значение температуры протокола Panasonic, а не обычная уставка.
- Поддерживаемые режимы вентилятора в Home Assistant: `AUTO`, `LOW`, `MEDIUM`, `HIGH`. В протоколе Panasonic есть больше градаций, но сейчас они схлопываются в эти четыре режима.
- Поддерживаемые режимы жалюзи в Home Assistant: `OFF` и `VERTICAL`. `VERTICAL` соответствует автоматическому качанию, `OFF` соответствует фиксированному положению.
- При подключенном `remote_receiver` компонент пытается синхронизировать состояние Home Assistant с оригинальным пультом.

## Прием ИК с пульта

Приемник нужен только для синхронизации состояния, когда кондиционером управляют штатным пультом. Компонент проверяет:

- первый фиксированный кадр Panasonic;
- заголовок второго кадра;
- checksum второго кадра;
- допустимость режима, температуры и байта `fan/swing`.

Если кадр не проходит проверку, состояние Home Assistant не публикуется.

## Важные байты протокола

Текущая реализация работает с 27-байтным сообщением из двух кадров:

- байт `13`: питание и режим;
- байт `14`: температура;
- байт `16`: скорость вентилятора и вертикальные жалюзи;
- байт `26`: checksum второго кадра, сумма байтов `8..25` по модулю 256.

Остальные байты пока считаются фиксированными для CS-E12MKDW. Если нужно добавлять quiet/powerful/econo/timer/profile, сначала стоит снять дампы оригинального пульта и уточнить карту этих байтов.

## Проверка сборки

Локально в этом репозитории код правится и проверяется статически. Полная сборка выполняется в ESPHome add-on Home Assistant.

Если сборка падает, нужен полный лог из ESPHome add-on, включая:

- версию ESPHome;
- YAML устройства;
- весь блок ошибки компиляции C++;
- строку, где подключается `external_components`.

##### YAML для добавления в конфигурацию ESPHome
```
external_components:
  - source: github://dima11235/panasonic-ir-esphome@main
    refresh: 0s
    components: [ panasonic ]

remote_transmitter:
  pin: GPIO14
  carrier_duty_percent: 50%
  
remote_receiver:
  id: receiver
  pin:
    number: GPIO05
    inverted: True
    mode: INPUT_PULLUP
  tolerance: 55%
  
climate:
  - platform: panasonic
    id: my_climate
    name: "Livingroom AC"
    sensor: current_temperature
    receiver_id: receiver

sensor:
  - platform: homeassistant
    id: current_temperature
    entity_id: sensor.livingroom_temperature
    unit_of_measurement: "°C"
    icon: "mdi:temperature"
    device_class: "temperature"
    state_class: "measurement"
    accuracy_decimals: 1

```
