#include "Touch_CST816.h"

struct CST816_Touch touch_data = {0};
uint8_t Touch_interrupts = 0;
static bool touch_ready = false;
static TwoWire TouchWire = TwoWire(1); // dedicated bus: SDA=1, SCL=3

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Touch I2C (Wire1 on GPIO 1/3 — NOT the shared TCA9554 bus on 10/11)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool I2C_Read_Touch(uint16_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
  static uint32_t last_err_ms = 0;
  TouchWire.beginTransmission(Driver_addr);
  TouchWire.write(Reg_addr);
  if (TouchWire.endTransmission(false) != 0) {
    uint32_t now = millis();
    if (now - last_err_ms > 2000) {
      printf("The I2C transmission fails. - Touch Read\r\n");
      last_err_ms = now;
    }
    return false;
  }
  uint8_t got = TouchWire.requestFrom((int)Driver_addr, (int)Length);
  if (got != Length) {
    uint32_t now = millis();
    if (now - last_err_ms > 2000) {
      printf("The I2C transmission fails. - Touch Read\r\n");
      last_err_ms = now;
    }
    return false;
  }
  for (uint32_t i = 0; i < Length; i++) {
    Reg_data[i] = TouchWire.read();
  }
  return true;
}

bool I2C_Write_Touch(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
  static uint32_t last_err_ms = 0;
  TouchWire.beginTransmission(Driver_addr);
  TouchWire.write(Reg_addr);
  for (uint32_t i = 0; i < Length; i++) {
    TouchWire.write(Reg_data[i]);
  }
  if (TouchWire.endTransmission(true) != 0) {
    uint32_t now = millis();
    if (now - last_err_ms > 2000) {
      printf("The I2C transmission fails. - Touch Write\r\n");
      last_err_ms = now;
    }
    return false;
  }
  return true;
}

void ARDUINO_ISR_ATTR Touch_CST816_ISR(void) {
  Touch_interrupts = true;
}

uint8_t Touch_Init(void) {
  TouchWire.begin(CST816_SDA_PIN, CST816_SCL_PIN, (uint32_t)I2C_MASTER_FREQ_HZ);
  TouchWire.setTimeOut(50);

  CST816_Touch_Reset();
  delay(50);
  CST816_Read_cfg();

  uint8_t chip = 0;
  if (!I2C_Read_Touch(CST816_ADDR, CST816_REG_ChipID, &chip, 1) || chip == 0x00) {
    touch_ready = false;
    printf("Touch controller not ready (ChipID=0x%02x)\r\n", chip);
    return false;
  }

  touch_ready = true;
  printf("Touch ready, ChipID=0x%02x\r\n", chip);
  CST816_AutoSleep(true);

  pinMode(CST816_INT_PIN, INPUT_PULLUP);
  attachInterrupt(CST816_INT_PIN, Touch_CST816_ISR, FALLING);
  return true;
}

uint8_t CST816_Touch_Reset(void)
{
  Set_EXIO(EXIO_PIN1, Low);
  vTaskDelay(pdMS_TO_TICKS(10));
  Set_EXIO(EXIO_PIN1, High);
  vTaskDelay(pdMS_TO_TICKS(50));
  return true;
}

uint16_t CST816_Read_cfg(void) {
  uint8_t buf[3] = {0};
  I2C_Read_Touch(CST816_ADDR, CST816_REG_Version, buf, 1);
  printf("TouchPad_Version:0x%02x\r\n", buf[0]);
  I2C_Read_Touch(CST816_ADDR, CST816_REG_ChipID, buf, 3);
  printf("ChipID:0x%02x   ProjID:0x%02x   FwVersion:0x%02x \r\n", buf[0], buf[1], buf[2]);
  return true;
}

void CST816_AutoSleep(bool Sleep_State) {
  CST816_Touch_Reset();
  uint8_t Sleep_State_Set = 10;
  (void)Sleep_State;
  I2C_Write_Touch(CST816_ADDR, CST816_REG_DisAutoSleep, &Sleep_State_Set, 1);
}

uint8_t Touch_Read_Data(void) {
  if (!touch_ready) {
    touch_data.points = 0;
    touch_data.gesture = NONE;
    return false;
  }
  uint8_t buf[6] = {0};
  if (!I2C_Read_Touch(CST816_ADDR, CST816_REG_GestureID, buf, 6)) {
    return false;
  }
  if (buf[0] != 0x00)
    touch_data.gesture = (GESTURE)buf[0];
  if (buf[1] != 0x00) {
    noInterrupts();
    touch_data.points = (uint8_t)buf[1];
    if (touch_data.points > CST816_LCD_TOUCH_MAX_POINTS)
      touch_data.points = CST816_LCD_TOUCH_MAX_POINTS;
    touch_data.x = ((buf[2] & 0x0F) << 8) + buf[3];
    touch_data.y = ((buf[4] & 0x0F) << 8) + buf[5];
    interrupts();
  }
  return true;
}

void example_touchpad_read(void) {
  Touch_Read_Data();
  if (touch_data.gesture != NONE || touch_data.points != 0x00) {
    printf("Touch : X=%u Y=%u points=%d\r\n", touch_data.x, touch_data.y, touch_data.points);
  }
}

void Touch_Loop(void) {
  if (Touch_interrupts) {
    Touch_interrupts = false;
    example_touchpad_read();
  }
}

String Touch_GestureName(void) {
  switch (touch_data.gesture) {
    case NONE: return "NONE";
    case SWIPE_DOWN: return "SWIPE DOWN";
    case SWIPE_UP: return "SWIPE UP";
    case SWIPE_LEFT: return "SWIPE LEFT";
    case SWIPE_RIGHT: return "SWIPE RIGHT";
    case SINGLE_CLICK: return "SINGLE CLICK";
    case DOUBLE_CLICK: return "DOUBLE CLICK";
    case LONG_PRESS: return "LONG PRESS";
    default: return "UNKNOWN";
  }
}
