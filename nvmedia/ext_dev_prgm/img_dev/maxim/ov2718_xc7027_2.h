
#ifndef _OV2718_XC7027_2_H_
#define _OV2718_XC7027_2_H_

#include "nvmedia_isc.h"
#include "log_utils.h"



void SetI2cFun(NvMediaISCSupportFunctions *func, NvMediaISCTransactionHandle *transaction);
void SetI2cFunOv(NvMediaISCSupportFunctions *func, NvMediaISCTransactionHandle *transaction);
void XC7027_i2c_bypass_on();
void XC7027_i2c_bypass_off();
void XC7027MIPIOpen(void);
void OV2718MIPIOpen(void);


#endif
