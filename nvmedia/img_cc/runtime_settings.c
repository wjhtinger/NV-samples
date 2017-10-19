/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "runtime_settings.h"
#include "capture.h"

#define MAX_COMMAND_LINE_SIZE 500

static NvMediaStatus
ParseCommandsFile(char *filename, RuntimeSettings **settings, NvU32 *numSettings)
{
    NvMediaStatus status;
    FILE *fp = NULL;
    RuntimeSettings *settingsTmp = NULL;
    RuntimeSettings *pSettings = NULL;
    NvU32 i;
    int k;
    char line[MAX_COMMAND_LINE_SIZE] = {0};
    int ch = 0;
    char *argument;

    fp = fopen(filename, "r");
    if(!fp) {
        LOG_ERR("%s: Failed to open commandline file\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    /* Count number of lines and reset file position */
    *numSettings = 0;
    while(!feof(fp)) {
        ch = fgetc(fp);
        if(ch == '\n')
            (*numSettings)++;
    }
    fseek(fp, 0, SEEK_SET);

    /* Allocate memory for settings */
    settingsTmp = calloc(1, sizeof(RuntimeSettings) * (*numSettings));
    if(!settingsTmp) {
        status = NVMEDIA_STATUS_OUT_OF_MEMORY;
        goto failed;
    }

    /* Populate settigns */
    for(i = 0; i < *numSettings; i++) {
        /* Get line from file */
        if(!fgets(line, MAX_COMMAND_LINE_SIZE, fp)) {
            LOG_ERR("%s: Failed to get line %d from file\n", __func__, i);
            goto failed;
        }

        pSettings = &settingsTmp[i];

        /* Create an empty argv for index 0 */
        pSettings->argc = 0;
        pSettings->argv[pSettings->argc] = calloc(1, 1);
        *pSettings->argv[pSettings->argc] = '\0';
        pSettings->argc++;

        /* Fill argc and argv for each line */
        argument = strtok(line, " ");
        while(argument != NULL) {
            pSettings->argv[pSettings->argc] = calloc(1, strlen(argument));
            strcpy(pSettings->argv[pSettings->argc], argument);
            (pSettings->argc)++;
            argument = strtok(NULL, " ");
        }

        /* Fill numFrames */
        for(k = 0; k < pSettings->argc; k++) {
            if(!strcmp(pSettings->argv[k], "-n")) {
                pSettings->numFrames = atoi(pSettings->argv[k + 1]);
                break;
            }
        }
    }

    fclose(fp);
    *settings = settingsTmp;
    return NVMEDIA_STATUS_OK;
failed:
    if(fp)
        fclose(fp);
    return status;
}

static NvU32
_RuntimeSettingsThreadFunc(void *data)
{
    NvRuntimeSettingsContext *runtimeCtx  =(NvRuntimeSettingsContext *)data;
    RuntimeSettings *settings;
    NvU32 i = 0, frameNum = 0;

    settings = &runtimeCtx->rtSettings[0];
    while(!(*runtimeCtx->quit)) {
        // Wait till required number of frames are captured
        frameNum += settings->numFrames;
        while(*runtimeCtx->currentFrame < frameNum) {
            usleep(1000);
            if(*runtimeCtx->quit)
                goto done;
        }

        i++;
        // Reset to 0 since its round-robin
        if(i == runtimeCtx->numRtSettings) {
            i = 0;
        }

        settings = &runtimeCtx->rtSettings[i];
        // Apply settings
        I2cProcessCommands(settings->cmds, I2C_WRITE, runtimeCtx->calParam->i2cDevice);
        runtimeCtx->currentRtSettings = i;
    }
done:
    runtimeCtx->exitedFlag = NVMEDIA_TRUE;
    return NVMEDIA_STATUS_OK;

}

NvMediaStatus
RuntimeSettingsInit(NvMainContext *mainCtx)
{
    NvRuntimeSettingsContext *runtimeCtx  = NULL;
    NvCaptureContext   *captureCtx = NULL;
    RuntimeSettings *settings = NULL;
    SensorInfo *sensorInfo = NULL;
    SensorProperties *properties = NULL;
    NvU32 i;
    NvMediaStatus status;

   /* Allocating runtime settings context */
    mainCtx->ctxs[RUNTIME_SETTINGS_ELEMENT]= malloc(sizeof(NvRuntimeSettingsContext));
    if (!mainCtx->ctxs[RUNTIME_SETTINGS_ELEMENT]){
        LOG_ERR("%s: Failed to allocate memory for runtime settings context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    runtimeCtx = mainCtx->ctxs[RUNTIME_SETTINGS_ELEMENT];
    memset(runtimeCtx, 0, sizeof(NvRuntimeSettingsContext));
    captureCtx = mainCtx->ctxs[CAPTURE_ELEMENT];
    sensorInfo = captureCtx->sensorInfo;

    /* Initialize context */
    runtimeCtx->quit = &mainCtx->quit;
    runtimeCtx->exitedFlag = NVMEDIA_TRUE;
    runtimeCtx->currentFrame =  &captureCtx->threadCtx[0].currentFrame;
    runtimeCtx->calParam = &captureCtx->calParams;

    if(!mainCtx->testArgs->rtSettings.isUsed)
        return NVMEDIA_STATUS_OK;

    /* Parse runtime commands file */
    status = ParseCommandsFile(mainCtx->testArgs->rtSettings.stringValue,
                               &runtimeCtx->rtSettings, &runtimeCtx->numRtSettings);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("Failed to parse runtime settings file\n");
        goto failed;
    }

    properties = malloc(sensorInfo->sizeOfSensorProperties);
    if(!properties) {
        status = NVMEDIA_STATUS_OUT_OF_MEMORY;
        goto failed;
    }

    for(i = 0; i < runtimeCtx->numRtSettings; i++) {
        settings = &runtimeCtx->rtSettings[i];
        memset(properties, 0, sensorInfo->sizeOfSensorProperties);

        /* Allocate memory for i2c commands */
        settings->cmds = calloc(1, sizeof(I2cCommands));

        /* Process command line */
        status = sensorInfo->ProcessCmdline(settings->argc, settings->argv, properties);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("Failed to process command line\n");
            goto failed;
        }

        /* Populate i2c commands */
        status = sensorInfo->CalibrateSensor(settings->cmds, &captureCtx->calParams, properties);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("Failed to get sensor settings\n");
            goto failed;
        }

        /* Create output filename */
        status = sensorInfo->AppendOutputFilename(settings->outputFileName, properties);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("Failed to create output filename\n");
            goto failed;
        }
    }

    free(properties);

    return NVMEDIA_STATUS_OK;
failed:
    if(properties)
        free(properties);

    RuntimeSettingsFini(mainCtx->ctxs[RUNTIME_SETTINGS_ELEMENT]);
    return status;
}

