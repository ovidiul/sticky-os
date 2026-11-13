#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "driver/i2c_master.h"

#define GT911_ADDR1 0x5D
#define GT911_ADDR2 0x14

#define GT911_REG_COMMAND 0x8040
#define GT911_REG_COMMAND_CHECK 0x8046
#define GT911_REG_CONFIG_START 0x8047
#define GT911_REG_CONFIG_CHECKSUM 0x80FF
#define GT911_REG_CONFIG_FRESH 0x8100
#define GT911_REG_ID 0x8140
#define GT911_REG_GESTURE_ID 0x8140
#define GT911_REG_GESTURE_TYPE 0x814B
#define GT911_REG_COORD_RES 0x8146
#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINTS 0x8150

#define GT911_COMMAND_NORMAL_MODE 0x00
#define GT911_COMMAND_SLEEP_MODE 0x05
#define GT911_COMMAND_GESTURE_MODE 0x08

enum class GTGesture : uint8_t {
    None = 0x00,
    Right = 0xAA,
    Left = 0xBB,
    Down = 0xAB,
    Up = 0xBA,
    DoubleTap = 0xCC,
};

struct GTPoint {
    uint16_t x;
    uint16_t y;
    uint16_t size;
    uint8_t id;
};

using TouchCallback = std::function<void(int8_t, const GTPoint*)>;

class GT911 {
public:
    static constexpr uint8_t kMaxTouchPoints = 5;
    // The controller returns status without the ready bit while there is no
    // new report. This is different from a valid zero-point release report.
    static constexpr int8_t kNoUpdate = -2;

    GT911() = default;
    ~GT911();

    bool begin(int intPin, int rstPin, uint16_t width, uint16_t height, i2c_master_bus_handle_t i2c_bus);
    void deinit();
    void setI2cBus(i2c_master_bus_handle_t i2c_bus);

    void onTouch(TouchCallback callback);
    void loop();

    bool is_available();
    int8_t read_points(GTPoint* points, uint8_t maxPoints = kMaxTouchPoints);

    void setRotation(uint8_t rotation);
    void readResolution(uint16_t& maxX, uint16_t& maxY);

    bool applyConfig(const uint8_t* config, size_t len);
    bool setCommand(uint8_t command);
    bool enterNormalMode();
    bool enterSleepMode();
    bool enterGestureMode();
    bool isGestureMode();
    bool readStatus(uint8_t& status);
    bool readGesture(GTGesture& gesture);
    bool clearGesture();
    bool readAndClearGesture(GTGesture& gesture);
    bool readRegisters(uint16_t reg, uint8_t* buffer, uint8_t len);

    bool isReady() const { return ready_; }
    uint8_t address() const { return addr_; }

    static const char* gestureToString(GTGesture gesture);

private:
    bool attachDevice(uint8_t address);
    void detachDevice();
    bool resetForAddress(uint8_t address);
    bool reset();
    bool clearStatus();

    bool writeRegister(uint16_t reg, uint8_t val);
    bool writeRegisters(uint16_t reg, const uint8_t* buffer, size_t len);
    bool readRegister(uint16_t reg, uint8_t& val);

    static uint16_t mapCoord(uint16_t value, uint16_t inMax, uint16_t outMax);

private:
    int intPin_ = -1;
    int rstPin_ = -1;

    uint16_t width_ = 0;
    uint16_t height_ = 0;

    uint16_t maxXSensor_ = 2048;
    uint16_t maxYSensor_ = 2048;

    uint8_t addr_ = GT911_ADDR1;
    uint8_t rotation_ = 0;

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t i2c_dev_ = nullptr;
    TouchCallback callback_ = nullptr;

    bool ready_ = false;
    bool statusCached_ = false;
    uint8_t cachedStatus_ = 0;
};
