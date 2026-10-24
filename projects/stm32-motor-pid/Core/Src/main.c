/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
/* main.h: 包含 STM32 HAL 全部驱动头文件 */
#include "main.h"
/* tim.h: 声明 htim1/htim3/htim4 和 MX_TIMx_Init */
#include "tim.h"
/* usart.h: 声明 huart1/huart3 和串口初始化函数 */
#include "usart.h"
/* gpio.h: 声明 MX_GPIO_Init */
#include "gpio.h"
#include "stdio.h"
/* rtthread.h: RT-Thread 内核 API（这里用来让 main 线程睡眠让出 CPU） */
#include "rtthread.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* 用户自己写的三个模块：电机、编码器、应用任务 */
#include "bsp_motor.h"
#include "bsp_encoder.h"
#include "app_task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
extern volatile int rt_uart1_ready;   /* board.c 里定义的标志：UART1 是否可用 */

/* 让标准库 printf/sprintf 的最终输出走 USART1 */
int fputc(int ch, FILE *f)
{
    /* 把 printf 的每一个字符用串口发出去 */
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
    return ch;    /* 必须返回字符，printf 才认为发送成功 */
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* 1) HAL 初始化：复位外设、配置中断分组、
        并把 TIM6 配成 HAL 自己的时基（HAL_IncTick） */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* 2) 配置系统时钟：HSE + PLL → 168MHz */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 3) 初始化 CubeMX 里勾选的外设 */
  MX_GPIO_Init();
  MX_TIM1_Init();          /* TIM1：双路 PWM，周期 8399 → 20kHz */
  MX_TIM3_Init();          /* TIM3：电机 A 编码器 */
  MX_TIM4_Init();          /* TIM4：电机 B 编码器 */
  MX_USART1_UART_Init();   /* USART1：调试/命令/上报 115200 */
  MX_USART3_UART_Init();   /* USART3：预留（蓝牙等）9600 */
  /* USER CODE BEGIN 2 */
	/* 4) CPU 主频已从 16MHz 变成 168MHz，重新给 SysTick 装正确的定时值 */
	SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);
	rt_uart1_ready = 1;   /* 5) USART1 就绪，允许 RT-Thread 控制台输出 */
	/* 5.1) 使能 USART1 接收中断：每收到一个字节立刻存进环形缓冲，
	       这样 ctrl 线程刷 0,0,0 时也不会丢串口命令 */
	HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(USART1_IRQn);
	__HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
	/* 上电自检提示：如果串口能看到这行字，说明 MCU、时钟、串口、RT-Thread 都正常 */
	rt_kprintf("\r\n[BOOT] system ready, try: rpm 60 / pwm a 300\r\n");
	Motor_Init();      /* 6) 电机：拉低 STBY、方向复位、PWM=0、启动 PWM 通道 */
	Encoder_Init();    /* 7) 编码器：启动 TIM3/TIM4 并清零 */
	App_Init();        /* 8) 应用：创建命令队列/互斥量/ctrl/bt 线程，最后使能电机 */
	/* 8.5) 上电提示：看到这行说明新固件已烧录成功、串口收发链路已通；
	        后面才是 ctrl 线程每 100ms 刷的 0,0,0 */
	rt_kprintf("\r\n[app] ready: send 'rpm 60' or 'pwm a 300'\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)    /* 9) 主线程空闲；真正的控制由 ctrl/bt 等 RT-Thread 线程完成 */
  {
    /* 关键：main 线程优先级(10)高于 FinSH 命令线程(21)。
       如果这里空转不睡眠，命令线程永远得不到 CPU，串口输入会完全失效，
       只会看到 ctrl 线程刷 0,0,0。睡 10ms 主动让出 CPU。 */
    rt_thread_mdelay(10);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  /* 这两个结构体用来描述“时钟配置”，下面逐步填好再交给 HAL */
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();                    /* 打开电源管理时钟 */
  /* F407 工作在 168MHz 需要把电压档设为 Scale1 */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE; /* 选外部晶振 */
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;                    /* 打开 HSE */
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;                /* 使用 PLL */
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;        /* PLL 输入=HSE */
  RCC_OscInitStruct.PLL.PLLM = 8;      /* HSE 8MHz ÷ 8 = 1MHz */
  RCC_OscInitStruct.PLL.PLLN = 336;    /* × 336 = 336MHz */
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2; /* ÷ 2 = 168MHz 系统时钟 */
  RCC_OscInitStruct.PLL.PLLQ = 4;      /* 给 USB 等外设用的时钟 */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)  /* 交给 HAL 执行 */
  {
    Error_Handler();      /* 时钟配置失败只能停在这里 */
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; /* 系统时钟=PLL */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;   /* AHB=168MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;    /* APB1=42MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;    /* APB2=84MHz */

  /* 烧录器延时 5 个周期；配置后 HAL 会自动重配 TIM6 时基 */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();      /* 总线时钟配置失败则停机 */
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* HAL 通用回调：哪个定时器周期到了，就会进这里 */
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6) {   /* 判断中断来自 TIM6 */
    HAL_IncTick();                /* HAL 自己的毫秒计数器 +1 */
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* 任何 HAL 初始化失败都会调到这：关中断并死循环，方便调试 */
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();    /* 关掉所有中断 */
  while (1)           /* 永久停住 */
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
