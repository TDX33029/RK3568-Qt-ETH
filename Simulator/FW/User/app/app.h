/**
  ******************************************************************************
  * @file    app.h
  * @brief   应用层：命令解析 / 分发、仿真主循环、遥测
  ******************************************************************************
  */
#ifndef __APP_H
#define __APP_H

#include <stdint.h>
#include "proto.h"

void APP_Init(void);
void APP_Step(void);     /* 在 main 循环里持续调用 */

const ProtoConfig_t *APP_GetConfig(void);
uint8_t APP_IsRunning(void);

#endif /* __APP_H */
