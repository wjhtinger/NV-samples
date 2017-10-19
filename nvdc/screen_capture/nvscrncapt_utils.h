/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVSCRNCAPT_UTILS_H_
#define _NVSCRNCAPT_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

/** \hideinitializer \brief Max string size */
#define MAX_STRING_SIZE 256

/** \hideinitializer \brief Max Mempool Size (bytes) */
#define MAX_MEMPOOL_SIZE 150000000

/**
 * \hideinitializer
 * \brief The set of possible logging verbsoity levels
 */
enum LogLevel {
    LEVEL_ERR   = 0,
    LEVEL_WARN  = 1,
    LEVEL_INFO  = 2,
    LEVEL_DBG   = 3,
};

extern int currentLogLevel;

/**
 * \hideinitializer
 * \brief Logging Macros
 */
#define LogLevelMessage(logLevel, functionName, format, args...) do {\
            if(logLevel <= currentLogLevel) {\
                printf("nvscrncapt(%d): ", logLevel);\
                if (logLevel != LEVEL_INFO)\
                    printf("%s(%d): ", __func__, __LINE__);\
                printf(format, ##args);\
            }} while(0)

#define LogMessage(format, args...) printf(format, ##args);
#define LOG_DBG(format, args...)    LogLevelMessage(LEVEL_DBG, __FUNCTION__, format, ##args)
#define LOG_INFO(format, args...)   LogLevelMessage(LEVEL_INFO, __FUNCTION__, format, ##args)
#define LOG_WARN(format, args...)   LogLevelMessage(LEVEL_WARN, __FUNCTION__, format, ##args)
#define LOG_ERR(format, args...)    LogLevelMessage(LEVEL_ERR, __FUNCTION__, format, ##args)
#define LOG_MSG(format, args...)    LogMessage(format, ##args)

/**
 * \brief Defines structure to hold command line arguments
 */
typedef struct {
    /* Prefix of rgb file to save */
    char filePrefix[200];
    /* Log verbosity */
    int logLevel;
    /* Bitmask indicating heads to capture on */
    unsigned int headMask;
    /* Indicates if app shall pre-allocate memory */
    int preAllocateMemory;
} TestArgs;

int
ParseArgs (
    int argc,
    char *argv[],
    TestArgs *testArgs);

void PrintUsage (void);

#ifdef __cplusplus
}
#endif

#endif  /* _NVSCRNCAPT_UTILS_H_ */