NvMediaStatus
RuntimeSettingsFini(NvMainContext *mainCtx)
{
    NvRuntimeSettingsContext *runtimeCtx  = NULL;
    RuntimeSettings *settings = NULL;
    NvU32 i = 0;
    int j = 0;
    NvMediaStatus status;

    if(!mainCtx)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    runtimeCtx = mainCtx->ctxs[RUNTIME_SETTINGS_ELEMENT];
    if(!runtimeCtx)
        return NVMEDIA_STATUS_OK;

    /* Wait for thread to exit */
    while(!runtimeCtx->exitedFlag) {
        LOG_DBG("%s: Waiting for runtime settings thread to quit\n", __func__);
    }
    *runtimeCtx->quit = NVMEDIA_TRUE;
    if(runtimeCtx->runtimeSettingsThread) {
        status = NvThreadDestroy(runtimeCtx->runtimeSettingsThread);
        if(status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy runtime settings thread\n",
                    __func__);
    }

    for(i = 0; i < runtimeCtx->numRtSettings; i++) {
        settings = &runtimeCtx->rtSettings[i];
        if(!settings)
            break;
        if(settings->cmds)
            free(settings->cmds);
        for (j = 0; j < settings->argc; j++) {
            if (settings->argv[j])
                free(settings->argv[j]);
        }
    }

    if(runtimeCtx->rtSettings)
        free(runtimeCtx->rtSettings);

    free(runtimeCtx);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
RuntimeSettingsProc(NvMainContext *mainCtx)
{
    NvRuntimeSettingsContext *runtimeCtx = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    runtimeCtx = mainCtx->ctxs[RUNTIME_SETTINGS_ELEMENT];

    if(!mainCtx->testArgs->rtSettings.isUsed)
        return NVMEDIA_STATUS_OK;

    /* Create runtime settings thread */
    runtimeCtx->exitedFlag = NVMEDIA_FALSE;
    status = NvThreadCreate(&runtimeCtx->runtimeSettingsThread,
                            &_RuntimeSettingsThreadFunc,
                            (void *)runtimeCtx,
                            NV_THREAD_PRIORITY_NORMAL);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create runtime settings thread\n",
                __func__);
        runtimeCtx->exitedFlag = NVMEDIA_TRUE;
        return status;
    }

    return NVMEDIA_STATUS_OK;
}
