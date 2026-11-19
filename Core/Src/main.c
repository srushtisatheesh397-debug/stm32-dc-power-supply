#include "main.h"
#include <stdio.h>

ADC_HandleTypeDef hadc1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);

/* Read one ADC channel at a time. */
static uint32_t ADC_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef config = {0};
    uint32_t value = 0;

    config.Channel = channel;
    config.Rank = 1;
    config.SamplingTime = ADC_SAMPLETIME_480CYCLES;

    if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK)
        return 0;

    if (HAL_ADC_Start(&hadc1) == HAL_OK)
    {
        if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
            value = HAL_ADC_GetValue(&hadc1);

        HAL_ADC_Stop(&hadc1);
    }

    return value;
}

/* LCD uses PB0–PB3 for D4–D7, PB4 for RS, and PB5 for Enable. */
static void LCD_PulseEnable(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    HAL_Delay(1);
}

static void LCD_Write4Bits(uint8_t value)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,
        (value & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1,
        (value & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2,
        (value & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3,
        (value & 0x08U) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    LCD_PulseEnable();
}

static void LCD_Send(uint8_t value, GPIO_PinState rs)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, rs);
    LCD_Write4Bits(value >> 4);
    LCD_Write4Bits(value & 0x0FU);
}

static void LCD_Command(uint8_t command)
{
    LCD_Send(command, GPIO_PIN_RESET);

    if (command == 0x01U || command == 0x02U)
        HAL_Delay(2);
}

static void LCD_Init(void)
{
    HAL_Delay(50);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);

    LCD_Write4Bits(0x03);
    HAL_Delay(5);
    LCD_Write4Bits(0x03);
    LCD_Write4Bits(0x03);
    LCD_Write4Bits(0x02);

    LCD_Command(0x28);
    LCD_Command(0x0C);
    LCD_Command(0x06);
    LCD_Command(0x01);
}

static void LCD_SetCursor(uint8_t column, uint8_t row)
{
    LCD_Command((row == 0U ? 0x80U : 0xC0U) + column);
}

static void LCD_Print(const char *text)
{
    while (*text != '\0')
        LCD_Send((uint8_t)*text++, GPIO_PIN_SET);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_ADC1_Init();

    uint32_t rawVoltage = 0;
    uint32_t rawCurrent = 0;
    float voltage;
    float current;
    float power;
    char line1[17];
    char line2[17];

    LCD_Init();

    while (1)
    {
        rawVoltage = ADC_ReadChannel(ADC_CHANNEL_0);
        rawCurrent = ADC_ReadChannel(ADC_CHANNEL_1);

        float adcVoltage = (rawVoltage * 3.3f) / 4095.0f;
        voltage = adcVoltage * 5.7f;

        float sensorVoltage = (rawCurrent * 3.3f) / 4095.0f;
        current = (sensorVoltage - 1.65f) / 0.185f;

        if (current < 0.0f)
            current = 0.0f;

        power = voltage * current;

        uint8_t overVoltage = (voltage > 13.0f);
        uint8_t overCurrent = (current > 1.0f);
        uint8_t fault = overVoltage || overCurrent;

        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8,
            fault ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9,
            fault ? GPIO_PIN_SET : GPIO_PIN_RESET);

        unsigned int voltageTenths =
            (unsigned int)(voltage * 10.0f + 0.5f);
        unsigned int currentHundredths =
            (unsigned int)(current * 100.0f + 0.5f);
        unsigned int powerHundredths =
            (unsigned int)(power * 100.0f + 0.5f);

        snprintf(line1, sizeof(line1), "V:%u.%uV I:%u.%02uA",
            voltageTenths / 10U, voltageTenths % 10U,
            currentHundredths / 100U, currentHundredths % 100U);

        if (overVoltage && overCurrent)
            snprintf(line2, sizeof(line2), "FAULT: BOTH");
        else if (overVoltage)
            snprintf(line2, sizeof(line2), "FAULT: OVER-V");
        else if (overCurrent)
            snprintf(line2, sizeof(line2), "FAULT: OVER-I");
        else
            snprintf(line2, sizeof(line2), "P:%u.%02uW NORMAL",
                powerHundredths / 100U, powerHundredths % 100U);

        LCD_Command(0x01);
        LCD_SetCursor(0, 0);
        LCD_Print(line1);
        LCD_SetCursor(0, 1);
        LCD_Print(line2);

        HAL_Delay(500);
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
        Error_Handler();
}

static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef config = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
        Error_Handler();

    config.Channel = ADC_CHANNEL_0;
    config.Rank = 1;
    config.SamplingTime = ADC_SAMPLETIME_480CYCLES;

    if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK)
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOB,
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
        GPIO_PIN_4 | GPIO_PIN_5, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
        GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}
