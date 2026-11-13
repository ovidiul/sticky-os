#include "gt911.h"

#include <algorithm>
#include <array>
#include <utility>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_debug_log.h"

namespace {
constexpr const char* kTag = "GT911";
constexpr uint32_t kI2CFreqHz = 400000;
constexpr int kI2CTimeoutMs = 20;
constexpr uint32_t kPostResetSettleMs = 30;
constexpr uint32_t kResetHoldLowMs = 20;
constexpr uint32_t kResetReleaseHighMs = 10;
constexpr uint32_t kResetIntLowBeforeFloatMs = 60;
constexpr uint32_t kResetAddressSettleMs = 50;
constexpr uint32_t kResetRetryDelayMs = 20;
constexpr int kResetRetryCount = 3;
constexpr uint32_t kConfigApplyDelayMs = 20;
constexpr uint8_t kConfigFreshPollAttempts = 25;
constexpr uint8_t kConfigApplyRetryCount = 3;
constexpr uint32_t kConfigRetryDelayMs = 60;
constexpr uint32_t kGestureModeVerifyDelayMs = 20;
constexpr uint8_t kGestureModeVerifyAttempts = 6;
constexpr uint32_t kSleepIntLowHoldMs = 10;
constexpr size_t kMaxWriteRegistersLen = 254;
constexpr uint8_t kGestureInfoLen = 14;

}

GT911::~GT911()
{
    detachDevice();
}

void GT911::deinit()
{
    detachDevice();
    ready_ = false;
    statusCached_ = false;
    cachedStatus_ = 0;
}

void GT911::setI2cBus(i2c_master_bus_handle_t i2c_bus)
{
    detachDevice();
    i2c_bus_ = i2c_bus;
    ready_ = false;
    statusCached_ = false;
    cachedStatus_ = 0;
}

bool GT911::begin(int intPin, int rstPin, uint16_t width, uint16_t height, i2c_master_bus_handle_t i2c_bus)
{
    if(intPin < 0 || rstPin < 0 || i2c_bus == nullptr)
    {
        APP_TOUCH_DEBUG_LOGW(kTag,
                 "begin rejected: int=%d rst=%d bus=%p map=%ux%u",
                 intPin,
                 rstPin,
                 static_cast<void*>(i2c_bus),
                 static_cast<unsigned>(width),
                 static_cast<unsigned>(height));
        return false;
    }

    intPin_ = intPin;
    rstPin_ = rstPin;
    width_ = width;
    height_ = height;
    i2c_bus_ = i2c_bus;

    rotation_ = 0;
    statusCached_ = false;
    cachedStatus_ = 0;
    ready_ = false;

    gpio_set_direction(static_cast<gpio_num_t>(intPin_), GPIO_MODE_OUTPUT);
    gpio_set_direction(static_cast<gpio_num_t>(rstPin_), GPIO_MODE_OUTPUT);

    bool reset_ok = false;
    for(int attempt = 0; attempt < kResetRetryCount; ++attempt)
    {
        if(reset())
        {
            reset_ok = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(kResetRetryDelayMs));
    }

    if(!reset_ok)
    {
        APP_TOUCH_DEBUG_LOGW(kTag,
                 "begin failed: reset/probe did not find GT911 after %d attempt(s)",
                 kResetRetryCount);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(kPostResetSettleMs));

    uint16_t maxX = 0;
    uint16_t maxY = 0;
    readResolution(maxX, maxY);
    (void)clearStatus();

    ready_ = true;
    return true;
}

void GT911::onTouch(TouchCallback callback)
{
    callback_ = std::move(callback);
}

void GT911::setRotation(uint8_t rotation)
{
    rotation_ = static_cast<uint8_t>(rotation & 0x03U);
}

void GT911::readResolution(uint16_t& maxX, uint16_t& maxY)
{
    uint8_t buf[4] = {};
    if(readRegisters(GT911_REG_COORD_RES, buf, sizeof(buf)))
    {
        uint16_t x = static_cast<uint16_t>((static_cast<uint16_t>(buf[1]) << 8) | buf[0]);
        uint16_t y = static_cast<uint16_t>((static_cast<uint16_t>(buf[3]) << 8) | buf[2]);
        if(x != 0)
        {
            maxXSensor_ = x;
        }
        if(y != 0)
        {
            maxYSensor_ = y;
        }
    }

    maxX = maxXSensor_;
    maxY = maxYSensor_;
}

