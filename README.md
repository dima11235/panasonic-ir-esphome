# Panasonic-Ir-ESPHome

Panasonic Climat IR компонент для ESP Home.
Сделано под кондиционер Panasonic CS-E12MKDW.
За основу взята версия кода https://github.com/flight4287/panasonic-air-ir-esphome

##### YAML для добавления в конфигурацию ESP Home
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
