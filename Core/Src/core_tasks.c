/*
 * core_tasks.c
 *
 *  Created on: Dec 15, 2025
 *      Author: federico
 */
#include "core_tasks.h"

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;

osMessageQueueId_t uart_msg_queue;
uint8_t rx_buffer[UART_RX_BUFFER_LEN];

osThreadId_t console_task_handler;
osThreadId_t led_task_handler;

const osThreadAttr_t console_task_attr = {
  .name = "Console",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

const osThreadAttr_t led_task_attr = {
	.name = "Green LED",
	.stack_size = 128,
	.priority = (osPriority_t) osPriorityNormal
};

const uint8_t help_text[] = "Supported commands:\nSPI_UP: Start SPI1 interface\nSPI_DOWN: Terminate SPI1 interface\nI2C_UP\nI2C_DOWN\nHELP";

///! Initialization
void setup_core_tasks(void){
  uart_msg_queue = osMessageQueueNew(1, sizeof(console_msg_t), NULL);

  console_task_handler = osThreadNew(console_task, NULL, &console_task_attr);
  led_task_handler = osThreadNew(led_task, NULL, &led_task_attr);
}



///! Core
static console_stat_t process_uart_directive(console_msg_t msg){
	console_stat_t status = CMDOK;

	switch(msg.id){
		case HELP:
			memcpy(msg.data, help_text, sizeof(help_text));
			//HAL_UART_Transmit(&huart1, msg.data, sizeof(msg.data), 0xFFFF);
			break;
		default:
			status = CMDNOK;
			break;
		}

	return status;
}

void console_task(void *argument)
{
	console_msg_t msg;
	osStatus_t status;

	while(1)
	{
	    status = osMessageQueueGet(uart_msg_queue, &msg, NULL, osWaitForever);
	    if (status == osOK) {
	    	process_uart_directive(msg);
	    }
	}

	osThreadId_t id;
	id = osThreadGetId();

	osThreadTerminate(id);
}

void led_task(void *argument)
{
  osStatus_t  status;
  uint32_t delay = *((uint32_t*) argument);

  while(1)
  {
	HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
	osDelay(1000);
	HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
	osDelay(1000);
  }

  osThreadId_t id;
  id = osThreadGetId();

  osThreadTerminate(id);
}