bool GT911::applyConfig(const uint8_t* config, size_t len)
{
    if(config == nullptr || len < 2)
    {
        APP_TOUCH_DEBUG_LOGW(kTag, "apply config rejected: config=%p len=%u", config, static_cast<unsigned>(len));
        return false;
    }

    for(uint8_t retry = 0; retry < kConfigApplyRetryCount; ++retry)
    {
        // Start each attempt from a known coordinate state. After a power-cycle
        // recovery the status register can still hold stale data from the boot
        // self-test, which can keep the controller from latching the new
        // configuration when Config_Fresh is set.
        (void)clearStatus();

        if(!writeRegisters(GT911_REG_CONFIG_START, config, len))
        {
            vTaskDelay(pdMS_TO_TICKS(kConfigRetryDelayMs));
            continue;
        }
        bool fresh_cleared = false;
        for(uint8_t attempt = 0; attempt < kConfigFreshPollAttempts; ++attempt)
        {
            vTaskDelay(pdMS_TO_TICKS(kConfigApplyDelayMs));
            uint8_t fresh = 0xFF;
            if(!readRegister(GT911_REG_CONFIG_FRESH, fresh))
            {
                break;
            }
            if(fresh == 0)
            {
                fresh_cleared = true;
                break;
            }
        }

        if(fresh_cleared)
        {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(kConfigRetryDelayMs));
    }

    APP_TOUCH_DEBUG_LOGW(kTag,
             "apply config failed after %u attempt(s) addr=0x%02X len=%u",
             static_cast<unsigned>(kConfigApplyRetryCount),
             addr_,
             static_cast<unsigned>(len));
    return false;
}

bool GT911::enterGestureMode()
{
    statusCached_ = false;
    cachedStatus_ = 0;

    (void)clearGesture();
    const bool check_ok = writeRegister(GT911_REG_COMMAND_CHECK, GT911_COMMAND_GESTURE_MODE);
    const bool command_ok = writeRegister(GT911_REG_COMMAND, GT911_COMMAND_GESTURE_MODE);
    if(!check_ok || !command_ok)
    {
        APP_TOUCH_DEBUG_LOGW(kTag,
                 "enter gesture mode failed: addr=0x%02X check=%d command=%d",
                 addr_,
                 check_ok ? 1 : 0,
                 command_ok ? 1 : 0);
        return false;
    }

    for(uint8_t attempt = 0; attempt < kGestureModeVerifyAttempts; ++attempt)
    {
        vTaskDelay(pdMS_TO_TICKS(kGestureModeVerifyDelayMs));
        if(isGestureMode())
        {
            return true;
        }
    }

    APP_TOUCH_DEBUG_LOGW(kTag, "enter gesture mode verify failed addr=0x%02X", addr_);
    return false;
}

bool GT911::enterNormalMode()
{
    return setCommand(GT911_COMMAND_NORMAL_MODE);
}

bool GT911::enterSleepMode()
{
    statusCached_ = false;
    cachedStatus_ = 0;

    if(!writeRegister(GT911_REG_COMMAND, GT911_COMMAND_SLEEP_MODE))
    {
        APP_TOUCH_DEBUG_LOGW(kTag, "enter sleep mode failed addr=0x%02X", addr_);
        return false;
    }

    if(intPin_ >= 0)
    {
        gpio_set_direction(static_cast<gpio_num_t>(intPin_), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(intPin_), 0);
        vTaskDelay(pdMS_TO_TICKS(kSleepIntLowHoldMs));
    }

    return true;
}

bool GT911::setCommand(uint8_t command)
{
    statusCached_ = false;
    cachedStatus_ = 0;
    if(!writeRegister(GT911_REG_COMMAND, command))
    {
        return false;
    }
    if(command == 0)
    {
        (void)clearStatus();
    }
    return true;
}

bool GT911::readStatus(uint8_t& status)
{
    statusCached_ = false;
    cachedStatus_ = 0;
    return readRegister(GT911_REG_STATUS, status);
}

bool GT911::isGestureMode()
{
    uint8_t id[4] = {};
    if(!readRegisters(GT911_REG_GESTURE_ID, id, sizeof(id)))
    {
        return false;
    }

    return id[0] == 'G' && id[1] == 'E' && id[2] == 'S' && id[3] == 'T';
}

bool GT911::readGesture(GTGesture& gesture)
{
    uint8_t info[kGestureInfoLen] = {};
    if(!readRegisters(GT911_REG_GESTURE_TYPE, info, sizeof(info)))
    {
        return false;
    }

    const uint8_t raw = info[0];
    gesture = static_cast<GTGesture>(raw);
    return true;
}

bool GT911::clearGesture()
{
    const bool ok = writeRegister(GT911_REG_GESTURE_TYPE, 0);
    return ok;
}

bool GT911::readAndClearGesture(GTGesture& gesture)
{
    if(!readGesture(gesture))
    {
        return false;
    }

    return clearGesture();
}

const char* GT911::gestureToString(GTGesture gesture)
{
    switch(gesture)
    {
        case GTGesture::None:
            return "none";
        case GTGesture::Right:
            return "right";
        case GTGesture::Left:
            return "left";
        case GTGesture::Down:
            return "down";
        case GTGesture::Up:
            return "up";
        case GTGesture::DoubleTap:
            return "double_tap";
        default:
            return "unknown";
    }
}

