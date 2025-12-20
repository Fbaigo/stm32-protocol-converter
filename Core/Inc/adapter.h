/*
 * core_tasks.h
 *
 *  Created on: Dec 15, 2025
 *      Author: federico
 */

#ifndef INC_ADAPTER_H_
#define INC_ADAPTER_H_

#include "stm32f1xx_hal.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdlib.h>



void uart_rx_complete_cb(UART_HandleTypeDef *huart);
void setup_core_tasks(void);


#endif /* INC_ADAPTER_H_ */
