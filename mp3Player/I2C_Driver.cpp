#include "I2C_Driver.h"

void I2C_Init(void) {
  // Do not Wire.end() here — on Arduino-ESP32 3.x that leaves the new
  // i2c master driver in ESP_ERR_INVALID_STATE for later transfers.
  static bool started = false;
  if (!started) {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, (uint32_t)I2C_MASTER_FREQ_HZ);
    started = true;
  } else {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, (uint32_t)I2C_MASTER_FREQ_HZ);
  }
  Wire.setTimeOut(50);
}

bool I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr);
  // Repeated start — required by CST816 / TCA9554 on the new Wire driver
  if (Wire.endTransmission(false) != 0) {
    printf("The I2C transmission fails. - I2C Read\r\n");
    return false;
  }
  uint8_t got = Wire.requestFrom((int)Driver_addr, (int)Length);
  if (got != Length) {
    printf("The I2C transmission fails. - I2C Read\r\n");
    return false;
  }
  for (uint32_t i = 0; i < Length; i++) {
    Reg_data[i] = Wire.read();
  }
  return true;
}

bool I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr);
  for (uint32_t i = 0; i < Length; i++) {
    Wire.write(Reg_data[i]);
  }
  if (Wire.endTransmission(true) != 0) {
    printf("The I2C transmission fails. - I2C Write\r\n");
    return false;
  }
  return true;
}
