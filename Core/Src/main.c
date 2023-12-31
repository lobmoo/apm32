/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "rtc.h"
#include "tim.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "common.h"
#include "testCase.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define DEBUG_TEST 0
uint8_t workState = INIT_STATE;
uint16_t g_testEndFlag = 0;

RTC_DateTypeDef sdatestructure;
RTC_TimeTypeDef stimestructure;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint16_t ADC_value[ADC_CHANNEL_CNT*SAMPLING_COUNT] = { 0 };
uint16_t lastVccData;
uint16_t lastClkData;
uint16_t lastIOData;
uint16_t lastRstData;

uint8_t  g_UcomBuf[1024] = {0};
uint8_t  sendBuf[1024] = {0};

uint8_t  StartSamplingFlag = 0;
uint8_t  LauchTestCaseFlag = 0;
uint16_t g_caseNumber = 0;

uint32_t g_uSampleStartTimeTick = 0;
SAMPLING_DATA g_curSampleVal = {0};
CASE_STATE g_caseState = {0};
uint16_t curCaseNum = 0;

float curVccAmp = 0.0;

uint8_t sampleInit = 0;

uint32_t IC_TIMES;  // 捕获次数，单�?1ms
uint8_t IC_START_FLAG;  // 捕获�?始标志，1：已捕获到高电平�?0：还没有捕获到高电平
uint8_t IC_DONE_FLAG;  // 捕获完成标志�?1：已完成�?次高电平捕获
uint16_t IC_VALUE;  // 输入捕获的捕获�??
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void USB_Status_Init(void);
/* 定时器计数溢出中断处理回调函�? */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(IC_DONE_FLAG == 0)  // 未完成捕�?
    {
        if(IC_START_FLAG == 1)  // 已经捕获到了高电�?
        {
            IC_TIMES++;  // 捕获次数加一
        }
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if(IC_DONE_FLAG == 0)  // 未完成捕�?
    {
        if(IC_START_FLAG == 1)  // 原来是高电平，现在捕获到�?个下降沿
        {
            IC_VALUE = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);  // 获取捕获�?
            TIM_RESET_CAPTUREPOLARITY(htim,TIM_CHANNEL_1);  // 先清除原来的设置
            TIM_SET_CAPTUREPOLARITY(htim,TIM_CHANNEL_1,TIM_ICPOLARITY_RISING);// 配置为上升沿捕获
            IC_START_FLAG = 0;  // 标志复位
            IC_DONE_FLAG = 1;  // 完成�?次高电平捕获
        }
        else  // 捕获还未�?始，第一次捕获到上升�?
        {
            IC_TIMES = 0;  // 捕获次数清零
            IC_VALUE = 0;  // 捕获值清�?
            IC_START_FLAG = 1;  // 设置捕获到了上边沿的标志
            TIM_RESET_CAPTUREPOLARITY(htim,TIM_CHANNEL_1);  // 先清除原来的设置
            TIM_SET_CAPTUREPOLARITY(htim,TIM_CHANNEL_1,TIM_ICPOLARITY_FALLING);// 配置为下降沿捕获
        }
        __HAL_TIM_SET_COUNTER(htim,0);  // 定时器计数�?�清�?
    }
#if 0
    if (htim->Instance == htim4.Instance)
    {
        g_uClockCnt++;
        HAL_TIM_ReadCapturedValue(&htim4,TIM_CHANNEL_1);
        clkStableFlag = 1;
    }
#endif
}

