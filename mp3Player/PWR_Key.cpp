#include "PWR_Key.h"

static uint8_t BAT_State = 0;
static uint8_t Device_State = 0;
static uint16_t Long_Press = 0;
static uint8_t batLowCount = 0;

void Fall_Asleep(void)
{
}

void Restart(void)
{
}

void Shutdown(void)
{
  printf("PWR: shutting down (BAT_Control LOW)\r\n");
  Set_Backlight(0);
  LCD_Backlight = 0;
  digitalWrite(PWR_Control_PIN, LOW);
  // If USB is still supplying power the board stays up; on battery this cuts the rail.
}

void PWR_Init(void)
{
  pinMode(PWR_KEY_Input_PIN, INPUT);
  pinMode(PWR_Control_PIN, OUTPUT);
  digitalWrite(PWR_Control_PIN, LOW);
  delay(100);

  // Latch battery power immediately so releasing PWR does not cut the rail.
  // On a battery cold-start the button is still held here (active-low).
  digitalWrite(PWR_Control_PIN, HIGH);
  if (!digitalRead(PWR_KEY_Input_PIN)) {
    BAT_State = 1;  // power-on press still held → wait for release
  } else {
    BAT_State = 2;  // USB boot or already released → ready for long-press off
  }
  printf("PWR: latched (Key=%d State=%u)\r\n",
         digitalRead(PWR_KEY_Input_PIN), (unsigned)BAT_State);
}

void PWR_Loop(void)
{
  if (!BAT_State) return;

  if (!digitalRead(PWR_KEY_Input_PIN)) {
    // Button held
    if (BAT_State == 2) {
      Long_Press++;
      if (Long_Press >= Device_Sleep_Time) {
        if (Long_Press >= Device_Sleep_Time && Long_Press < Device_Restart_Time)
          Device_State = 1;
        else if (Long_Press >= Device_Restart_Time && Long_Press < Device_Shutdown_Time)
          Device_State = 2;
        else if (Long_Press >= Device_Shutdown_Time)
          Shutdown();
      }
    }
  } else {
    // Button released
    if (BAT_State == 1)
      BAT_State = 2;
    Long_Press = 0;
  }
}

void PWR_CheckBattery(float volts)
{
  if (volts <= 0.1f) return;  // ADC not ready / disconnected sense
  if (volts < BAT_LOW_VOLTS) {
    if (batLowCount < 255) batLowCount++;
    if (batLowCount >= BAT_LOW_COUNT_MAX) {
      printf("PWR: battery low (%.2f V) — shutting down\r\n", volts);
      Shutdown();
    }
  } else {
    batLowCount = 0;
  }
}
