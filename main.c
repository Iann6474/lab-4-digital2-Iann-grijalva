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
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

// Maquina de estados del juego:
// STATE_IDLE       = esperando que alguien presione el boton de inicio
// STATE_COUNTDOWN  = conteo regresivo 5->0, jugadores bloqueados
// STATE_RACING     = carrera en curso, jugadores pueden presionar
// STATE_FINISHED   = alguien llego a la meta, juego terminado
typedef enum {
    STATE_IDLE,
    STATE_COUNTDOWN,
    STATE_RACING,
    STATE_FINISHED
} GameState;

// Estado actual del juego, empieza esperando el boton de inicio
volatile GameState gameState = STATE_IDLE;

// Contadores de cuantas veces ha presionado cada jugador
volatile uint8_t counter_j1 = 0;
volatile uint8_t counter_j2 = 0;

// Guarda quien gano: 0=nadie todavia, 1=J1 gano, 2=J2 gano
volatile uint8_t winner = 0;

// Numero de presiones necesarias para ganar la carrera
#define RACE_TARGET   9

// Tiempo minimo entre presiones para filtrar rebotes del boton (ms)
#define DEBOUNCE_MS   50

// Guarda el tiempo (en ms) de la ultima presion valida de cada jugador
volatile uint32_t lastTime_j1 = 0;
volatile uint32_t lastTime_j2 = 0;

// Bandera que se activa en la interrupcion del boton de inicio
// El main loop la revisa para arrancar el countdown sin bloquear la ISR
volatile uint8_t flag_inicio = 0;

// Debounce para el boton de inicio
volatile uint32_t lastTime_inicio = 0;

