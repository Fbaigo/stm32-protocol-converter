/*
 * core_tasks.h
 *
 *  Created on: Dec 15, 2025
 *      Author: federico
 */

#ifndef INC_CORE_TASKS_H_
#define INC_CORE_TASKS_H_

#include "stm32f1xx_hal.h"
#include "cmsis_os.h"
#include <string.h>

///! Interfaces
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;

///! UART iface for the console task
#define UART_RX_BUFFER_LEN	4

typedef struct {
  uint8_t data[UART_RX_BUFFER_LEN];
  uint8_t id;
} console_msg_t;

typedef enum {CMDOK, CMDNOK} console_stat_t;
typedef enum {SPI_UP, SPI_DOWN, I2C_UP, I2C_DOWN, HELP, TOTAL_IDS, INVALID} console_ids_t;

///! Tasks
extern osMessageQueueId_t uart_msg_queue;
extern uint8_t rx_buffer[UART_RX_BUFFER_LEN];

void uart_rx_complete_cb(UART_HandleTypeDef *huart);
void console_task(void *argument);
void led_task(void *args);
void setup_core_tasks(void);

extern osThreadId_t console_task_handler;
const extern osThreadAttr_t console_task_attr;

extern osThreadId_t led_task_handler;
const extern osThreadAttr_t led_task_attr;

#endif /* INC_CORE_TASKS_H_ */
