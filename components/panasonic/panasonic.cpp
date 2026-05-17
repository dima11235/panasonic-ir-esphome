#include "panasonic.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/log.h"

#include <cstring>
#include <cstdio>

namespace esphome {
namespace panasonic {

static const char *const TAG = "panasonic.climate";

static const uint8_t PANASONIC_FIRST_FRAME_SIZE = 8;
static const uint8_t PANASONIC_SECOND_FRAME_OFFSET = 8;
static const uint8_t PANASONIC_MODE_INDEX = 13;
static const uint8_t PANASONIC_TEMPERATURE_INDEX = 14;
static const uint8_t PANASONIC_FAN_SWING_INDEX = 16;
static const uint8_t PANASONIC_CHECKSUM_INDEX = PANASONIC_STATE_FRAME_SIZE - 1;

static const char *const PANASONIC_CUSTOM_FAN_1 = "1";
static const char *const PANASONIC_CUSTOM_FAN_2 = "2";
static const char *const PANASONIC_CUSTOM_FAN_3 = "3";
static const char *const PANASONIC_CUSTOM_FAN_4 = "4";
static const char *const PANASONIC_CUSTOM_FAN_5 = "5";

static const char *const PANASONIC_CUSTOM_PRESET_SWING_HIGHEST = "swing_highest";
static const char *const PANASONIC_CUSTOM_PRESET_SWING_HIGH = "swing_high";
static const char *const PANASONIC_CUSTOM_PRESET_SWING_MIDDLE = "swing_middle";
static const char *const PANASONIC_CUSTOM_PRESET_SWING_LOW = "swing_low";
static const char *const PANASONIC_CUSTOM_PRESET_SWING_LOWEST = "swing_lowest";

static const uint8_t PANASONIC_STATE_TEMPLATE[PANASONIC_STATE_FRAME_SIZE] = {
    0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06, 0x02, 0x20, 0xE0, 0x04, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00};

static uint8_t panasonic_checksum(const uint8_t frame[]) {
  uint8_t checksum = 0;
  for (uint8_t i = PANASONIC_SECOND_FRAME_OFFSET; i < PANASONIC_CHECKSUM_INDEX; i++) {
    checksum += frame[i];
  }
  return checksum;
}

static bool panasonic_read_byte(remote_base::RemoteReceiveData *data, uint8_t *byte) {
  *byte = 0;
  for (int8_t bit = 0; bit < 8; bit++) {
    if (data->expect_item(PANASONIC_BIT_MARK, PANASONIC_ONE_SPACE)) {
      *byte |= 1 << bit;
    } else if (!data->expect_item(PANASONIC_BIT_MARK, PANASONIC_ZERO_SPACE)) {
      return false;
    }
  }
  return true;
}

static void panasonic_log_frame(const uint8_t frame[]) {
  char buffer[PANASONIC_STATE_FRAME_SIZE * 3] = {};
  char *pos = buffer;
  size_t remaining = sizeof(buffer);
  for (uint8_t i = 0; i < PANASONIC_STATE_FRAME_SIZE; i++) {
    const int written = std::snprintf(pos, remaining, "%s%02X", i == 0 ? "" : " ", static_cast<unsigned>(frame[i]));
    if (written < 0 || static_cast<size_t>(written) >= remaining) {
      break;
    }
    pos += written;
    remaining -= written;
  }
  ESP_LOGI(TAG, "Panasonic IR dump: %s", buffer);
}

static bool panasonic_string_is(StringRef mode, const char *expected) {
  return mode.size() == std::strlen(expected) && std::strncmp(mode.c_str(), expected, mode.size()) == 0;
}

PanasonicClimate::PanasonicClimate()
    : climate_ir::ClimateIR(PANASONIC_TEMP_MIN, PANASONIC_TEMP_MAX, 1.0f, true, false,
                            {climate::CLIMATE_FAN_AUTO},
                            {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}),
      previous_mode(climate::CLIMATE_MODE_OFF) {
  this->set_supported_custom_fan_modes({PANASONIC_CUSTOM_FAN_1, PANASONIC_CUSTOM_FAN_2, PANASONIC_CUSTOM_FAN_3,
                                        PANASONIC_CUSTOM_FAN_4, PANASONIC_CUSTOM_FAN_5});
  this->set_supported_custom_presets({PANASONIC_CUSTOM_PRESET_SWING_HIGHEST, PANASONIC_CUSTOM_PRESET_SWING_HIGH,
                                      PANASONIC_CUSTOM_PRESET_SWING_MIDDLE, PANASONIC_CUSTOM_PRESET_SWING_LOW,
                                      PANASONIC_CUSTOM_PRESET_SWING_LOWEST});
}

climate::ClimateTraits PanasonicClimate::traits() {
  auto traits = climate_ir::ClimateIR::traits();
  traits.add_supported_mode(climate::CLIMATE_MODE_AUTO);
  return traits;
}

void PanasonicClimate::control(const climate::ClimateCall &call) {
  auto mode = call.get_mode();
  if (mode.has_value())
    this->mode = *mode;
  auto target_temperature = call.get_target_temperature();
  if (target_temperature.has_value())
    this->target_temperature = *target_temperature;
  if (call.has_custom_fan_mode()) {
    this->set_custom_fan_mode_(call.get_custom_fan_mode());
  } else {
    auto fan_mode = call.get_fan_mode();
    if (fan_mode.has_value())
      this->set_fan_mode_(*fan_mode);
  }
  auto swing_mode = call.get_swing_mode();
  if (swing_mode.has_value()) {
    this->swing_mode = *swing_mode;
    if (this->swing_mode == climate::CLIMATE_SWING_VERTICAL) {
      this->clear_custom_preset_();
    }
  }
  if (call.has_custom_preset()) {
    this->set_custom_preset_(call.get_custom_preset());
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  } else {
    auto preset = call.get_preset();
    if (preset.has_value())
      this->set_preset_(*preset);
  }
  this->transmit_state();
  this->publish_state();
}

void PanasonicClimate::transmit_state() {
  uint8_t remote_state[PANASONIC_STATE_FRAME_SIZE];
  std::memcpy(remote_state, PANASONIC_STATE_TEMPLATE, sizeof(remote_state));

  remote_state[PANASONIC_MODE_INDEX] = this->operation_mode_();
  remote_state[PANASONIC_TEMPERATURE_INDEX] = this->temperature_();
  remote_state[PANASONIC_FAN_SWING_INDEX] = this->fan_swing_();
  remote_state[PANASONIC_CHECKSUM_INDEX] = panasonic_checksum(remote_state);

  auto transmit = this->transmitter_->transmit();
  auto data = transmit.get_data();
  data->set_carrier_frequency(PANASONIC_IR_FREQUENCY);

  data->mark(PANASONIC_HEADER_MARK);
  data->space(PANASONIC_HEADER_SPACE);

  for (uint8_t i = 0; i < PANASONIC_FIRST_FRAME_SIZE; i++) {
    for (uint8_t mask = 1; mask > 0; mask <<= 1) {
      data->mark(PANASONIC_BIT_MARK);
      const bool bit = remote_state[i] & mask;
      data->space(bit ? PANASONIC_ONE_SPACE : PANASONIC_ZERO_SPACE);
    }
  }

  data->mark(PANASONIC_BIT_MARK);
  data->space(PANASONIC_PAUSE);

  data->mark(PANASONIC_HEADER_MARK);
  data->space(PANASONIC_HEADER_SPACE);

  for (uint8_t i = PANASONIC_SECOND_FRAME_OFFSET; i < PANASONIC_STATE_FRAME_SIZE; i++) {
    for (uint8_t mask = 1; mask > 0; mask <<= 1) {
      data->mark(PANASONIC_BIT_MARK);
      const bool bit = remote_state[i] & mask;
      data->space(bit ? PANASONIC_ONE_SPACE : PANASONIC_ZERO_SPACE);
    }
  }
  data->mark(PANASONIC_BIT_MARK);
  data->space(0);
  transmit.perform();
}

uint8_t PanasonicClimate::operation_mode_() {
  // HEAT_COOL is a virtual Home Assistant command: restore the last real Panasonic mode.
  if (this->mode == climate::CLIMATE_MODE_HEAT_COOL) {
    this->mode = (this->previous_mode != climate::CLIMATE_MODE_OFF) ? this->previous_mode
                                                                    : climate::CLIMATE_MODE_COOL;
    ESP_LOGD(TAG, "Restoring previous mode for HEAT_COOL: %d", static_cast<int>(this->mode));
  }

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    return PANASONIC_MODE_OFF;
  }

  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
      this->previous_mode = this->mode;
      return PANASONIC_MODE_ON | PANASONIC_MODE_COOL;
    case climate::CLIMATE_MODE_DRY:
      this->previous_mode = this->mode;
      return PANASONIC_MODE_ON | PANASONIC_MODE_DRY;
    case climate::CLIMATE_MODE_HEAT:
      this->previous_mode = this->mode;
      return PANASONIC_MODE_ON | PANASONIC_MODE_HEAT;
    case climate::CLIMATE_MODE_AUTO:
      this->previous_mode = this->mode;
      return PANASONIC_MODE_ON | PANASONIC_MODE_AUTO;
    default:
      ESP_LOGW(TAG, "Unsupported mode %d, sending OFF", static_cast<int>(this->mode));
      return PANASONIC_MODE_OFF;
  }
}