uint32_t ADC_MultiChannelPolling(uint8_t *packet)
{
    uint16_t dataLength = 0;
    uint16_t index = 0;
    SAMPLING_DATA data = { 0 };
    uint16_t packet_index = 8; /* �?8个字节固�? */
    uint8_t bitLen = 0;
    uint32_t uSampleOneClk = 0;
    uint32_t uCurrentClk = 0;

    float curVcc_v = 0.0;
    float curClk_v = 0.0;
    float curIo_v  = 0.0;
    float curRst_v = 0.0;

    packet[2] = 0x81; /* 报文类型 */
    packet[3] = 0x01; /* 报文类型 */
    *(uint32_t *)(packet+4) = stm32_htonl(HAL_GetTick());
    
    if(clkStableFlag == 0){
       g_uStartSampleClkCnt = 0;
       g_uEndSampleClkCnt =  0; 
    }
      
    g_uStartSampleClkCnt = g_uClockCnt;
    uSampleOneClk =  (g_uStartSampleClkCnt - g_uEndSampleClkCnt)/32;
    g_uEndSampleClkCnt = g_uStartSampleClkCnt;

    for (index = 0; index < SAMPLING_COUNT; index++) {
        memset(&data, 0, sizeof(SAMPLING_DATA));
        bitLen = 0;
        uCurrentClk = g_uStartSampleClkCnt + uSampleOneClk * index;
        data.Clock = stm32_htonl(uCurrentClk);
        data.Pin = BIT7; /* 电压 */
        if (ADC_value[index*4] != lastVccData)
        {
            data.Pin = data.Pin | BIT2;
            data.Data0 = ADC_value[index*4];
            lastVccData = ADC_value[index*4];
            bitLen += 12;
        }

        if (ADC_value[index*4+2] != lastRstData)
        {
            data.Pin = data.Pin | BIT1;
            if (bitLen == 12) { /* 数据中包含时钟�?�和电压�? */
                data.Data1 = ADC_value[index*4+2];
            } else if (bitLen == 0){ /* 数据中包含时钟�?�或电压�? */
                data.Data0 = ADC_value[index*4+2];
            } else {
                //print error
            }
            bitLen += 12;
            lastRstData = ADC_value[index*4+2];
        }

        if (ADC_value[index*4+1] != lastIOData)
        {
            data.Pin = data.Pin | BIT0;
            if (bitLen == 24){ /* 数据中包含时钟�?��?�电压�?�和IO�? */
                data.Data2 = ADC_value[index*4+1];
            } else if (bitLen == 12) {
                data.Data1 =  ADC_value[index*4+1];
            } else if(bitLen == 0) {
                data.Data0 = ADC_value[index*4+1];
            } else {
                //error
            }
            bitLen += 12;
            lastIOData = ADC_value[index*4+1];
        }
#if 0
        usb_printf("uCurrentClk = %u, curVcc_v = %f, curClk_v = %f, curIo_v = %f, Amp = %f, curRst_v = %f\r\n", 
		uCurrentClk,
            VOL(ADC_value[index*4]),
            VOL(ADC_value[index*4+1]), VOL(ADC_value[index*4+2]), (VOL(ADC_value[index*4+2])/3300)*1000000, VOL(ADC_value[index*4+3]));
#else
usb_printf("[%u,%d] %f %f %f %f %f\r\n", 
		        uCurrentClk,                //clk
				    index,                      
            VOL(ADC_value[index*4]),        //vcc
            VOL(ADC_value[index*4+3]),      //clk
				    VOL(ADC_value[index*4+1]),      //io
				    VOL(ADC_value[index*4+2]),      //rst
  			    (VOL(ADC_value[index*4+1])/3300)*1000000); //io amp

#endif        
        if (LauchTestCaseFlag == 1)
        {
            curVcc_v = VOL(ADC_value[index*4]);         /*vcc*/
            curClk_v = VOL(ADC_value[index*4+3]);       /*clk*/
            curIo_v  = VOL(ADC_value[index*4+1]);       /*io*/
            curRst_v = VOL(ADC_value[index*4+2]);       /*rst*/
            //curVccAmp = vcc_amp_count(ADC_value[4], ADC_value[5]);
            if (ClkLevel(curClk_v) == 'H'){
                HAL_GPIO_WritePin(DATA_GPIO_Port, DATA_Pin, GPIO_PIN_SET);
            }else
                HAL_GPIO_WritePin(DATA_GPIO_Port, DATA_Pin, GPIO_PIN_RESET);
                    

            switch(g_caseNumber){
                case 1:
                    te_init_pwr_seq(&TePwrSeqState, curVcc_v, curClk_v, curIo_v, curRst_v);
                    te_3v3_pwr_seq(&TePwrSeqState, uCurrentClk, curVcc_v, curClk_v, curIo_v, curRst_v);
                    te_2nd_5v_pwr_seq(&TePwrSeqState,  uCurrentClk, curVcc_v, curClk_v, curIo_v, curRst_v);
                    break;
                case 2:
                    te_init_pwr_seq(&TePwrSeqState, curVcc_v, curClk_v, curIo_v, curRst_v);
                    te_1v8_pwr_seq(&TePwrSeqState,  uCurrentClk, curVcc_v, curClk_v, curIo_v, curRst_v);
                    te_2nd_3v3_pwr_seq(&TePwrSeqState,  uCurrentClk, curVcc_v, curClk_v, curIo_v, curRst_v);
                    break;
                case 3:
                    te_3v3_pwr_down_seq(&g_pwrDownState, uCurrentClk, curVcc_v, curClk_v, curIo_v, curRst_v);
                    te_2nd_5v_pwr_down_seq(&g_pwrDownState, uCurrentClk, curVcc_v, curClk_v, curIo_v, curRst_v);
                    break;
                case 4:
                    te_1v8_pwr_down_seq(&g_pwrDownState, uCurrentClk, curVcc_v, curClk_v, curIo_v, curRst_v);
                    te_2nd_3v3_pwr_down_seq(&g_pwrDownState, uCurrentClk, curVcc_v, curClk_v, curIo_v, curRst_v);
                    break;
                default:
                    break;
            }
        }

        dataLength = (bitLen / 8) + ((bitLen % 8) ? 1 : 0) + 5;
        memcpy((packet + packet_index), &data, dataLength);
        packet_index += dataLength;
    }
    
	*((uint16_t *)packet) = stm32_htons(packet_index);
    return packet_index;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  uint32_t sendLen = 0;
  uint8_t sampleInit = 0;
  uint32_t time = 0;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  USB_Status_Init();
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while(1){
    /* USER CODE END WHILE */
		/* USER CODE BEGIN 3 */
    switch(workState){
        case INIT_STATE:
            /* Initialize all configured peripherals */
            MX_GPIO_Init();
            MX_DMA_Init();
            MX_ADC1_Init();
            MX_TIM4_Init();
            MX_USB_DEVICE_Init();
            MX_RTC_Init();
            TePwrSeqState = INIT;
            TePreErrState = INIT;
		    HAL_GPIO_WritePin(CLK_CNT_EN_GPIO_Port, CLK_CNT_EN_Pin, GPIO_PIN_RESET);

            workState = IDLE_STATE;
		    HAL_Delay(500);
            break;
        case IDLE_STATE:
            if (StartSamplingFlag == 1) {
				startup_info_report();
                workState = SAMPLE_STATE;
            }
            else
                HAL_Delay(100);
            break;
        case SAMPLE_STATE:
            if ((sampleInit == 0) && (StartSamplingFlag == 1)) {
                HAL_ADCEx_Calibration_Start(&hadc1);
                HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&ADC_value, ADC_CHANNEL_CNT * SAMPLING_COUNT);
                HAL_GPIO_WritePin(GPIOB, LOCAL_Pin, GPIO_PIN_SET);
                sampleInit = 1;
            }
            if (StartSamplingFlag == 1)
            {
                sendLen = ADC_MultiChannelPolling(sendBuf);

                if ((HAL_GetTick()%1000) == 0)
                    HAL_GPIO_TogglePin(DATA_GPIO_Port, DATA_Pin);

                //CDC_Transmit_FS(sendBuf, sendLen);
            }
            if (LauchTestCaseFlag == 1) {
                g_caseState = execTestCase(g_caseNumber, sendBuf);
            }
            else
            {
                workState = TEST_END_STATE;
            }
            break;
        case TEST_END_STATE:
            if ((LauchTestCaseFlag == 0) && (g_testEndFlag == 0)) {
                endTestCase(g_caseNumber, &g_caseState);
				g_testEndFlag = 1;
			}
						
            if (StartSamplingFlag == 1)
                  workState = SAMPLE_STATE;
            else {
                /*stop adc
                  stop tm
                  stop rtc
                */
                workState = IDLE_STATE;
                HAL_GPIO_WritePin(GPIOB, LOCAL_Pin, GPIO_PIN_RESET);						
			}
            break;
        default:
            ;
    }
#if DEBUG_TEST == 1
    HAL_RTC_GetTime(&hrtc, &stimestructure, RTC_FORMAT_BIN);
    /* Get the RTC current Date */
    HAL_RTC_GetDate(&hrtc, &sdatestructure, RTC_FORMAT_BIN);
    /* Display date Format : yy/mm/dd */
    usb_printf("%02d/%02d/%02d\r\n", 2000 + sdatestructure.Year, sdatestructure.Month, sdatestructure.Date);
    /* Display time Format : hh:mm:ss */
    usb_printf("%02d:%02d:%02d\r\n", stimestructure.Hours, stimestructure.Minutes, stimestructure.Seconds);
    uiFrequency = 1000000 / uiCycle;
//    printf("占空�?:%dus  周期:%dus  频率:%dHz \r\n", uiDutyCycle, uiCycle, uiFrequency);
    HAL_GPIO_TogglePin(DATA_GPIO_Port, DATA_Pin); /* 翻转GPIO引脚电平 */
    HAL_Delay(500); /* 延时500ms */
#endif
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC
                              |RCC_PERIPHCLK_USB;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_HSE_DIV128;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void USB_Status_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11|GPIO_PIN_12, GPIO_PIN_RESET);

    /*Configure GPIO pin : PA11 PA12 */	
    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_Delay(10);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
