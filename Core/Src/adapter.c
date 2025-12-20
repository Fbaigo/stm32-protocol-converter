/*
 * core_tasks.c
 *
 *  Created on: Dec 15, 2025
 *      Author: federico
 */
#include "adapter.h"

///! Hardware interfaces
static I2C_HandleTypeDef hi2c1;
static UART_HandleTypeDef huart1;

/**
 * UART communication from/to IDE
 */
static const uint8_t help_msg[] = "Supported commands (not case sensitive):\n"
		"SPIUP: Start SPI1 interface\n"
		"SPIDOWN: Terminate SPI1 interface\n"
		"I2CUP: Start I2C1 interface\n"
		"I2CDOWN: Terminate I2C1 interface\n"
		"HELP: Usage of a given command";

static const uint8_t i2c_help_msg[];

#define CONSOLE_BUFFER_MAX_LEN	128
osMessageQueueId_t uart_msg_queue;
uint8_t rx_buffer[CONSOLE_BUFFER_MAX_LEN];

struct console_frame {
  uint8_t data[CONSOLE_BUFFER_MAX_LEN];
  uint32_t len;
  uint8_t id;
};

typedef enum {CMDOK, CMDNOK} console_stat_t;
typedef enum {CMD_SPIUP, CMD_SPIDOWN, CMD_I2CUP, CMD_I2CDOWN, CMD_HELP, CMD_TOTAL_IDS, CMD_INVALID} console_cmd_ids_t;
char console_cmds_list[30][] = {
	{"SPIUP"},
	{"SPIDOWN"},
	{"I2CUP"},
	{"I2CDOWN"},
	{"HELP"}
};

typedef enum {I2C_ADDR1, I2C_ADDR2, I2C_CLOCK, I2C_ADDRMODE_7B, I2C_ADDRMODE_10B, I2C_DADDRMODE, I2C_TOTAL_IDS, I2C_INVALID} i2c_param_ids_t;
char i2c_params_list[30][] = {
		{"ADDR1"},
		{"ADDR2"},
		{"CLOCK"},
		{"ADDRMODE7B"},
		{"ADDRMODE10B"},
		{"DADDRMODE"}
};

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
void setup_core_tasks(void){
  uart_msg_queue = osMessageQueueNew(1, sizeof(console_frame), NULL);

  console_task_handler = osThreadNew(console_task, NULL, &console_task_attr);
  led_task_handler = osThreadNew(blinker_task, NULL, &blinker_task_attr);
}

///! Core


void console_task(void *argument)
{
	console_frame_t msg;
	osStatus_t status;

	while(1)
	{
	    status = osMessageQueueGet(uart_msg_queue, &msg, NULL, osWaitForever);
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
 *
 */
static void usart1_rx_to_idle_cb(UART_HandleTypeDef *huart, uint16_t size){
	console_frame_t msg;
	size_t _size;

	if( !__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE) ){
		__HAL_UART_FLUSH_DRREGISTER(huart);
		HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buffer, sizeof(rx_buffer));
		return;
	}

	_size = size > sizeof(rx_buffer) ? sizeof(rx_buffer) : size;
    memcpy(msg.data, rx_buffer, _size);

    if(msg.data[0] > TOTAL_IDS){
    	msg.id = INVALID;
    }
    else {
    	msg.id = (console_ids_t) msg.data[0];
    }

    osMessageQueuePut(uart_msg_queue, &msg, 0U, 0U);
	HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buffer, sizeof(rx_buffer));
}

void uart_rx_error_cb(UART_HandleTypeDef *huart){
	uint32_t uart_error = HAL_UART_GetError(huart); ///! See /** @defgroup UART_Error_Code UART Error Code stm32f1xx_hal_uart.h

	switch(uart_error){
		case HAL_UART_ERROR_ORE:
	          __HAL_UART_CLEAR_OREFLAG(huart);
			break;

		default:
			break;
	}
}

void uart_rx_complete_cb(UART_HandleTypeDef *huart){
	console_frame_t msg;

    memcpy(msg.data, rx_buffer, 4);

    if(msg.data[0] > TOTAL_IDS){
    	msg.id = INVALID;
    }
    else {
    	msg.id = (console_ids_t) msg.data[0];
    }

    osMessageQueuePut(uart_msg_queue, &msg, 0U, 0U);

	__HAL_UART_FLUSH_DRREGISTER(huart);	///! Just in case to avoid ORE error
	HAL_UART_Receive_IT(&huart1, rx_buffer, sizeof(rx_buffer));
}

void uart_cb_init(){
  ///! Register a callback and place the UART in receive mode (interrupt)
  HAL_UART_RegisterCallback(&huart1, HAL_UART_RX_COMPLETE_CB_ID, uart_rx_complete_cb);
  HAL_UART_RegisterCallback(&huart1, HAL_UART_ERROR_CB_ID, uart_rx_error_cb);
  HAL_UART_RegisterRxEventCallback(&huart1, usart1_rx_to_idle_cb);

  HAL_UARTEx_ReceiveToIdle_IT(&huart1, rx_buffer, sizeof(rx_buffer));
  //HAL_UART_Receive_IT(&huart1, rx_buffer, sizeof(rx_buffer));
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
static void spi_up_directive(){
	do {
		val = strtok_r(NULL, "=", &r_param);
		if(val != NULL){
		}

		token = strtok_r(NULL, ":", &r_arg);
		param = strtok_r(token, "=", &r_param);
	} while(token);
}

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
static void set_i2c_param(i2c_param_ids_t param_id, uint32_t val){
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
			break;
	}
}



static void i2c_up_directive(char *r_arg, char *r_param){
	i2c_param_ids_t i2c_param;
	char *token, *val, *param;

	do {
		val = strtok_r(NULL, "=", &r_param);
		if(val != NULL){
			i2c_param = get_i2c_param_id_from_string(param);
			set_i2c_param(i2c_param, atoi(val));
		}

		token = strtok_r(NULL, ":", &r_arg);
		param = strtok_r(token, "=", &r_param);
	} while(token);
}


static console_stat_t process_directive(struct console_frame msg){
	console_stat_t status = CMDOK;
	console_cmd_ids_t cmd;
	char *token, *val, *param;
    char *r_arg=NULL, *r_param=NULL;

	///! Example: I2CUP:ADDR1=1:ADDR2=2
    token = strtok_r((char*)msg, ":", &r_arg);
	param = strtok_r(token, "=", &r_param);
	cmd = get_cmd_id_from_string(param);

	switch(cmd){
		case CMD_SPIUP:
			spi_up_directive();
			break;

		case CMD_I2CUP:
			i2c1_default_conf();
			i2c_up_directive(r_arg, r_param);
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
