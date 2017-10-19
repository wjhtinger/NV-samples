/* Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "testutil_i2c.h"
#include "log_utils.h"
#include "parser.h"

NvMediaStatus
ParseRegistersFile(char *filename,
                   CaptureConfigParams *params,
                   I2cCommands *allCommands)
{
    char readLine [MAX_STRING_SIZE];
    char parsedLine [MAX_STRING_SIZE];
    char stringBuf [MAX_STRING_SIZE];
    NvU32 uIntBuf;
    int intBuf;
    NvMediaBool isGroupRegister = NVMEDIA_FALSE;
    NvMediaBool isInitialRegister = NVMEDIA_FALSE;
    NvU32 numCommands = 0;
    NvU32 i2cDevice = 0;
    NvU32 deviceAddress = 0;
    NvU32 address = 0;
    NvU32 value = 0;
    NvU32 arrayIndex = 0;
    NvU32 frameNumber = 0;
    NvU32 delayVal = 0;
    char timeUnit[2];
    char * memPointer = NULL;
    NvU8 i;
    NvU8 count;
    char subAdd[8];

    FILE * file = fopen(filename, "r");
    if (!file) {
        LOG_ERR("%s: Failed to open file \"%s\"\n",__func__, filename);
        goto failed;
    }

    memset((void *)readLine, 0, MAX_STRING_SIZE);
    memset((void *)parsedLine, 0, MAX_STRING_SIZE);
    memset((void *)stringBuf, 0, MAX_STRING_SIZE);

    params->pixelOrder.uIntValue = NVMEDIA_RAW_PIXEL_ORDER_BGGR;  //default pixel order

    while (fgets(readLine, MAX_STRING_SIZE, file) != NULL) {
        deviceAddress = 0;
        address = 0;
        value = 0;
        arrayIndex = 0;
        frameNumber = 0;

        // Parse for comments ('#' symbol)
        memPointer = strchr(readLine, '#');
        if (memPointer != NULL) // Found comment so set it to line terminator
            *memPointer = '\0';
        strcpy(parsedLine, readLine);

        // Set process type
        if (isGroupRegister) {
            allCommands->commands[numCommands].processType = GROUP_REG;
        } else if (isInitialRegister) {
            allCommands->commands[numCommands].processType = PRESET_REG;
        } else {
            allCommands->commands[numCommands].processType = DEFAULT;
        }

        // Parse for delay (starts with ';' symbol)
        if (sscanf(parsedLine, "; Delay %u%s", &delayVal, timeUnit) == 2) {
            if (strcmp(timeUnit, "ms") == 0) {
                // Convert time to microseconds
                allCommands->commands[numCommands].delay = delayVal*1000;
            } else if (strcmp(timeUnit, "us") == 0) {
                allCommands->commands[numCommands].delay = delayVal;
            } else {
                LOG_ERR("%s: Unknown time unit found!\n", __func__);
                goto failed;
            }

#ifdef DEBUG
            LOG_DBG("%s: Delay %u microseconds\n", __func__,
                    allCommands->commands[numCommands].delay);
#endif

            allCommands->commands[numCommands].commandType = DELAY;
            numCommands++;
        } else if (strstr(parsedLine, "; I2C Err on") != NULL) {
            allCommands->commands[numCommands].commandType = I2C_ERR;
            allCommands->commands[numCommands].i2cErr = NVMEDIA_TRUE;
#ifdef DEBUG
            LOG_DBG("%s: Check for I2C errors\n", __func__);
#endif
            numCommands++;
        } else if (strstr(parsedLine, "; I2C Err off") != NULL) {
            allCommands->commands[numCommands].commandType = I2C_ERR;
            allCommands->commands[numCommands].i2cErr = NVMEDIA_FALSE;
#ifdef DEBUG
            LOG_DBG("%s: Do not check for I2C errors\n", __func__);
#endif
            numCommands++;
        } else if (sscanf(parsedLine, "; I2C %u",
                  (NvU32 *)&i2cDevice) == 1) {
            allCommands->commands[numCommands].commandType = I2C_DEVICE;
            allCommands->commands[numCommands].i2cDevice = i2cDevice;
#ifdef DEBUG
            LOG_DBG("%s: Open I2C Handle %u\n", __func__,
                    allCommands->commands[numCommands].i2cDevice);
#endif
            numCommands++;
        } else if (sscanf(parsedLine, "; Wait for frame %u",
                  &frameNumber) == 1) {
            if (frameNumber < 1) {
                LOG_WARN("%s: Invalid frame number. Using default of 1\n",
                         __func__);
                frameNumber = 1;
            }

            isGroupRegister = NVMEDIA_TRUE;
            allCommands->commands[numCommands].commandType = SECTION_START;
            allCommands->commands[numCommands].processType = GROUP_REG;
            allCommands->commands[numCommands].triggerFrame = frameNumber;
            numCommands++;
        } else if (sscanf(parsedLine, "; End frame %u regsiters",
                   &frameNumber) == 1) {
            isGroupRegister = NVMEDIA_FALSE;
            allCommands->commands[numCommands].commandType = SECTION_STOP;
            allCommands->commands[numCommands].processType = GROUP_REG;
            allCommands->commands[numCommands].triggerFrame = -1;
            numCommands++;
        } else if (strstr(parsedLine, "; Begin preset registers") != NULL) {
            isInitialRegister = NVMEDIA_TRUE;
            allCommands->commands[numCommands].commandType = SECTION_START;
            allCommands->commands[numCommands].processType = PRESET_REG;
            numCommands++;
        } else if (strstr(parsedLine, "; End preset registers") != NULL) {
            isInitialRegister = NVMEDIA_FALSE;
            allCommands->commands[numCommands].commandType = SECTION_STOP;
            allCommands->commands[numCommands].processType = PRESET_REG;
            numCommands++;
        } else if (sscanf(parsedLine,"%x %x %x", &deviceAddress, &address,
                         (NvU32 *)&value) == 3) {
            allCommands->commands[numCommands].deviceAddress = deviceAddress >> 1;

            // check subAdd 1 or 2 bytes
            sscanf(parsedLine,"%s %s", subAdd, subAdd);
            if (subAdd[2]!='\0') {
                allCommands->commands[numCommands].commandType = WRITE_REG_2;
                allCommands->commands[numCommands].buffer[arrayIndex] =
                    (NvU8)((address >> 8) & 0xFF);
                allCommands->commands[numCommands].buffer[++arrayIndex] =
                    (NvU8)(address & 0xFF);
            } else {
                allCommands->commands[numCommands].commandType = WRITE_REG_1;
                allCommands->commands[numCommands].buffer[arrayIndex] =
                    (NvU8)(address & 0xFF);
            }

            count = 0;
            for (i = 5; i < (MAX_STRING_SIZE-1); i++) {
                if ((parsedLine[i] == ' ') && (parsedLine[i+1] != '\0')) {
                    if ((sscanf((parsedLine+i+1),"%x", (NvU32 *)&value)) >=1) {
                        count++;
                        allCommands->commands[numCommands].buffer[++arrayIndex] =
                        (NvU8)(value & 0xFF);
                    }
                }
            }

            //save data length
            allCommands->commands[numCommands].dataLength = count;

#ifdef DEBUG
            if (allCommands->commands[numCommands].commandType == WRITE_REG_1) {
                LOG_DBG("%s: %02x %02x %02x\n", __func__,
                        allCommands->commands[numCommands].deviceAddress,
                        allCommands->commands[numCommands].buffer[0],
                        allCommands->commands[numCommands].buffer[1]);
            } else {
                LOG_DBG("%s: %02x %02x%02x %02x\n", __func__,
                        allCommands->commands[numCommands].deviceAddress,
                        allCommands->commands[numCommands].buffer[0],
                        allCommands->commands[numCommands].buffer[1],
                        allCommands->commands[numCommands].buffer[2]);
            }
#endif
            numCommands++;
        } else if (sscanf(parsedLine, "; Interface: %s",
                  stringBuf) == 1) {
            params->interface.isUsed = NVMEDIA_TRUE;
            strcpy(params->interface.stringValue, stringBuf);
        } else if (sscanf(parsedLine, "; Input Format: %s",
                  stringBuf) == 1) {
            params->inputFormat.isUsed = NVMEDIA_TRUE;
            strcpy(params->inputFormat.stringValue, stringBuf);
        } else if (sscanf(parsedLine, "; Surface Format: %s",
                  stringBuf) == 1) {
            params->surfaceFormat.isUsed = NVMEDIA_TRUE;
            strcpy(params->surfaceFormat.stringValue, stringBuf);
        } else if (sscanf(parsedLine, "; Resolution: %s",
                  stringBuf) == 1) {
            params->resolution.isUsed = NVMEDIA_TRUE;
            strcpy(params->resolution.stringValue, stringBuf);
        } else if (sscanf(parsedLine, "; CSI Lanes: %u",
                  &uIntBuf) == 1) {
            params->csiLanes.isUsed = NVMEDIA_TRUE;
            params->csiLanes.uIntValue = uIntBuf;
        } else if (sscanf(parsedLine, "; I2C Device: %d",
                  &intBuf) == 1) {
            params->i2cDevice.isUsed = NVMEDIA_TRUE;
            params->i2cDevice.intValue = intBuf;
        } else if (sscanf(parsedLine, "; Sensor Address: %x",
                  &uIntBuf) == 1) {
            params->sensorAddress.isUsed = NVMEDIA_TRUE;
            params->sensorAddress.uIntValue= uIntBuf >> 1;
        } else if (sscanf(parsedLine, "; Max9286 Address: %x",
                  &uIntBuf) == 1) {
            params->deserAddress.isUsed = NVMEDIA_TRUE;
            params->deserAddress.uIntValue = uIntBuf >> 1;
        } else if (sscanf(parsedLine, "; Deserializer Address: %x",
                  &uIntBuf) == 1) {
            params->deserAddress.isUsed = NVMEDIA_TRUE;
            params->deserAddress.uIntValue = uIntBuf >> 1;
        } else if (sscanf(parsedLine, "; Pixel Order: %s",
                  stringBuf) == 1) {
            params->pixelOrder.isUsed = NVMEDIA_TRUE;
            if ((!strncmp(stringBuf,"RGGB", 4)) || (!strncmp(stringBuf,"rggb", 4)))
                params->pixelOrder.uIntValue = NVMEDIA_RAW_PIXEL_ORDER_RGGB;
            else if ((!strncmp(stringBuf,"GRBG", 4)) || (!strncmp(stringBuf,"grbg", 4)))
                params->pixelOrder.uIntValue = NVMEDIA_RAW_PIXEL_ORDER_GRBG;
            else if ((!strncmp(stringBuf,"GBRG", 4)) || (!strncmp(stringBuf,"gbrg", 4)))
                params->pixelOrder.uIntValue = NVMEDIA_RAW_PIXEL_ORDER_GBRG;
            // else, by default, it is already set to NVMEDIA_RAW_PIXEL_ORDER_BGGR
        }

        memset((void *)readLine, 0, MAX_STRING_SIZE);
        memset((void *)parsedLine, 0, MAX_STRING_SIZE);
        memset((void *)stringBuf, 0, MAX_STRING_SIZE);
    }

    // Check for required parameters
    if (!params->interface.isUsed) {
        LOG_ERR("%s: Interface not specified!\n");
        goto failed;
    }
    if (!params->inputFormat.isUsed) {
        LOG_ERR("%s: Input Format not specified!\n");
        goto failed;
    }
    if (!params->resolution.isUsed) {
        LOG_ERR("%s: Resolution not specified!\n");
        goto failed;
    }
    if (!params->csiLanes.isUsed) {
        LOG_ERR("%s: CSI Lanes not specified!\n");
        goto failed;
    }
    if (!params->i2cDevice.isUsed) {
        LOG_ERR("%s: I2C Device not specified!\n");
        goto failed;
    }
    if (!params->sensorAddress.isUsed) {
        LOG_ERR("%s: Sensor Address not specified!\n");
        goto failed;
    }

    allCommands->numCommands = numCommands;
    if (file)
        fclose(file);
    return NVMEDIA_STATUS_OK;
failed:
    if (file)
        fclose(file);
    return NVMEDIA_STATUS_ERROR;
}