uint8_t PanasonicClimate::fan_swing_() {
  uint8_t fan_swing = PANASONIC_FAN_AUTO;
  if (this->has_custom_fan_mode()) {
    auto custom_fan = this->get_custom_fan_mode();
    if (panasonic_string_is(custom_fan, PANASONIC_CUSTOM_FAN_1)) {
      fan_swing = PANASONIC_FAN_1;
    } else if (panasonic_string_is(custom_fan, PANASONIC_CUSTOM_FAN_2)) {
      fan_swing = PANASONIC_FAN_2;
    } else if (panasonic_string_is(custom_fan, PANASONIC_CUSTOM_FAN_3)) {
      fan_swing = PANASONIC_FAN_3;
    } else if (panasonic_string_is(custom_fan, PANASONIC_CUSTOM_FAN_4)) {
      fan_swing = PANASONIC_FAN_4;
    } else if (panasonic_string_is(custom_fan, PANASONIC_CUSTOM_FAN_5)) {
      fan_swing = PANASONIC_FAN_5;
    }
  } else if (this->fan_mode.has_value()) {
    switch (this->fan_mode.value()) {
      case climate::CLIMATE_FAN_AUTO:
      default:
        fan_swing = PANASONIC_FAN_AUTO;
        break;
    }
  }

  switch (this->swing_mode) {
    case climate::CLIMATE_SWING_VERTICAL:
      fan_swing |= PANASONIC_SWING_AUTO;
      break;
    case climate::CLIMATE_SWING_OFF:
    default:
      if (this->has_custom_preset()) {
        auto custom_preset = this->get_custom_preset();
        if (panasonic_string_is(custom_preset, PANASONIC_CUSTOM_PRESET_SWING_HIGHEST)) {
          fan_swing |= PANASONIC_SWING_HIGHEST;
        } else if (panasonic_string_is(custom_preset, PANASONIC_CUSTOM_PRESET_SWING_HIGH)) {
          fan_swing |= PANASONIC_SWING_HIGH;
        } else if (panasonic_string_is(custom_preset, PANASONIC_CUSTOM_PRESET_SWING_MIDDLE)) {
          fan_swing |= PANASONIC_SWING_MIDDLE;
        } else if (panasonic_string_is(custom_preset, PANASONIC_CUSTOM_PRESET_SWING_LOW)) {
          fan_swing |= PANASONIC_SWING_LOW;
        } else if (panasonic_string_is(custom_preset, PANASONIC_CUSTOM_PRESET_SWING_LOWEST)) {
          fan_swing |= PANASONIC_SWING_LOWEST;
        } else {
          fan_swing |= PANASONIC_SWING_HIGHEST;
        }
      } else {
        fan_swing |= PANASONIC_SWING_HIGHEST;
      }
      break;
  }
  return fan_swing;
}

