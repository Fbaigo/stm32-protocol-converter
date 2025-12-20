/*
 * core_tasks.c
 *
 *  Created on: Dec 15, 2025
 *      Author: federico
 */
#include "stm32f1xx_hal.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdlib.h>

///! Hardware interfaces (extern declaration in stm32f1xx_it.c)
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;

/**
 * UART communication from/to IDE
 */

#define CONSOLE_BUFFER_MAX_LEN	128

static osMessageQueueId_t uart_rx_queue;
static uint8_t rx_buffer[CONSOLE_BUFFER_MAX_LEN];

struct console_frame {
  uint8_t data[CONSOLE_BUFFER_MAX_LEN];
  uint32_t len;
  uint8_t id;
};

typedef enum {CMDOK, CMDNOK} console_stat_t;
typedef enum {CMD_SPIUP, CMD_SPIDOWN, CMD_I2CUP, CMD_I2CDOWN, CMD_I2CFRAME, CMD_HELP, CMD_TOTAL_IDS, CMD_INVALID} console_cmd_ids_t;
char console_cmds_list[][30] = {
	{"SPIUP"},
	{"SPIDOWN"},
	{"I2CUP"},
	{"I2CDOWN"},
	{"I2CFRAME"},
	{"HELP"}
};

typedef enum {I2C_ADDR1, I2C_ADDR2, I2C_CLOCK, I2C_ADDRMODE_7B, I2C_ADDRMODE_10B, I2C_DADDRMODE, I2C_TOTAL_IDS, I2C_INVALID} i2c_param_ids_t;
char i2c_params_list[][30] = {
		{"ADDR1"},
		{"ADDR2"},
		{"CLOCK"},
		{"ADDRMODE7B"},
		{"ADDRMODE10B"},
		{"DADDRMODE"}
};

static console_stat_t process_directive(struct console_frame msg);
static console_cmd_ids_t get_cmd_id_from_string(char* param);

static i2c_param_ids_t get_i2c_param_id_from_string(char *param);
static console_stat_t i2c_up_directive(char *r_arg, char *r_param);
static console_stat_t set_i2c_param(i2c_param_ids_t param_id, uint32_t val);
static void i2c1_default_conf(void);

static void adapter_gpio_init(void);

static void uart_init(void);
static void uart_rx_idle_cb(UART_HandleTypeDef *huart, uint16_t size);
static void uart_rx_error_cb(UART_HandleTypeDef *huart);
//static void uart_rx_complete_cb(UART_HandleTypeDef *huart);
static void iface_error_handler(void);

static const uint8_t help_msg[] =
		"Supported commands (not case sensitive):\n"
		"SPIUP: Start SPI1 interface\n"
		"SPIDOWN: Terminate SPI1 interface\n"
		"I2CUP: Start I2C1 interface\n"
		"I2CDOWN: Terminate I2C1 interface\n"
		"HELP: Usage of a given command";

static const uint8_t i2c_help_msg[] =
		"The start of frame must be a valid command. See HELP for more\n"
		"I2C Configuration format: CMD:ARG=VAL:ARG=VAL"
		"For example: I2CUP:ADDR1=1:ADDR2=2\n"
		"I2C frame to slave format: CMD:FRAME:FRAME:FRAME"
		"Supported arguments for I2C\n"
		"ADDR1 Own : address #1\n"
		"ADDR2 Own : address #1\n"
		"CLOCK I2C : communication speed\n"
		"ADDRMODE7B : I2C addressing mode 7bits of address\n"
		"ADDRMODE10B : I2C addressing mode 10bits of address\n";

static const uint8_t hal_iface_error_msg[] =
		"Failed to initialize desired interface";


/**
 * Tasks
 */
static osThreadId_t console_task_handler;
static osThreadId_t blinker_task_handler;

static void console_task(void *argument);
static void blinker_task(void *args);

static const osThreadAttr_t console_task_attr = {
  .name = "Console",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

static const osThreadAttr_t blinker_task_attr = {
	.name = "Green LED",
	.stack_size = 128,
	.priority = (osPriority_t) osPriorityNormal
};


///! Initialization
void initialize_adapter(void){
	adapter_gpio_init();
	uart_init();

	///! Register required callbacks and place the UART in receive mode (interrupt)
	//HAL_UART_RegisterCallback(&huart1, HAL_UART_RX_COMPLETE_CB_ID, uart_rx_complete_cb);
	HAL_UART_RegisterCallback(&huart1, HAL_UART_ERROR_CB_ID, uart_rx_error_cb);
	HAL_UART_RegisterRxEventCallback(&huart1, uart_rx_idle_cb);

	HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buffer, sizeof(rx_buffer));
	//HAL_UART_Receive_IT(&huart1, rx_buffer, sizeof(rx_buffer));

	osKernelInitialize();

	uart_rx_queue = osMessageQueueNew(1, sizeof(struct console_frame), NULL);
	console_task_handler = osThreadNew(console_task, NULL, &console_task_attr);
	blinker_task_handler = osThreadNew(blinker_task, NULL, &blinker_task_attr);

	osKernelStart();
}

///! Adapter tasks
static void console_task(void *argument)
{
	struct console_frame msg;
	osStatus_t status;

	while(1)
	{
	    status = osMessageQueueGet(uart_rx_queue, &msg, NULL, osWaitForever);
	    if (status == osOK) {
	    	process_directive(msg);
	    }
	}

	osThreadId_t id;
	id = osThreadGetId();

	osThreadTerminate(id);
}

