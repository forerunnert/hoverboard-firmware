/*
* This file is part of the hoverboard-firmware-hack project.
*
* Copyright (C) 2017-2018 Rene Hopf <renehopf@mac.com>
* Copyright (C) 2017-2018 Nico Stute <crinq@crinq.de>
* Copyright (C) 2017-2018 Niklas Fauth <niklas.fauth@kit.fail>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stm32f1xx_hal.h"
#include "defines.h"
#include "setup.h"
#include "config.h"
#include "hd44780.h"
LCD_PCF8574_HandleTypeDef lcd;
extern TIM_HandleTypeDef htim_left;
extern TIM_HandleTypeDef htim_right;
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern volatile adc_buf_t adc_buffer;
extern I2C_HandleTypeDef hi2c2;
extern UART_HandleTypeDef huart2;

int cmd1;  // normalized input values. -1000 to 1000
int cmd2;
int cmd3;

typedef struct{
   int16_t steer;
   int16_t speed;
   //uint32_t crc;
} Serialcommand;

volatile Serialcommand command;

uint8_t button1, button2;

int steer; // global variable for steering. -1000 to 1000
int speed; // global variable for speed. -1000 to 1000

extern volatile int pwml;  // global variable for pwm left. -1000 to 1000
extern volatile int pwmr;  // global variable for pwm right. -1000 to 1000
extern volatile int weakl; // global variable for field weakening left. -1000 to 1000
extern volatile int weakr; // global variable for field weakening right. -1000 to 1000

float weak;  // fuer sanftes einsetzen des turbos

extern uint8_t buzzerFreq;    // global variable for the buzzer pitch. can be 1, 2, 3, 4, 5, 6, 7...
extern uint8_t buzzerPattern; // global variable for the buzzer pattern. can be 1, 2, 3, 4, 5, 6, 7...

extern uint8_t enable; // global variable for motor enable

extern volatile uint32_t timeout; // global variable for timeout
extern float batteryVoltage; // global variable for battery voltage
extern volatile float currentR; //TODO
extern volatile float currentL; //TODO

unsigned char bat_13[8] = { 0xe, 0x1b, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1f};
unsigned char bat_25[8] = { 0xe, 0x1b, 0x11, 0x11, 0x11, 0x11, 0x1f, 0x1f};
unsigned char bat_38[8] = { 0xe, 0x1b, 0x11, 0x11, 0x11, 0x1f, 0x1f, 0x1f};
unsigned char bat_50[8] = { 0xe, 0x1b, 0x11, 0x11, 0x1f, 0x1f, 0x1f, 0x1f};
unsigned char bat_63[8] = { 0xe, 0x1b, 0x11, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
unsigned char bat_75[8] = { 0xe, 0x1b, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
unsigned char bat_88[8] = { 0xe, 0x11, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
unsigned char bat_100[8] ={ 0xe, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};

uint32_t inactivity_timeout_counter;
uint32_t lcd_update_counter=0;
uint32_t lcd_refresh = 20;

extern uint8_t nunchuck_data[6];
#ifdef CONTROL_PPM
extern volatile uint16_t ppm_captured_value[PPM_NUM_CHANNELS+1];
#endif

int milli_vel_error_sum = 0;


void beep(uint8_t anzahl) {  // blocking function, do not use in main loop!
    for(uint8_t i = 0; i < anzahl; i++) {
        buzzerFreq = 2;
        HAL_Delay(100);
        buzzerFreq = 0;
        HAL_Delay(200);
    }
}


void poweroff() {
    if (abs(speed) < 20) {
        LCD_ClearDisplay(&lcd);
        HAL_Delay(50);
        LCD_SetLocation(&lcd, 0, 0);
        LCD_WriteString(&lcd, "Power off");
        LCD_SetLocation(&lcd, 0, 1);
        LCD_WriteString(&lcd, "Goodbye!");
        HAL_Delay(100);
        buzzerPattern = 0;
        enable = 0;
        for (int i = 0; i < 8; i++) {
            buzzerFreq = i;
            HAL_Delay(100);
        }
        HAL_GPIO_WritePin(OFF_PORT, OFF_PIN, 0);
        while(1) {}
    }
}

void initializeLCD(){
	I2C_Init();
    HAL_Delay(100);
    lcd.pcf8574.PCF_I2C_ADDRESS = 0x27;
      lcd.pcf8574.PCF_I2C_TIMEOUT = 1000;
      lcd.pcf8574.i2c = hi2c2;
      lcd.pcf8574.i2c.Init.ClockSpeed = 100000;
      lcd.NUMBER_OF_LINES = NUMBER_OF_LINES_2;
      lcd.type = TYPE0;

      if(LCD_Init(&lcd)!=LCD_OK){
          // error occured
          //TODO while(1);
      }

	// create symbols
	LCD_CustomChar(&lcd, bat_13, 0);
	LCD_CustomChar(&lcd, bat_25, 1);
	LCD_CustomChar(&lcd, bat_38, 2);
	LCD_CustomChar(&lcd, bat_50, 3);
	LCD_CustomChar(&lcd, bat_63, 4);
	LCD_CustomChar(&lcd, bat_75, 5);
	LCD_CustomChar(&lcd, bat_88, 6);
	LCD_CustomChar(&lcd, bat_100, 7);
	
    LCD_ClearDisplay(&lcd);
    HAL_Delay(10);
    LCD_SetLocation(&lcd, 0, 0);
    LCD_WriteString(&lcd, "Get Ready!");
    LCD_SetLocation(&lcd, 0, 1);
    LCD_WriteString(&lcd, "Waiting to arm");
}


void updateLCD(){
    // ####### BATTERY VOLTAGE INDICATOR ######
    /* Rudimentary Voltage --- Charge state
    4.2 V --- 100 %
    4.1 V --- 90 %
    4.0 V --- 80 %
    3.9 V --- 60 %
    3.8 V --- 40 %
    3.7 V --- 20 %
    3.6 V --- 0 %
	from: https://xiaolaba.wordpress.com/2017/06/12/hd44780-lcm-battery-gauge-and-symbol-design/
	0x00 const byte bat_13[8] = { 0xe, 0x1b, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1f}; 
	0x01 const byte bat_25[8] = { 0xe, 0x1b, 0x11, 0x11, 0x11, 0x11, 0x1f, 0x1f};
	0x02 const byte bat_38[8] = { 0xe, 0x1b, 0x11, 0x11, 0x11, 0x1f, 0x1f, 0x1f};
	0x03 const byte bat_50[8] = { 0xe, 0x1b, 0x11, 0x11, 0x1f, 0x1f, 0x1f, 0x1f};
	0x04 const byte bat_63[8] = { 0xe, 0x1b, 0x11, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
	0x05 const byte bat_75[8] = { 0xe, 0x1b, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
	0x06 const byte bat_88[8] = { 0xe, 0x11, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
	0x06 const byte bat_100[8] ={ 0xe, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
    */

    LCD_ClearDisplay(&lcd);
    HAL_Delay(5);
    LCD_SetLocation(&lcd, 0, 0);

    if(batteryVoltage/(float)BAT_NUMBER_OF_CELLS < BAT_LOW_LVL2){
        LCD_WriteString(&lcd, "BATTERY EMPTY");
    } else if (batteryVoltage/(float)BAT_NUMBER_OF_CELLS > BAT_LOW_LVL2 && batteryVoltage/(float)BAT_NUMBER_OF_CELLS <= BAT_LOW_LVL1){
        LCD_WriteDATA(&lcd, 0x00);
    } else if (batteryVoltage/(float)BAT_NUMBER_OF_CELLS > 3.6 && batteryVoltage/(float)BAT_NUMBER_OF_CELLS <= 3.7){
        LCD_WriteDATA(&lcd, 0x01);
    } else if (batteryVoltage/(float)BAT_NUMBER_OF_CELLS > 3.7 && batteryVoltage/(float)BAT_NUMBER_OF_CELLS <= 3.8){
        LCD_WriteDATA(&lcd, 0x02);
    }else if (batteryVoltage/(float)BAT_NUMBER_OF_CELLS > 3.8 && batteryVoltage/(float)BAT_NUMBER_OF_CELLS <= 3.9){
        LCD_WriteDATA(&lcd, 0x03);
    }else if (batteryVoltage/(float)BAT_NUMBER_OF_CELLS > 3.9 && batteryVoltage/(float)BAT_NUMBER_OF_CELLS <= 3.95){
        LCD_WriteDATA(&lcd, 0x04);
    }else if (batteryVoltage/(float)BAT_NUMBER_OF_CELLS > 3.95 && batteryVoltage/(float)BAT_NUMBER_OF_CELLS <= 4.0){
        LCD_WriteDATA(&lcd, 0x05);
    }else if (batteryVoltage/(float)BAT_NUMBER_OF_CELLS > 4.0 && batteryVoltage/(float)BAT_NUMBER_OF_CELLS <= 4.1){
        LCD_WriteDATA(&lcd, 0x06);
    }else if (batteryVoltage/(float)BAT_NUMBER_OF_CELLS > 4.1){
        LCD_WriteDATA(&lcd, 0x07);
    }
    LCD_SetLocation(&lcd, 2, 0);
    LCD_WriteNumber(&lcd, batteryVoltage, 0);
    LCD_SetLocation(&lcd, 4, 0);
    LCD_WriteString(&lcd, "V");

    // speed
    LCD_SetLocation(&lcd, 0, 1);
    LCD_WriteString(&lcd, "Speed");
    LCD_SetLocation(&lcd, 8, 1);
    LCD_WriteNumber(&lcd, lcd_update_counter, 1);
    LCD_SetLocation(&lcd, 10, 1);
    LCD_WriteString(&lcd, "km/h");
}