uint8_t PanasonicClimate::temperature_() {
  // Auto and dry use the Panasonic special value instead of a normal set point.
  switch (this->mode) {
    case climate::CLIMATE_MODE_AUTO:
    case climate::CLIMATE_MODE_DRY:
      return 0xC0;
    default:
      uint8_t temperature = (uint8_t) roundf(clamp<float>(this->target_temperature, PANASONIC_TEMP_MIN, PANASONIC_TEMP_MAX));
      return temperature << 1;
  }
}

bool PanasonicClimate::parse_state_frame_(const uint8_t frame[]) {
  for (uint8_t i = 0; i < PANASONIC_FIRST_FRAME_SIZE; i++) {
    if (frame[i] != PANASONIC_STATE_TEMPLATE[i]) {
      ESP_LOGV(TAG, "Unexpected first frame byte %u: 0x%02X", static_cast<unsigned>(i),
               static_cast<unsigned>(frame[i]));
      return false;
    }
  }
  for (uint8_t i = PANASONIC_SECOND_FRAME_OFFSET; i < PANASONIC_SECOND_FRAME_OFFSET + 4; i++) {
    if (frame[i] != PANASONIC_STATE_TEMPLATE[i]) {
      ESP_LOGV(TAG, "Unexpected second frame header byte %u: 0x%02X", static_cast<unsigned>(i),
               static_cast<unsigned>(frame[i]));
      return false;
    }
  }

  const uint8_t checksum = panasonic_checksum(frame);
  if (frame[PANASONIC_CHECKSUM_INDEX] != checksum) {
    ESP_LOGV(TAG, "Checksum mismatch: expected 0x%02X, got 0x%02X", static_cast<unsigned>(checksum),
             static_cast<unsigned>(frame[PANASONIC_CHECKSUM_INDEX]));
    return false;
  }

  const uint8_t mode = frame[PANASONIC_MODE_INDEX];
  climate::ClimateMode parsed_mode;
  float parsed_target_temperature = this->target_temperature;
  climate::ClimateSwingMode parsed_swing_mode = this->swing_mode;
  bool parsed_custom_preset = false;
  const char *parsed_custom_preset_mode = nullptr;
  bool parsed_custom_fan = false;
  const char *parsed_custom_fan_mode = nullptr;
  climate::ClimateFanMode parsed_fan_mode = climate::CLIMATE_FAN_AUTO;
  if (this->fan_mode.has_value()) {
    parsed_fan_mode = this->fan_mode.value();
  }

  if (mode & PANASONIC_MODE_ON) {
    switch (mode & 0xF0) {
      case PANASONIC_MODE_COOL:
        parsed_mode = climate::CLIMATE_MODE_COOL;
        break;
      case PANASONIC_MODE_DRY:
        parsed_mode = climate::CLIMATE_MODE_DRY;
        break;
      case PANASONIC_MODE_HEAT:
        parsed_mode = climate::CLIMATE_MODE_HEAT;
        break;
      case PANASONIC_MODE_AUTO:
        parsed_mode = climate::CLIMATE_MODE_AUTO;
        break;
      default:
        ESP_LOGV(TAG, "Unsupported received mode byte: 0x%02X", static_cast<unsigned>(mode));
        return false;
    }
  } else {
    parsed_mode = climate::CLIMATE_MODE_OFF;
  }

  const uint8_t temperature = frame[PANASONIC_TEMPERATURE_INDEX];
  if (!(temperature & 0xC0)) {
    const uint8_t decoded_temperature = temperature >> 1;
    if (decoded_temperature < PANASONIC_TEMP_MIN || decoded_temperature > PANASONIC_TEMP_MAX) {
      ESP_LOGV(TAG, "Ignoring received temperature out of range: %u", static_cast<unsigned>(decoded_temperature));
      return false;
    }
    parsed_target_temperature = decoded_temperature;
  }

  const uint8_t fan_swing = frame[PANASONIC_FAN_SWING_INDEX];

  switch (fan_swing & 0x0F) {
    case PANASONIC_SWING_AUTO:
      parsed_swing_mode = climate::CLIMATE_SWING_VERTICAL;
      break;
    case PANASONIC_SWING_HIGHEST:
      parsed_swing_mode = climate::CLIMATE_SWING_OFF;
      parsed_custom_preset = true;
      parsed_custom_preset_mode = PANASONIC_CUSTOM_PRESET_SWING_HIGHEST;
      break;
    case PANASONIC_SWING_HIGH:
      parsed_swing_mode = climate::CLIMATE_SWING_OFF;
      parsed_custom_preset = true;
      parsed_custom_preset_mode = PANASONIC_CUSTOM_PRESET_SWING_HIGH;
      break;
    case PANASONIC_SWING_MIDDLE:
      parsed_swing_mode = climate::CLIMATE_SWING_OFF;
      parsed_custom_preset = true;
      parsed_custom_preset_mode = PANASONIC_CUSTOM_PRESET_SWING_MIDDLE;
      break;
    case PANASONIC_SWING_LOW:
      parsed_swing_mode = climate::CLIMATE_SWING_OFF;
      parsed_custom_preset = true;
      parsed_custom_preset_mode = PANASONIC_CUSTOM_PRESET_SWING_LOW;
      break;
    case PANASONIC_SWING_LOWEST:
      parsed_swing_mode = climate::CLIMATE_SWING_OFF;
      parsed_custom_preset = true;
      parsed_custom_preset_mode = PANASONIC_CUSTOM_PRESET_SWING_LOWEST;
      break;
    default:
      parsed_swing_mode = climate::CLIMATE_SWING_OFF;
      break;
  }

  switch (fan_swing & 0xF0) {
    case PANASONIC_FAN_1:
    case PANASONIC_FAN_SILENT:
      parsed_custom_fan = true;
      parsed_custom_fan_mode = PANASONIC_CUSTOM_FAN_1;
      break;
    case PANASONIC_FAN_2:
      parsed_custom_fan = true;
      parsed_custom_fan_mode = PANASONIC_CUSTOM_FAN_2;
      break;
    case PANASONIC_FAN_3:
      parsed_custom_fan = true;
      parsed_custom_fan_mode = PANASONIC_CUSTOM_FAN_3;
      break;
    case PANASONIC_FAN_4:
      parsed_custom_fan = true;
      parsed_custom_fan_mode = PANASONIC_CUSTOM_FAN_4;
      break;
    case PANASONIC_FAN_5:
      parsed_custom_fan = true;
      parsed_custom_fan_mode = PANASONIC_CUSTOM_FAN_5;
      break;
    case PANASONIC_FAN_AUTO:
      parsed_fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
    default:
      ESP_LOGV(TAG, "Unsupported received fan nibble: 0x%02X", static_cast<unsigned>(fan_swing & 0xF0));
      return false;
  }

  this->mode = parsed_mode;
  this->target_temperature = parsed_target_temperature;
  this->swing_mode = parsed_swing_mode;
  if (parsed_custom_preset) {
    this->set_custom_preset_(parsed_custom_preset_mode);
  } else {
    this->clear_custom_preset_();
  }
  if (parsed_custom_fan) {
    this->set_custom_fan_mode_(parsed_custom_fan_mode);
  } else {
    this->set_fan_mode_(parsed_fan_mode);
  }
  if (this->mode != climate::CLIMATE_MODE_OFF) {
    this->previous_mode = this->mode;
  }

  if (this->has_custom_fan_mode() || this->has_custom_preset()) {
    auto custom_fan = this->get_custom_fan_mode();
    auto custom_preset = this->get_custom_preset();
    ESP_LOGD(TAG, "Received state: mode=%d target=%.1f custom_fan=%.*s preset=%.*s swing=%d",
             static_cast<int>(this->mode),
             this->target_temperature, static_cast<int>(custom_fan.size()), custom_fan.c_str(),
             static_cast<int>(custom_preset.size()), custom_preset.c_str(),
             static_cast<int>(this->swing_mode));
  } else {
    ESP_LOGD(TAG, "Received state: mode=%d target=%.1f fan=%d swing=%d", static_cast<int>(this->mode),
             this->target_temperature, static_cast<int>(this->fan_mode.value()), static_cast<int>(this->swing_mode));
  }
  this->publish_state();
  return true;
}

bool PanasonicClimate::on_receive(remote_base::RemoteReceiveData data) {
  uint8_t state_frame[PANASONIC_STATE_FRAME_SIZE] = {};
  if (!data.expect_item(PANASONIC_HEADER_MARK, PANASONIC_HEADER_SPACE)) {
    return false;
  }
  for (uint8_t pos = 0; pos < PANASONIC_FIRST_FRAME_SIZE; pos++) {
    if (!panasonic_read_byte(&data, &state_frame[pos])) {
      return false;
    }
  }
  if (!data.expect_item(PANASONIC_BIT_MARK, PANASONIC_PAUSE)) {
    return false;
  }
  if (!data.expect_item(PANASONIC_HEADER_MARK, PANASONIC_HEADER_SPACE)) {
    return false;
  }
  for (uint8_t pos = PANASONIC_SECOND_FRAME_OFFSET; pos < PANASONIC_STATE_FRAME_SIZE; pos++) {
    if (!panasonic_read_byte(&data, &state_frame[pos])) {
      return false;
    }
  }
  panasonic_log_frame(state_frame);
  return this->parse_state_frame_(state_frame);
}

}  // namespace panasonic
}  // namespace esphome