bool GT911::is_available()
{
    if(i2c_dev_ == nullptr)
    {
        return false;
    }

    if(statusCached_)
    {
        const uint8_t cachedCount = cachedStatus_ & 0x0FU;
        return ((cachedStatus_ & 0x80U) != 0U) && (cachedCount > 0U) && (cachedCount <= kMaxTouchPoints);
    }

    uint8_t status = 0;
    if(!readRegister(GT911_REG_STATUS, status))
    {
        return false;
    }

    if((status & 0x80U) == 0U)
    {
        return false;
    }

    const uint8_t touchCount = status & 0x0FU;
    if(touchCount == 0U || touchCount > kMaxTouchPoints)
    {
        (void)clearStatus();
        return false;
    }

    cachedStatus_ = status;
    statusCached_ = true;
    return true;
}

int8_t GT911::read_points(GTPoint* points, uint8_t maxPoints)
{
    if(i2c_dev_ == nullptr)
    {
        return -1;
    }

    uint8_t status = 0;
    if(statusCached_)
    {
        status = cachedStatus_;
        statusCached_ = false;
        cachedStatus_ = 0;
    }
    else
    {
        if(!readRegister(GT911_REG_STATUS, status))
        {
            return -1;
        }
    }

    if((status & 0x80U) == 0U)
    {
        return kNoUpdate;
    }

    const uint8_t touchCount = status & 0x0FU;
    if(touchCount == 0U || touchCount > kMaxTouchPoints)
    {
        (void)clearStatus();
        return 0;
    }

    std::array<uint8_t, kMaxTouchPoints * 8> rawData = {};
    const uint8_t readLength = static_cast<uint8_t>(touchCount * 8U);
    if(!readRegisters(GT911_REG_POINTS, rawData.data(), readLength))
    {
        (void)clearStatus();
        return -1;
    }

    const uint8_t outCount = std::min(touchCount, maxPoints);
    if(points != nullptr)
    {
        for(uint8_t i = 0; i < outCount; ++i)
        {
            const uint8_t* ptData = &rawData[static_cast<size_t>(i) * 8U];

            const uint16_t x = static_cast<uint16_t>(ptData[0] | (static_cast<uint16_t>(ptData[1]) << 8));
            const uint16_t y = static_cast<uint16_t>(ptData[2] | (static_cast<uint16_t>(ptData[3]) << 8));
            const uint16_t size = static_cast<uint16_t>(ptData[4] | (static_cast<uint16_t>(ptData[5]) << 8));
            const uint8_t id = ptData[7];

            const uint16_t mapX = mapCoord(x, maxXSensor_, width_);
            const uint16_t mapY = mapCoord(y, maxYSensor_, height_);

            uint16_t finalX = mapX;
            uint16_t finalY = mapY;
            switch(rotation_)
            {
                case 1:
                    finalX = mapY;
                    finalY = (mapX > width_) ? 0U : static_cast<uint16_t>(width_ - mapX);
                    break;
                case 2:
                    finalX = (mapX > width_) ? 0U : static_cast<uint16_t>(width_ - mapX);
                    finalY = (mapY > height_) ? 0U : static_cast<uint16_t>(height_ - mapY);
                    break;
                case 3:
                    finalX = (mapY > height_) ? 0U : static_cast<uint16_t>(height_ - mapY);
                    finalY = mapX;
                    break;
                default:
                    break;
            }

            points[i].x = finalX;
            points[i].y = finalY;
            points[i].id = id;
            points[i].size = size;
        }
    }

    (void)clearStatus();
    return static_cast<int8_t>(outCount);
}

void GT911::loop()
{
    if(callback_ == nullptr || !is_available())
    {
        return;
    }

    GTPoint points[kMaxTouchPoints] = {};
    const int8_t touchCount = read_points(points, kMaxTouchPoints);
    if(touchCount > 0)
    {
        callback_(touchCount, points);
    }
}

bool GT911::reset()
{
    if(i2c_bus_ == nullptr)
    {
        return false;
    }

    constexpr uint8_t kProbeAddrs[] = {GT911_ADDR2, GT911_ADDR1};
    uint8_t id[4] = {};
    for(uint8_t candidate : kProbeAddrs)
    {
        if(!resetForAddress(candidate))
        {
            continue;
        }

        const esp_err_t probe_err = i2c_master_probe(i2c_bus_, candidate, kI2CTimeoutMs);
        if(probe_err != ESP_OK)
        {
            continue;
        }

        if(!attachDevice(candidate))
        {
            continue;
        }

        if(readRegisters(GT911_REG_ID, id, sizeof(id)))
        {
            if(id[0] == '9' && id[1] == '1' && id[2] == '1')
            {
                return true;
            }

            detachDevice();
            continue;
        }

        detachDevice();
    }

    return false;
}