// Tabla de segmentos para display de 7
// Bits corresponden a: g f e d c b a
//                      6 5 4 3 2 1 0
const uint8_t tabla_7seg[10] = {
    0x3F,  // 0 -> 0b00111111 -> segmentos a,b,c,d,e,f
    0x06,  // 1 -> 0b00000110 -> segmentos b,c
    0x5B,  // 2 -> 0b01011011 -> segmentos a,b,d,e,g
    0x4F,  // 3 -> 0b01001111 -> segmentos a,b,c,d,g
    0x66,  // 4 -> 0b01100110 -> segmentos b,c,f,g
    0x6D,  // 5 -> 0b01101101 -> segmentos a,c,d,f,g
    0x7D,  // 6 -> 0b01111101 -> segmentos a,c,d,e,f,g
    0x07,  // 7 -> 0b00000111 -> segmentos a,b,c
    0x7F,  // 8 -> 0b01111111 -> todos los segmentos
    0x6F   // 9 -> 0b01101111 -> segmentos a,b,c,d,f,g
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

void LEDs_J1_Set(uint8_t value);
void LEDs_J2_Set(uint8_t value);
void Display_Mostrar(uint8_t numero);
void Display_Apagar(void);
void Check_Winner(void);
void Hacer_Countdown(void);
void Reiniciar_Juego(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// -----------------------------------------------------------------------
// Funcion: LEDs_J1_Set
// Enciende los 4 LEDs del jugador 1 segun el valor en binario (0-15)
// -----------------------------------------------------------------------
void LEDs_J1_Set(uint8_t value)
{
    HAL_GPIO_WritePin(LED1J1_GPIO_Port,   LED1J1_Pin,   (value >> 0) & 1);
    HAL_GPIO_WritePin(LED2J2B4_GPIO_Port, LED2J2B4_Pin, (value >> 1) & 1);
    HAL_GPIO_WritePin(LED3J1_GPIO_Port,   LED3J1_Pin,   (value >> 2) & 1);
    HAL_GPIO_WritePin(LED4J1_GPIO_Port,   LED4J1_Pin,   (value >> 3) & 1);
}

// -----------------------------------------------------------------------
// Funcion: LEDs_J2_Set
// Enciende los 4 LEDs del jugador 2 segun el valor en binario (0-15)
// -----------------------------------------------------------------------
void LEDs_J2_Set(uint8_t value)
{
    HAL_GPIO_WritePin(LED1J2_GPIO_Port, LED1J2_Pin, (value >> 0) & 1);
    HAL_GPIO_WritePin(LED2J2_GPIO_Port, LED2J2_Pin, (value >> 1) & 1);
    HAL_GPIO_WritePin(LED3J2_GPIO_Port, LED3J2_Pin, (value >> 2) & 1);
    HAL_GPIO_WritePin(LED4J2_GPIO_Port, LED4J2_Pin, (value >> 3) & 1);
}

// -----------------------------------------------------------------------
// Funcion: Display_Mostrar
// Muestra un numero del 0 al 9 en el display de 7 segmentos
// -----------------------------------------------------------------------
void Display_Mostrar(uint8_t numero)
{
    if (numero > 9) return;

    uint8_t patron = tabla_7seg[numero];

    HAL_GPIO_WritePin(SEG_A_GPIO_Port, SEG_A_Pin, (patron >> 0) & 1);
    HAL_GPIO_WritePin(SEG_B_GPIO_Port, SEG_B_Pin, (patron >> 1) & 1);
    HAL_GPIO_WritePin(SEG_C_GPIO_Port, SEG_C_Pin, (patron >> 2) & 1);
    HAL_GPIO_WritePin(SEG_D_GPIO_Port, SEG_D_Pin, (patron >> 3) & 1);
    HAL_GPIO_WritePin(SEG_E_GPIO_Port, SEG_E_Pin, (patron >> 4) & 1);
    HAL_GPIO_WritePin(SEG_F_GPIO_Port, SEG_F_Pin, (patron >> 5) & 1);
    HAL_GPIO_WritePin(SEG_G_GPIO_Port, SEG_G_Pin, (patron >> 6) & 1);
}

// -----------------------------------------------------------------------
// Funcion: Display_Apagar
// Apaga todos los segmentos del display
// -----------------------------------------------------------------------
void Display_Apagar(void)
{
    HAL_GPIO_WritePin(SEG_A_GPIO_Port, SEG_A_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SEG_B_GPIO_Port, SEG_B_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SEG_C_GPIO_Port, SEG_C_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SEG_D_GPIO_Port, SEG_D_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SEG_E_GPIO_Port, SEG_E_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SEG_F_GPIO_Port, SEG_F_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SEG_G_GPIO_Port, SEG_G_Pin, GPIO_PIN_RESET);
}

// -----------------------------------------------------------------------
// Funcion: Reiniciar_Juego
// Resetea todos los contadores y variables para una nueva carrera
// Se llama antes de cada countdown para arrancar limpio
// -----------------------------------------------------------------------
void Reiniciar_Juego(void)
{
    counter_j1 = 0;
    counter_j2 = 0;
    winner     = 0;

    // Apagamos todos los LEDs para que arranquen desde 0
    LEDs_J1_Set(0x00);
    LEDs_J2_Set(0x00);
    Display_Apagar();
}

// -----------------------------------------------------------------------
// Funcion: Hacer_Countdown
// Realiza el conteo regresivo 5->4->3->2->1->0 en el display
// Durante este tiempo el estado es STATE_COUNTDOWN, lo que bloquea
// los botones de los jugadores en el Callback
// Al terminar pone el estado en STATE_RACING para habilitar los botones
// -----------------------------------------------------------------------
void Hacer_Countdown(void)
{
    // Cambiamos el estado a COUNTDOWN para bloquear los botones
    gameState = STATE_COUNTDOWN;

    // Mostramos cada numero por 1 segundo (1000 ms)
    // HAL_Delay bloquea la ejecucion ese tiempo, lo cual esta bien aqui
    // porque no necesitamos hacer nada mas durante el countdown
    for (int i = 5; i >= 0; i--)
    {
        Display_Mostrar(i);   // Mostramos el numero actual
        HAL_Delay(1000);      // Esperamos 1 segundo
    }

    // Terminamos el countdown, apagamos el display
    // y habilitamos la carrera
    Display_Apagar();
    gameState = STATE_RACING;
}

// -----------------------------------------------------------------------
// Funcion: Check_Winner
// Revisa si algun jugador llego a la meta
// Enciende LEDs del ganador y muestra su numero en el display
// -----------------------------------------------------------------------
void Check_Winner(void)
{
    if (counter_j1 >= RACE_TARGET && winner == 0)
    {
        winner = 1;
        gameState = STATE_FINISHED;

        LEDs_J1_Set(0x0F);      // Todos los LEDs de J1 ON
        LEDs_J2_Set(0x00);      // Todos los LEDs de J2 OFF
        Display_Mostrar(1);     // Display muestra "1" = gano J1
    }
    else if (counter_j2 >= RACE_TARGET && winner == 0)
    {
        winner = 2;
        gameState = STATE_FINISHED;

        LEDs_J2_Set(0x0F);      // Todos los LEDs de J2 ON
        LEDs_J1_Set(0x00);      // Todos los LEDs de J1 OFF
        Display_Mostrar(2);     // Display muestra "2" = gano J2
    }
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

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */

  // Habilitamos interrupciones de los botones
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  // Arrancamos con todo apagado y esperando el boton de inicio
  LEDs_J1_Set(0x00);
  LEDs_J2_Set(0x00);
  Display_Apagar();

  // El juego empieza en IDLE, esperando que se presione BTNINICIO
  gameState = STATE_IDLE;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    // Revisamos si la interrupcion del boton de inicio activo la bandera
    // Usamos una bandera en lugar de hacer todo en la ISR porque
    // HAL_Delay no se puede usar dentro de interrupciones
    if (flag_inicio == 1)
    {
        flag_inicio = 0;         // Limpiamos la bandera

        Reiniciar_Juego();       // Reseteamos contadores y LEDs
        Hacer_Countdown();       // Countdown 5->0 en el display
                                 // Al terminar Hacer_Countdown pone
                                 // gameState = STATE_RACING automaticamente
    }

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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
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
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, SEG_F_Pin|SEG_E_Pin|SEG_G_Pin|LED2J2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, SEG_A_Pin|SEG_B_Pin|SEG_C_Pin|LD2_Pin
                          |LED4J2_Pin|LED1J2_Pin|LED4J1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, SEG_D_Pin|LED3J1_Pin|LED2J2B4_Pin|LED1J1_Pin
                          |LED3J2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : BTNINICIO_Pin */
  GPIO_InitStruct.Pin = BTNINICIO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BTNINICIO_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SEG_F_Pin SEG_E_Pin SEG_G_Pin LED2J2_Pin */
  GPIO_InitStruct.Pin = SEG_F_Pin|SEG_E_Pin|SEG_G_Pin|LED2J2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : SEG_A_Pin SEG_B_Pin SEG_C_Pin LD2_Pin
                           LED4J2_Pin LED1J2_Pin LED4J1_Pin */
  GPIO_InitStruct.Pin = SEG_A_Pin|SEG_B_Pin|SEG_C_Pin|LD2_Pin
                          |LED4J2_Pin|LED1J2_Pin|LED4J1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : USART_TX_Pin USART_RX_Pin */
  GPIO_InitStruct.Pin = USART_TX_Pin|USART_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : BTNJ2_Pin BTNJ1_Pin */
  GPIO_InitStruct.Pin = BTNJ2_Pin|BTNJ1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : SEG_D_Pin LED3J1_Pin LED2J2B4_Pin LED1J1_Pin
                           LED3J2_Pin */
  GPIO_InitStruct.Pin = SEG_D_Pin|LED3J1_Pin|LED2J2B4_Pin|LED1J1_Pin
                          |LED3J2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PD2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// -----------------------------------------------------------------------
// Callback: HAL_GPIO_EXTI_Callback
// HAL llama esta funcion automaticamente cuando se dispara un EXTI
// Maneja los 3 botones: inicio, jugador 1 y jugador 2
// -----------------------------------------------------------------------
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now = HAL_GetTick();

    // ── Boton de Inicio de Carrera (boton azul PC13) ──────────────────────
    if (GPIO_Pin == BTNINICIO_Pin)
    {
        // Debounce para el boton de inicio
        if ((now - lastTime_inicio) > DEBOUNCE_MS)
        {
            lastTime_inicio = now;

            // Solo permitimos iniciar si el juego esta en IDLE o FINISHED
            // Esto permite reiniciar la carrera despues de que alguien gano
            if (gameState == STATE_IDLE || gameState == STATE_FINISHED)
            {
                // Activamos la bandera para que el main loop haga el countdown
                // NO podemos hacer HAL_Delay aqui dentro de la ISR
                flag_inicio = 1;
            }
        }
    }

    // ── Boton Jugador 1 ───────────────────────────────────────────────────
    if (GPIO_Pin == BTNJ1_Pin)
    {
        if ((now - lastTime_j1) > DEBOUNCE_MS)
        {
            lastTime_j1 = now;

            // Solo cuenta si la carrera esta activa (no en countdown ni idle)
            if (gameState == STATE_RACING && winner == 0)
            {
                if (counter_j1 < RACE_TARGET)
                {
                    counter_j1++;
                    LEDs_J1_Set(counter_j1);
                    Check_Winner();
                }
            }
        }
    }

    // ── Boton Jugador 2 ───────────────────────────────────────────────────
    if (GPIO_Pin == BTNJ2_Pin)
    {
        if ((now - lastTime_j2) > DEBOUNCE_MS)
        {
            lastTime_j2 = now;

            // Solo cuenta si la carrera esta activa (no en countdown ni idle)
            if (gameState == STATE_RACING && winner == 0)
            {
                if (counter_j2 < RACE_TARGET)
                {
                    counter_j2++;
                    LEDs_J2_Set(counter_j2);
                    Check_Winner();
                }
            }
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