int main(void) {
  HAL_Init();
  __HAL_RCC_AFIO_CLK_ENABLE();
  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
  /* System interrupt init*/
  /* MemoryManagement_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(MemoryManagement_IRQn, 0, 0);
  /* BusFault_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(BusFault_IRQn, 0, 0);
  /* UsageFault_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(UsageFault_IRQn, 0, 0);
  /* SVCall_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SVCall_IRQn, 0, 0);
  /* DebugMonitor_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DebugMonitor_IRQn, 0, 0);
  /* PendSV_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(PendSV_IRQn, 0, 0);
  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

  SystemClock_Config();

  __HAL_RCC_DMA1_CLK_DISABLE();
  MX_GPIO_Init();
  MX_TIM_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();

  #if defined(DEBUG_SERIAL_USART2) || defined(DEBUG_SERIAL_USART3)
    UART_Init();
  #endif

  HAL_GPIO_WritePin(OFF_PORT, OFF_PIN, 1);

  HAL_ADC_Start(&hadc1);
  HAL_ADC_Start(&hadc2);

  for (int i = 8; i >= 0; i--) {
    buzzerFreq = i;
    HAL_Delay(100);
  }
  buzzerFreq = 0;

  HAL_GPIO_WritePin(LED_PORT, LED_PIN, 1);

  int lastSpeedL = 0, lastSpeedR = 0;
  int speedL = 0, speedR = 0, speedRL = 0;
  // float direction = 1;
  
  float adc1_filtered = 0.0;
  float adc2_filtered = 0.0;

  #ifdef CONTROL_PPM
    PPM_Init();
  #endif

  #ifdef CONTROL_NUNCHUCK
    I2C_Init();
    Nunchuck_Init();
  #endif

  #ifdef CONTROL_SERIAL_USART2
    UART_Control_Init();
    HAL_UART_Receive_DMA(&huart2, (uint8_t *)&command, 4);
  #endif

  #ifdef DEBUG_I2C_LCD
    initializeLCD();
  #endif
  int8_t mode;
  HAL_Delay(300);
  // ####### driving modes #######
  mode = MODE;
  beep(2);
  updateLCD();

  float board_temp_adc_filtered = (float)adc_buffer.temp;
  float board_temp_deg_c;

  enable = 1;  // enable motors

  while(1) {
    HAL_Delay(DELAY_IN_MAIN_LOOP); //delay in ms

    #ifdef CONTROL_NUNCHUCK
      Nunchuck_Read();
      cmd1 = CLAMP((nunchuck_data[0] - 127) * 8, -1000, 1000); // x - axis. Nunchuck joystick readings range 30 - 230
      cmd2 = CLAMP((nunchuck_data[1] - 128) * 8, -1000, 1000); // y - axis

      button1 = (uint8_t)nunchuck_data[5] & 1;
      button2 = (uint8_t)(nunchuck_data[5] >> 1) & 1;
    #endif

    #ifdef CONTROL_PPM
      cmd1 = CLAMP((ppm_captured_value[0] - 500) * 2, -1000, 1000);
      cmd2 = CLAMP((ppm_captured_value[1] - 500) * 2, -1000, 1000);
      button1 = ppm_captured_value[5] > 500;
      float scale = ppm_captured_value[2] / 1000.0f;
    #endif


    #ifdef CONTROL_ADC
      // ADC values range: 0-4095, see ADC-calibration in config.h
      //cmd1 = CLAMP(adc_buffer.l_tx2 - ADC1_MIN, 0, ADC1_MAX) / (ADC1_MAX / 1000.0f);  // ADC1
      //cmd2 = CLAMP(adc_buffer.l_rx2 - ADC2_MIN, 0, ADC2_MAX) / (ADC2_MAX / 1000.0f);  // ADC2

      // use ADCs as button inputs:
      //#ifdef ADC1_BUTTON
      //  button1 = (uint8_t)(adc_buffer.l_tx2 > 2000);  // ADC1
      //#endif
      //#ifdef ADC2_BUTTON
      //  button2 = (uint8_t)(adc_buffer.l_rx2 > 2000);  // ADC2
      //#endif
      timeout = 0;
    #endif

    #ifdef CONTROL_SERIAL_USART2
      cmd1 = CLAMP((int16_t)command.steer, -1000, 1000);
      cmd2 = CLAMP((int16_t)command.speed, -1000, 1000);

      timeout = 0;
    #endif

    // ####### larsm's bobby car code #######

    // LOW-PASS FILTER (fliessender Mittelwert)
    adc1_filtered = adc1_filtered * 0.9 + (float)adc_buffer.l_rx2 * 0.1; // links, rueckwearts
    adc2_filtered = adc2_filtered * 0.9 + (float)adc_buffer.l_tx2 * 0.1; // rechts, vorwaerts

    // magic numbers die ich nicht mehr nachvollziehen kann, faehrt sich aber gut ;-)
    #define LOSLASS_BREMS_ACC 0.995f  // naeher an 1 = gemaechlicher, default 0.996f
    #define DRUECK_ACC (1.0f - LOSLASS_BREMS_ACC + 0.001f)  // naeher an 0 = gemaechlicher
    //die + 0.001f gleichen float ungenauigkeiten aus.

    // ADC1 = rx2, throttle, ADC2 = tx2, FWD/REV
    #define ADC1_DELTA (ADC1_MAX - ADC1_MIN)
    #define ADC2_DELTA (ADC2_MAX - ADC2_MIN)

    float throttle_input = adc_buffer.l_rx2;
    float throttle_param;

    float shift_input = adc_buffer.l_tx2;
    float shift_setpoint=0.0;

    if (mode == 1) {
      throttle_param = 350.0f;
    } else if (mode == 2) {
      throttle_param = 400.0f;
    } else if (mode == 3) {
      throttle_param = 440.0f;
    } else if (mode == 4) {
      throttle_param = 480.0f;
    } else if (mode == 5) {
      throttle_param = 520.0f;
    } else if (mode == 6) {
      throttle_param = 563.0f;
    } else if (mode == 7) {
      throttle_param = 617.0f;
    } else if (mode == 8) {
      throttle_param = 684.0f;
    } else if (mode == 9) {
      throttle_param = 751.0f;
    } else if (mode == 10) {
      throttle_param = 1000.0f;
    }

    if (shift_input > ADC2_CENTER + 200) {
        shift_setpoint = 1; // FWD
    } else if (shift_input < ADC2_CENTER + 200 && shift_input > ADC2_CENTER - 200) {
        shift_setpoint = 0; // STOP
    } else if (shift_input < ADC2_CENTER - 200) {
        shift_setpoint = -0.6; // REV
    }

    speedRL = (float)speedRL * LOSLASS_BREMS_ACC  // bremsen wenn kein poti gedrueckt
            + (CLAMP(throttle_input - ADC1_MIN, 0, ADC1_DELTA) / (ADC1_DELTA / throttle_param)) * DRUECK_ACC;  // vorwaerts gedrueckt = beschleunigen 12s: 350=3kmh

    weakl = 0;
    weakr = 0;
    speed = speedR = speedL = shift_setpoint*(CLAMP(speedRL, -1000, 1000));  // clamp output

    #ifdef ADDITIONAL_CODE
      ADDITIONAL_CODE;
    #endif


    // ####### SET OUTPUTS #######
    if ((speedL < lastSpeedL + 50 && speedL > lastSpeedL - 50) && (speedR < lastSpeedR + 50 && speedR > lastSpeedR - 50) && timeout < TIMEOUT) {
    #ifdef INVERT_R_DIRECTION
      pwmr = speedR;
    #else
      pwmr = -speedR;
    #endif
    #ifdef INVERT_L_DIRECTION
      pwml = -speedL;
    #else
      pwml = speedL;
    #endif
    }

    lastSpeedL = speedL;
    lastSpeedR = speedR;

    if (inactivity_timeout_counter % 25 == 0) {
      // ####### CALC BOARD TEMPERATURE #######
      board_temp_adc_filtered = board_temp_adc_filtered * 0.99 + (float)adc_buffer.temp * 0.01;
      board_temp_deg_c = ((float)TEMP_CAL_HIGH_DEG_C - (float)TEMP_CAL_LOW_DEG_C) / ((float)TEMP_CAL_HIGH_ADC - (float)TEMP_CAL_LOW_ADC) * (board_temp_adc_filtered - (float)TEMP_CAL_LOW_ADC) + (float)TEMP_CAL_LOW_DEG_C;
      
      // ####### DEBUG SERIAL OUT #######
      #ifdef CONTROL_ADC
        setScopeChannel(0, (int)adc1_filtered);  // 1: ADC1
        setScopeChannel(1, (int)adc2_filtered);  // 2: ADC2
      #endif
      setScopeChannel(2, (int)speedR);  // 3: output speed: 0-1000
      setScopeChannel(3, (int)speedL);  // 4: output speed: 0-1000
      setScopeChannel(4, (int)adc_buffer.batt1);  // 5: for battery voltage calibration
      setScopeChannel(5, (int)(batteryVoltage * 100.0f));  // 6: for verifying battery voltage calibration
      setScopeChannel(6, (int)board_temp_adc_filtered);  // 7: for board temperature calibration
      setScopeChannel(7, (int)board_temp_deg_c);  // 8: for verifying board temperature calibration
      consoleScope();
    }


    // ####### POWEROFF BY POWER-BUTTON #######
    if (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN) && weakr == 0 && weakl == 0) {
      enable = 0;
      while (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN)) {}
      poweroff();
    }

    // ####### BEEP AND EMERGENCY POWEROFF #######
    if ((TEMP_POWEROFF_ENABLE && board_temp_deg_c >= TEMP_POWEROFF && abs(speed) < 20) || (batteryVoltage < ((float)BAT_LOW_DEAD * (float)BAT_NUMBER_OF_CELLS) && abs(speed) < 20)) {  // poweroff before mainboard burns OR low bat 3
      poweroff();
    } else if (TEMP_WARNING_ENABLE && board_temp_deg_c >= TEMP_WARNING) {  // beep if mainboard gets hot
      buzzerFreq = 4;
      buzzerPattern = 1;
    } else if (batteryVoltage < ((float)BAT_LOW_LVL1 * (float)BAT_NUMBER_OF_CELLS) && batteryVoltage > ((float)BAT_LOW_LVL2 * (float)BAT_NUMBER_OF_CELLS) && BAT_LOW_LVL1_ENABLE) {  // low bat 1: slow beep
      buzzerFreq = 5;
      buzzerPattern = 42;
    } else if (batteryVoltage < ((float)BAT_LOW_LVL2 * (float)BAT_NUMBER_OF_CELLS) && batteryVoltage > ((float)BAT_LOW_DEAD * (float)BAT_NUMBER_OF_CELLS) && BAT_LOW_LVL2_ENABLE) {  // low bat 2: fast beep
      buzzerFreq = 5;
      buzzerPattern = 6;
    } else if (BEEPS_BACKWARD && speed < -50) {  // backward beep
      buzzerFreq = 5;
      buzzerPattern = 1;
    } else {  // do not beep
      buzzerFreq = 0;
      buzzerPattern = 0;
    }

    // ####### INACTIVITY TIMEOUT #######
    if (abs(speedL) > 50 || abs(speedR) > 50) {
      inactivity_timeout_counter = 0;
    } else {
      inactivity_timeout_counter ++;
    }
    if (inactivity_timeout_counter > (INACTIVITY_TIMEOUT * 60 * 1000) / (DELAY_IN_MAIN_LOOP + 1)) {  // rest of main loop needs maybe 1ms
      poweroff();
    }
  }
}

/** System Clock Configuration
*/
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;

  /**Initializes the CPU, AHB and APB busses clocks
    */
  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL          = RCC_PLL_MUL16;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  /**Initializes the CPU, AHB and APB busses clocks
    */
  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection    = RCC_ADCPCLK2_DIV8;  // 8 MHz
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

  /**Configure the Systick interrupt time
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);

  /**Configure the Systick
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}