bool GT911::resetForAddress(uint8_t address)
{
    // GT911 address select:
    // INT=0 -> 0x5D, INT=1 -> 0x14 during reset rising edge.
    const int int_level = (address == GT911_ADDR1) ? 0 : 1;

    gpio_hold_dis(static_cast<gpio_num_t>(rstPin_));
    gpio_hold_dis(static_cast<gpio_num_t>(intPin_));

    gpio_set_direction(static_cast<gpio_num_t>(rstPin_), GPIO_MODE_OUTPUT);
    gpio_set_direction(static_cast<gpio_num_t>(intPin_), GPIO_MODE_OUTPUT);

    gpio_set_level(static_cast<gpio_num_t>(rstPin_), 0);
    gpio_set_level(static_cast<gpio_num_t>(intPin_), int_level);
    vTaskDelay(pdMS_TO_TICKS(kResetHoldLowMs));

    gpio_set_level(static_cast<gpio_num_t>(rstPin_), 1);
    vTaskDelay(pdMS_TO_TICKS(kResetReleaseHighMs));

    gpio_set_level(static_cast<gpio_num_t>(intPin_), 0);
    vTaskDelay(pdMS_TO_TICKS(kResetIntLowBeforeFloatMs));

    gpio_set_direction(static_cast<gpio_num_t>(intPin_), GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(kResetAddressSettleMs));

    return true;
}

bool GT911::clearStatus()
{
    statusCached_ = false;
    cachedStatus_ = 0;
    return writeRegister(GT911_REG_STATUS, 0);
}

bool GT911::writeRegister(uint16_t reg, uint8_t val)
{
    if(i2c_dev_ == nullptr)
    {
        return false;
    }

    const uint8_t payload[] = {
        static_cast<uint8_t>(reg >> 8),
        static_cast<uint8_t>(reg & 0xFFU),
        val,
    };
    const esp_err_t err = i2c_master_transmit(i2c_dev_, payload, sizeof(payload), kI2CTimeoutMs);
    if(err != ESP_OK)
    {
        return false;
    }
    return true;
}

bool GT911::writeRegisters(uint16_t reg, const uint8_t* buffer, size_t len)
{
    if(i2c_dev_ == nullptr || buffer == nullptr || len == 0 || len > kMaxWriteRegistersLen)
    {
        return false;
    }

    std::array<uint8_t, kMaxWriteRegistersLen + 2U> payload = {};
    payload[0] = static_cast<uint8_t>(reg >> 8);
    payload[1] = static_cast<uint8_t>(reg & 0xFFU);
    std::copy(buffer, buffer + len, payload.begin() + 2);

    const esp_err_t err = i2c_master_transmit(i2c_dev_, payload.data(), len + 2U, kI2CTimeoutMs);
    if(err != ESP_OK)
    {
        return false;
    }
    return true;
}

bool GT911::readRegister(uint16_t reg, uint8_t& val)
{
    return readRegisters(reg, &val, 1);
}

bool GT911::readRegisters(uint16_t reg, uint8_t* buffer, uint8_t len)
{
    if(i2c_dev_ == nullptr || buffer == nullptr || len == 0)
    {
        return false;
    }

    const uint8_t reg_buf[] = {
        static_cast<uint8_t>(reg >> 8),
        static_cast<uint8_t>(reg & 0xFFU),
    };
    const esp_err_t err =
        i2c_master_transmit_receive(i2c_dev_, reg_buf, sizeof(reg_buf), buffer, len, kI2CTimeoutMs);
    if(err != ESP_OK)
    {
        return false;
    }
    return true;
}

bool GT911::attachDevice(uint8_t address)
{
    if(i2c_bus_ == nullptr)
    {
        return false;
    }

    detachDevice();

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = kI2CFreqHz,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };

    const esp_err_t err = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
    if(err != ESP_OK)
    {
        i2c_dev_ = nullptr;
        return false;
    }

    addr_ = address;
    return true;
}

void GT911::detachDevice()
{
    if(i2c_dev_ == nullptr)
    {
        return;
    }

    i2c_master_bus_rm_device(i2c_dev_);
    i2c_dev_ = nullptr;
}

uint16_t GT911::mapCoord(uint16_t value, uint16_t inMax, uint16_t outMax)
{
    if(inMax == 0)
    {
        return 0;
    }

    const uint32_t mapped = (static_cast<uint32_t>(value) * static_cast<uint32_t>(outMax)) /
                            static_cast<uint32_t>(inMax);
    return static_cast<uint16_t>(mapped > 0xFFFFU ? 0xFFFFU : mapped);
}