static void blinker_task(void *argument)
{
  osStatus_t  status;
  const uint32_t delay = 1000;

  while(1)
  {
	HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
	osDelay(delay);
	HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
	osDelay(delay);
  }

  osThreadId_t id;
  id = osThreadGetId();

  osThreadTerminate(id);
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void adapter_gpio_init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
 * UART interface
 */
/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void uart_init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    iface_error_handler();
  }
}

static void uart_rx_idle_cb(UART_HandleTypeDef *huart, uint16_t size){
	struct console_frame msg;
	size_t _size;

	if( !__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE) ){
		__HAL_UART_FLUSH_DRREGISTER(huart);
		HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buffer, sizeof(rx_buffer));
		return;
	}

	_size = size > sizeof(rx_buffer) ? sizeof(rx_buffer) : size;
    memcpy(msg.data, rx_buffer, _size);

    osMessageQueuePut(uart_rx_queue, &msg, 0U, 0U);
	HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buffer, sizeof(rx_buffer));
}

static void uart_rx_error_cb(UART_HandleTypeDef *huart){
	uint32_t uart_error = HAL_UART_GetError(huart); ///! See /** @defgroup UART_Error_Code UART Error Code stm32f1xx_hal_uart.h

	switch(uart_error){
		case HAL_UART_ERROR_ORE:
	          __HAL_UART_CLEAR_OREFLAG(huart);
			break;

		default:
			break;
	}
}

/**
 * Handling of IDE directives and parsing
 */

static console_cmd_ids_t get_cmd_id_from_string(char* param){
	uint32_t ids=0;
	console_cmd_ids_t param_id = CMD_INVALID;

	do{
		if(strcmp(param, console_cmds_list[ids]) == 0){
			param_id = (console_cmd_ids_t) ids;
			break;
		}
		ids++;
	} while(ids < CMD_TOTAL_IDS);

	return param_id;
}

static i2c_param_ids_t get_i2c_param_id_from_string(char *param){
	uint32_t ids=0;
	i2c_param_ids_t param_id = I2C_INVALID;

	do {
		if(strcmp(param, i2c_params_list[ids]) == 0){
			param_id = (i2c_param_ids_t) ids;
			break;
		}
		ids++;
	} while(ids < I2C_TOTAL_IDS);

	return param_id;
}

/**
 * SPI
 */
/*static void spi_up_directive(){
	do {
		val = strtok_r(NULL, "=", &r_param);
		if(val != NULL){
		}

		token = strtok_r(NULL, ":", &r_arg);
		param = strtok_r(token, "=", &r_param);
	} while(token);
}*/

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void i2c1_default_conf(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
}

/**
 * TODO: check values range
 */
static console_stat_t set_i2c_param(i2c_param_ids_t param_id, uint32_t val){
	console_stat_t retval = CMDOK;

	switch(param_id){
		case I2C_ADDR1:
			  hi2c1.Init.OwnAddress1 = val;
			  break;

		case I2C_ADDR2:
			  hi2c1.Init.OwnAddress2 = val;
			  break;

		case I2C_ADDRMODE_7B:
			  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
			  break;

		case I2C_ADDRMODE_10B:
			  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_10BIT;
			  break;

		case I2C_CLOCK:
			  hi2c1.Init.ClockSpeed = val;
			  break;

		case I2C_DADDRMODE:
			  hi2c1.Init.DualAddressMode = val == 0 ? I2C_DUALADDRESS_DISABLE : I2C_DUALADDRESS_ENABLE;
			  break;

		case I2C_INVALID:
		default:
			retval = CMDNOK;
			break;
	}

	return retval;
}

static console_stat_t i2c_up_directive(char *r_arg, char *r_param){
	i2c_param_ids_t i2c_param;
	char *token, *val, *param;
	console_stat_t retval = CMDOK;

	do {
		val = strtok_r(NULL, "=", &r_param);
		if(val != NULL){
			i2c_param = get_i2c_param_id_from_string(param);
			retval = set_i2c_param(i2c_param, atoi(val));
		}

		if(retval != CMDOK){
			break;
		}

		token = strtok_r(NULL, ":", &r_arg);
		param = strtok_r(token, "=", &r_param);
	} while(token);

	return retval;
}

static console_stat_t process_directive(struct console_frame msg){
	console_stat_t status = CMDOK;
	console_cmd_ids_t cmd;
	char *token, *val, *param;
    char *r_arg=NULL, *r_param=NULL;
    const char *arg_delim =":", *param_delim="=";

	///! Example: I2CUP:ADDR1=1:ADDR2=2
    token = strtok_r((char*)msg.data, arg_delim, &r_arg);
	param = strtok_r(token, param_delim, &r_param);
	cmd = get_cmd_id_from_string(param);

	switch(cmd){
		case CMD_SPIUP:
			//spi_up_directive();
			break;

		case CMD_I2CUP:
			i2c1_default_conf();
			status = i2c_up_directive(r_arg, r_param);

			if(status != CMDOK){
				HAL_UART_Transmit(&huart1, i2c_help_msg, sizeof(i2c_help_msg), 0xFFFF);
			}
			else if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
				HAL_UART_Transmit(&huart1, hal_iface_error_msg, sizeof(hal_iface_error_msg), 0xFFFF);
				status = CMDNOK;
			}
			break;

		case CMD_I2CDOWN:
			if(HAL_I2C_DeInit(&hi2c1) != HAL_OK){
				status = CMDNOK;
			}

			break;

		case CMD_I2CFRAME:
			break;

		case CMD_HELP:
			HAL_UART_Transmit(&huart1, help_msg, sizeof(help_msg), 0xFFFF);
			break;

		case CMD_INVALID:
		default:
			HAL_UART_Transmit(&huart1, help_msg, sizeof(help_msg), 0xFFFF);
			status = CMDNOK;
			break;
	}

	return status;
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
static void iface_error_handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
