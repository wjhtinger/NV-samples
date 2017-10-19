/*
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "log_utils.h"
#include "nvmedia_isc.h"
#include "isc_max9286.h"

#define REGISTER_ADDRESS_BYTES  1
#define REG_WRITE_BUFFER        32
#define MAX_NUM_GMSL_LINKS      4

#if !defined(__INTEGRITY)
#define MIN(a,b)            (((a) < (b)) ? (a) : (b))
#endif
#define GET_SIZE(x)         sizeof(x)
#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   &x[1]
#define SET_NEXT_BLOCK(x)   x += (x[0] + 1)

// each 4bit 0 or 1 --> 1bit 0 or 1. eg. 0x1101 --> 0xD
#define CAMMAP_4BITSTO_1BIT(a) \
               ((a & 0x01) + ((a >> 3) & 0x02) + ((a >> 6) & 0x04) + ((a >> 9) & 0x08))
// each 4bit 0/1/2/3 --> 2bit 0/1/2/3. eg. 0x3210 --> 0xE4
#define CAMMAP_4BITSTO_2BITS(a) \
               ((a & 0x03) + ((a >> 2) & 0x0c) + ((a >> 4) & 0x30) + ((a >> 6) & 0xc0))

unsigned char max9286_defaults[] = {
    2, 0x0A, 0xF0,  // Enable the forward control channels to make sure if they're disabled before starting a camera app
};

unsigned char max9286_enable_link_0[] = {
    2, 0x00, 0xE1  // Enable link 0
};

unsigned char max9286_enable_links_01[] = {
    2, 0x00, 0xE3  // Enable links 0 and 1
};

unsigned char max9286_enable_links_012[] = {
    2, 0x00, 0xE7  // Enable links 0, 1 and 2
};

unsigned char max9286_enable_links_0123[] = {
    2, 0x00, 0xEF  // Enable all
};

unsigned char max9286_set_pixel_info[] = {
    2, 0x12, 0xf7,  // 4-lane, YUV422 8-bit, double pixel per word
};

unsigned char max9286_disable_auto_ack[] = {
    2, 0x34, 0x36,  // Disable auto-ack
};

unsigned char max9286_reverse_channel_setting[] = {
    2, 0x3b, 0x1b, // Set amplitude first
    2, 0x3f, 0x62, // Enable to change
};

unsigned char max9286_reverse_channel_reset[] = {
    2, 0x3f, 0x62, // Enable to change
    2, 0x3b, 0x24, // Set amplitude first
};

unsigned char max9286_enable_auto_ack[] = {
    2, 0x34, 0xB6   // Reenable auto-ack
};

unsigned char max9286_set_csi_timing[] = {
    2, 0x19, 0xA3  // Bug 1608443 for fixing CSI TEOT timing failure
};

unsigned char max9286_swap_data_lanes[] = {
    2, 0x14, 0xE1,  // Swap data lanes D0 and D1 for e2580 platform
};

unsigned char max9286_reverse_channel_enable[] = {
    2, 0x3f, 0x4F,   // Enable Rev channel;
};

unsigned char max9286_reverse_channel_ampl_l[] = {
    2, 0x3b, 0x1E,   // Rev channel pulse length;
};

unsigned char max9286_reverse_channel_ampl_h[] = {
    2, 0x3b, 0x19,  // Increase Rev channel amplitude to 170mv;
};

unsigned char max9286_disable_ovlp_window[] = {
    2, 0x63, 0x00,
    2, 0x64, 0x00,
};

unsigned char max9286_enable_hibw[] = {
    2, 0x1C, 0x06,  // HIBW =1
};

typedef struct {
    NvMediaISCSupportFunctions *funcs;
    ContextMAX9286 ctx;
} _DriverHandle;

static unsigned char
max9286ErrRegAddress[ISC_MAX9286_NUM_ERR_REG] = {
    MAX9286_REG_DATA_OK,
    MAX9286_REG_VIDEO_LINK,
    MAX9286_REG_VSYNC_DET,
    MAX9286_REG_LINE_BUF_ERR,
    MAX9286_REG_FSYNC_LOCK,
};

static NvMediaStatus
EnableLinks(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int links,
    NvMediaBool enable)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaStatus status;

    if(!handle || !transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    // Only 4 bits are used to represent 4 links
    if(links > 0xF)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    // Invert bits if disabling links
    if(!enable)
        links = ~links;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = MAX9286_REG_LINK_EN; // Register address

    status = funcs->Read(
        transaction,            // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        data,                   // regData
        1,                      // dataLength
        &data[1]);              // data
    if(status != NVMEDIA_STATUS_OK)
        return status;

    // Clear last 4 bits
    data[1] &= 0xF0;

    // Set last 4 bits
    data[1] |=  links & 0xF;

    status = funcs->Write(
        transaction, // transaction
        2,           // dataLength
        data);       // data
    if(status != NVMEDIA_STATUS_OK)
        return status;

    // Update context with latest status of links
    ((_DriverHandle *)handle)->ctx.gmslLinks = data[1] & 0x0F;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DriverCreate(
    NvMediaISCDriverHandle **handle,
    NvMediaISCSupportFunctions *supportFunctions,
    void *clientContext)
{
    _DriverHandle *driverHandle;

    if(!handle || !supportFunctions)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    driverHandle = calloc(1, sizeof(_DriverHandle));
    if(!driverHandle)
        return NVMEDIA_STATUS_OUT_OF_MEMORY;

    driverHandle->funcs = supportFunctions;

    if(clientContext) {
        memcpy(&driverHandle->ctx, clientContext, sizeof(ContextMAX9286));
    }

    *handle = (NvMediaISCDriverHandle *)driverHandle;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DriverDestroy(
    NvMediaISCDriverHandle *handle)
{
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    free(handle);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
CheckPresence(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2] = {0};
    NvMediaStatus status;

    if(!handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    funcs = ((_DriverHandle *)handle)->funcs;

    // Read the chip revision number
    data[0] = MAX9286_REG_CHIP_REVISION;

    status = funcs->Read(
        transaction,            // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        data,                   // regData
        1,                      // dataLength
        &data[1]);              // data
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    ((_DriverHandle *)handle)->ctx.revision = data[1] & MAX9286_CHIP_REVISION_MASK;

    if(((_DriverHandle *)handle)->ctx.disableBackChannelCtrl) {
        if(((_DriverHandle *)handle)->ctx.revision != MAX9286_CHIP_REVISION_8) {
            return NVMEDIA_STATUS_NOT_SUPPORTED;
        } else {
            return NVMEDIA_STATUS_OK;
        }
    } else {
        return NVMEDIA_STATUS_OK;
    }
}

static NvMediaStatus
CheckLink(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int instanceNumber)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char address;
    NvMediaStatus status;
    unsigned char data = 0;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(instanceNumber > 4)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    address = MAX9286_REG_CONFIG_LINK_DETECT;

    status = funcs->Read(
        transaction,
        1,
        &address,
        1,
        &data);

    if(status != NVMEDIA_STATUS_OK)
        return status;

    if(!((data >> (4 + instanceNumber)) & 1))
        return NVMEDIA_STATUS_ERROR;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetLinkStatus(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int instanceNumber)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char address;
    NvMediaStatus status;
    unsigned char data = 0;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(instanceNumber > 4)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    address = MAX9286_REG_CONFIG_LINK_DETECT;

    status = funcs->Read(
        transaction,
        1,
        &address,
        1,
        &data);

    if(status != NVMEDIA_STATUS_OK)
        return status;

    if(!((data >> instanceNumber) & 1))
        return NVMEDIA_STATUS_ERROR;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
EnableLink(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int instanceNumber,
    NvMediaBool enable)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    // Disable for now
    if(0 && enable) {
        // Enable link
        data[0] = 0; // Register address

        status = funcs->Read(
            transaction,            // transaction
            REGISTER_ADDRESS_BYTES, // regLength
            data,                   // regData
            1,                      // dataLength
            &data[1]);              // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        // Enable link for a specific instance
        data[1] &= 0xF0;
        data[1] |= 1 << instanceNumber;

        status = funcs->Write(
            transaction, // transaction
            2,           // dataLength
            data);       // data
        if(status != NVMEDIA_STATUS_OK)
            return status;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetIgnoreError(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        bool enable
)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;
    if(!funcs)
        return NVMEDIA_STATUS_NOT_SUPPORTED;

    if(enable) {
        /* set err threshold to be maximum */
        data[0] = 0x10;
        data[1] = 0xff;
        status = funcs->Write(
            transaction,                       // transaction
            2,                                 // dataLength
            &data[0]);                         // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        /* set error reset automatically */
        data[0] = 0x0f;
        data[1] = 0x2b;
        status = funcs->Write(
            transaction,                       // transaction
            2,                                 // dataLength
            &data[0]);                         // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

    } else { /* unmask error */
        /* read operation resets sync error count before enabling error */
        data[0] = 0x5e;
        status = funcs->Read(
            transaction,            // transaction
            REGISTER_ADDRESS_BYTES, // regLength
            data,                   // regData
            1,                      // dataLength
            &data[1]);              // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        /* disable automatically clearing error */
        data[0] = 0x0f;
        data[1] = 0x0b;
        status = funcs->Write(
            transaction,                       // transaction
            2,                                 // dataLength
            &data[0]);                         // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        /* set error threshold to be 0 to detect any error from MAX9286 */
        data[0] = 0x10;
        data[1] = 0x00;
        status = funcs->Write(
            transaction,                       // transaction
            2,                                 // dataLength
            &data[0]);                         // data
        if(status != NVMEDIA_STATUS_OK)
            return status;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetPixelInfo(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    ConfigurePixelInfoMAX9286 *pixelInfo)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    max9286_set_pixel_info[2] =  (unsigned char)pixelInfo->byte;

    status = funcs->Write(
        transaction,                  // transaction
        max9286_set_pixel_info[0],    // dataLength
        &max9286_set_pixel_info[1]);  // data

    if(status != NVMEDIA_STATUS_OK)
            return status;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetSyncMode(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    SetFsyncModeMAX9286 mode,
    int margin)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status;
    unsigned char set_val[4];
    unsigned char max9286_set_sync_mode[] = {
        2, 0x01, 0x21,  // FSYNC settings
    };

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    switch (mode) {
    case ISC_SET_FSYNC_MAX9286_FSYNC_MANUAL:
        max9286_set_sync_mode[2] = 0x00;
        status = funcs->Write(
           transaction,                    // transaction
           max9286_set_sync_mode[0],       // dataLength
           &max9286_set_sync_mode[1]);     // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        //set number of pclk of fsync
        set_val[0] = MAX9286_REG_FSYNC_PERIOD;
        set_val[1] = margin & 0xff;
        set_val[2] = (margin >> 8) & 0xff;
        set_val[3] = (margin >> 16) & 0xff;
        status = funcs->Write(
            transaction,         // transaction
            4,                   // dataLength
            &set_val[0]);        // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        return NVMEDIA_STATUS_OK;
    case ISC_SET_FSYNC_MAX9286_FSYNC_AUTO:
        mode = ISC_SET_FSYNC_MAX9286_INTERNAL_PIN_HIGH_Z;
        max9286_set_sync_mode[2] &= 0x3c;
        max9286_set_sync_mode[2] |= ((mode << 6) & 0xc0) | 0x2;

        set_val[0] = MAX9286_REG_SET_SYNC_MARGIN;
        set_val[1] = 1; /*default margin */
        status = funcs->Write(
            transaction,      // transaction
            2,                // dataLength
            &set_val[0]);     // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        status = funcs->Write(
            transaction,                 // transaction
            max9286_set_sync_mode[0],    // dataLength
            &max9286_set_sync_mode[1]);  // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        return status;
    case ISC_SET_FSYNC_MAX9286_FSYNC_SEMI_AUTO:
        mode = ISC_SET_FSYNC_MAX9286_INTERNAL_PIN_HIGH_Z;
        max9286_set_sync_mode[2] &= 0x3c;
        max9286_set_sync_mode[2] |= ((mode << 6) & 0xc0) | 0x1;
        set_val[0] = MAX9286_REG_SET_SYNC_MARGIN;
        set_val[1] = (margin > 0)? (unsigned char)(margin & 0xf) :
                                   (unsigned char)((1 << 4) | (margin & 0xf));
        status = funcs->Write(
            transaction,      // transaction
            2,                // dataLength
            &set_val[0]);     // data
        if(status != NVMEDIA_STATUS_OK)
            return status;
        status = funcs->Write(
            transaction,                 // transaction
            max9286_set_sync_mode[0],    // dataLength
            &max9286_set_sync_mode[1]);  // data

        if(status != NVMEDIA_STATUS_OK)
            return status;
        return status;
    case ISC_SET_FSYNC_MAX9286_DISABLE_SYNC:
        max9286_set_sync_mode[2] = 0xc0;
        set_val[0] = MAX9286_REG_SET_SYNC_MARGIN;
        set_val[1] = 1; /* default margin */
        status = funcs->Write(
            transaction,      // transaction
            2,                // dataLength
            &set_val[0]);     // data

        if(status != NVMEDIA_STATUS_OK)
            return status;
        return funcs->Write(
            transaction,                 // transaction
            max9286_set_sync_mode[0],    // dataLength
            &max9286_set_sync_mode[1]);  // data
    case ISC_SET_FSYNC_MAX9286_EXTERNAL_FROM_ECU:
        mode = ISC_SET_FSYNC_MAX9286_EXTERNAL_FROM_ECU;
        max9286_set_sync_mode[2] &= 0x3c;
        max9286_set_sync_mode[2] |= ((mode << 6) & 0xc0);
        set_val[0] = MAX9286_REG_SET_SYNC_MARGIN;
        set_val[1] = (margin > 0)? (unsigned char)(margin & 0xf) :
                                   (unsigned char)((1 << 4) | (margin & 0xf));
        status = funcs->Write(
            transaction,      // transaction
            2,                // dataLength
            &set_val[0]);     // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        return funcs->Write(
            transaction,                 // transaction
            max9286_set_sync_mode[0],    // dataLength
            &max9286_set_sync_mode[1]);  // data
    default:
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }
}

static NvMediaStatus
WriteArray(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int arrayByteLength,
    unsigned char *arrayData)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    while(arrayByteLength) {
        status = funcs->Write(
            transaction,                 // transaction
            GET_BLOCK_LENGTH(arrayData), // dataLength
            GET_BLOCK_DATA(arrayData));  // data
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: MAX9286: error: wri2c   0x%.2X    0x%.2X\n", __func__,
                (unsigned int)arrayData[1],
                (unsigned int)arrayData[2]);
        }
        /* This SER-DES pair needs 20SCLK clocks or more timing for next I2C command so we set 100 us with margin */
        usleep(100);

        arrayByteLength -= GET_BLOCK_LENGTH(arrayData) + 1;
        SET_NEXT_BLOCK(arrayData);
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetDefaults(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaISCSupportFunctions *funcs;
    unsigned char offset[1];
    unsigned char dataBuff[1];

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    offset[0] = 0x00;

    status = funcs->Read(
        transaction,    // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        offset,   // regData
        1,     // dataLength
        dataBuff);      // data

    if(status != NVMEDIA_STATUS_OK)
        return status;

    status = WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_defaults),
                max9286_defaults);
    if(status != NVMEDIA_STATUS_OK)
        return status;

    usleep(3000);

    return status;
}

static NvMediaStatus
SetReverseChannelAmplitude(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int amplitude,
        unsigned int transitionTime)
{
    unsigned char rev_amp = 0;
    unsigned char rev_trf = 0;

    if(amplitude > MAX9286_MAX_REVERSE_CHANNEL_AMP)
        return NVMEDIA_STATUS_NOT_SUPPORTED;

    if((amplitude > MAX9286_REVERSE_CHANNEL_BOOT_AMP) &&
        (amplitude <= MAX9286_MAX_REVERSE_CHANNEL_AMP)) {
        rev_amp = (((amplitude - (MAX9286_REVERSE_CHANNEL_BOOT_AMP +
                                     MAX9286_MIN_REVERSE_CHANNEL_AMP)) /
                       MAX9286_REVERSE_CHANNEL_STEP_SIZE) << MAX9286_REVERSE_CHANNEL_AMP_REG_SHIFT) |
                   (1 << MAX9286_REVERSE_CHANNEL_AMP_BOOST_REG_SHIFT);
        max9286_reverse_channel_setting[2] &= 0xf0;
        max9286_reverse_channel_setting[2] |= (rev_amp << 0);

    } else if(amplitude >= 30) {
        rev_amp = (((amplitude - MAX9286_MIN_REVERSE_CHANNEL_AMP) /
                       MAX9286_REVERSE_CHANNEL_STEP_SIZE) << MAX9286_REVERSE_CHANNEL_AMP_REG_SHIFT);
        max9286_reverse_channel_setting[2] &= 0xf0;
        max9286_reverse_channel_setting[2] |= (rev_amp << 0);
    }

    if((transitionTime >= 100) && (transitionTime <= 400)) {
        rev_trf = (transitionTime / 100) - 1;
        max9286_reverse_channel_setting[2] &= 0xcf;
        max9286_reverse_channel_setting[2] |= (rev_trf << 4);
    }

    return WriteArray(handle,
                transaction,
                GET_SIZE(max9286_reverse_channel_setting),
                max9286_reverse_channel_setting);
}

static NvMediaStatus
EnableControlChannels(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        NvMediaBool enable)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char max9286_enable_ctrl_channel[2] = {
        0x0a, 0xff,   // Enable forward & reverse control channels
    };
    NvMediaStatus status;

    if(!handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    funcs = ((_DriverHandle *)handle)->funcs;

    if(!enable) {
        max9286_enable_ctrl_channel[1] = 0x00;
    }

    status = funcs->Write(
        transaction,
        2,
        max9286_enable_ctrl_channel);
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    // 3ms needed by MAX9286 to lock the link
    usleep(3000);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
ControlReverseChannel(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int id,
        bool enable,
        bool individualControl)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(id > 4)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = MAX9286_REG_CONTROL_CHANNEL;

    status = funcs->Read(
        transaction,
        1,
        &data[0],
        1,
        &data[1]);

    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    if(individualControl && enable)
        data[1] |= (1 << id);
    else if(individualControl && !enable)
        data[1] &= ~(1 << id);
    else if(!individualControl && enable)
        data[1] |= 0x0f;
    else
        data[1] &= 0xf0;

    return funcs->Write(
        transaction,
        2,
        data);
}

static NvMediaStatus
PowerupReverseChannelTransmitter(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        NvMediaBool powerup)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status;
    unsigned char max9286_power_up_reverse_channel_transmitter[2] = {
        0x43, 0xb0,    // power up reverse channel transmitter
    };

    if(!handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    // The new aggregator should power up the reverse channel transmitter before
    // programming the serializer and the camera sensor
    if(((_DriverHandle *)handle)->ctx.revision == MAX9286_CHIP_REVISION_8) {
        funcs = ((_DriverHandle *)handle)->funcs;

        // Invert a bit if powering down reverse channel transmitter
        if(!powerup) {
            max9286_power_up_reverse_channel_transmitter[1] &= ~MAX9286_REVERSE_CHANNEL_TRANSMITTER;
        }

        status = funcs->Write(
            transaction, // transaction
            2,           // dataLength
            max9286_power_up_reverse_channel_transmitter);
        if(status != NVMEDIA_STATUS_OK) {
            return status;
        }

        // Wait for reverse channel transmitter stable
        usleep(2000);
    } else if(((_DriverHandle *)handle)->ctx.revision == 0) {
        LOG_ERR("%s: CheckPresence() should be called before this function called\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    } else {
        return NVMEDIA_STATUS_OK;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
CameraMapping(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    CamMap *camMap,
    NvMediaBool enable)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaStatus status;

    if(!handle || !transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    if(enable) {
        // set video link enable
        status = EnableLinks (handle, transaction,
                 CAMMAP_4BITSTO_1BIT(camMap->enable), NVMEDIA_TRUE);
        if(status != NVMEDIA_STATUS_OK)
            return status;

        // set video link mask
        data[0] = MAX9286_REG_MASK_LINK; // Register address
        status = funcs->Read(
            transaction,            // transaction
            REGISTER_ADDRESS_BYTES, // regLength
            data,                   // regData
            1,                      // dataLength
            &data[1]);              // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        data[1] &= 0xC0; // disable auto mask and auto comeback
        data[1] |= CAMMAP_4BITSTO_1BIT(camMap->mask);
        status = funcs->Write(
            transaction, // transaction
            2,           // dataLength
            data);       // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        // set csi out mapping
        data[0] = MAX9286_REG_LINK_OUT_ORDER; // Register addres
        data[1] = CAMMAP_4BITSTO_2BITS(camMap->csiOut);
        status = funcs->Write(
            transaction, // transaction
            2,           // dataLength
            data);       // data
    } else {
        // clear video link mask
        data[0] = MAX9286_REG_MASK_LINK;
        data[1] = 0;
        status = funcs->Write(
            transaction, // transaction
            2,           // dataLength
            data);       // data
        if(status != NVMEDIA_STATUS_OK)
            return status;
        // clear link order
        data[0] = MAX9286_REG_LINK_OUT_ORDER;
        data[1] = 0xe4; /* default value */
        status = funcs->Write(
            transaction, // transaction
            2,           // dataLength
            data);       // data
    }

    return status;
}

static NvMediaStatus
EnableVirtualChannels(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaStatus status;
    unsigned char max9286_enable_vc[2] = {
        0x15, 0x90    // Select W x N*H for combined camera line format for CSI2 output & set VC according to the link number
    };

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = max9286_enable_vc[0]; // Register address
    status = funcs->Read(
        transaction,            // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        data,                   // regData
        1,                      // dataLength
        &data[1]);              // data
    if(status != NVMEDIA_STATUS_OK)
        return status;

    // Enable virtual channels
    max9286_enable_vc[1] |= (data[1] & 0x0F);

    return funcs->Write(
        transaction,
        2,
        &max9286_enable_vc[0]);
}

static NvMediaStatus
EnableCSI(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    bool enable)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaBool enSwapBitOrder;
    unsigned char max9286_enable_csi_out[] = {
        2, 0x15, 0x0B   // Enable CSI out
    };

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;
    enSwapBitOrder = ((_DriverHandle *)handle)->ctx.enSwapBitOrder;

    if(enable) {
        /* csi_timing doesn't need to be programmed for disabling CSI */
        status = funcs->Write(
            transaction,
            2,
            &max9286_set_csi_timing[1]);
        if(status != NVMEDIA_STATUS_OK)
            return status;

        data[0] = max9286_enable_csi_out[1]; // Register address
        status = funcs->Read(
            transaction,            // transaction
            REGISTER_ADDRESS_BYTES, // regLength
            data,                   // regData
            1,                      // dataLength
            &data[1]);              // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        max9286_enable_csi_out[2] |= (data[1] & 0xF0);
        max9286_enable_csi_out[2] |= (1 << 3) | (enSwapBitOrder << 2);

        status = funcs->Write(
            transaction,
            2,
            &max9286_enable_csi_out[1]);

        if(status != NVMEDIA_STATUS_OK)
            return status;

        /* revert ignoring error */
        status = SetIgnoreError(
            handle,
            transaction,
            false);
    } else {
        /* disable csi out */
        data[0] = max9286_enable_csi_out[1]; // Register address
        status = funcs->Read(
            transaction,            // transaction
            REGISTER_ADDRESS_BYTES, // regLength
            data,                   // regData
            1,                      // dataLength
            &data[1]);              // data
        if(status != NVMEDIA_STATUS_OK)
            return status;

        max9286_enable_csi_out[2] |= (data[1] & 0xF0);
        max9286_enable_csi_out[2] &= ~(1 << 3);

        status = funcs->Write(
            transaction,
            2,
            &max9286_enable_csi_out[1]);

        if(status != NVMEDIA_STATUS_OK)
            return status;

        /* enable ignoring error while csi out is off */
        status = SetIgnoreError(
            handle,
            transaction,
            true);
    }

    return status;
}

static NvMediaStatus
SetDeviceConfig(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int enumeratedDeviceConfig)
{
    NvMediaISCSupportFunctions *funcs;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;
    if(!funcs)
        return NVMEDIA_STATUS_NOT_SUPPORTED;

    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_MAX9286_DEFAULT:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_defaults),
                max9286_defaults);
        case ISC_CONFIG_MAX9286_DISABLE_AUTO_ACK:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_disable_auto_ack),
                max9286_disable_auto_ack);
        case ISC_CONFIG_MAX9286_ENABLE_AUTO_ACK:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_enable_auto_ack),
                max9286_enable_auto_ack);
        case ISC_CONFIG_MAX9286_ENABLE_CSI_OUT:
            return EnableCSI(handle,
                transaction,
                true);
        case ISC_CONFIG_MAX9286_ENABLE_LINK_0:
            return EnableLinks(
                handle,
                transaction,
                MAX9286_GMSL_LINK(0),
                NVMEDIA_TRUE);
        case ISC_CONFIG_MAX9286_ENABLE_LINKS_01:
            return EnableLinks(
                handle,
                transaction,
                MAX9286_GMSL_LINK(0) | MAX9286_GMSL_LINK(1),
                NVMEDIA_TRUE);
        case ISC_CONFIG_MAX9286_ENABLE_LINKS_012:
            return EnableLinks(
                handle,
                transaction,
                MAX9286_GMSL_LINK(0) | MAX9286_GMSL_LINK(1) |
                MAX9286_GMSL_LINK(2),
                NVMEDIA_TRUE);
        case ISC_CONFIG_MAX9286_ENABLE_LINKS_0123:
            return EnableLinks(
                handle,
                transaction,
                MAX9286_GMSL_LINK(0) | MAX9286_GMSL_LINK(1) |
                MAX9286_GMSL_LINK(2) | MAX9286_GMSL_LINK(3),
                NVMEDIA_TRUE);
        case ISC_CONFIG_MAX9286_REVERSE_CHANNEL_SETTING:
            return SetReverseChannelAmplitude(
                handle,
                transaction,
                ((_DriverHandle *)handle)->ctx.reverseChannelAmp,
                ((_DriverHandle *)handle)->ctx.reverseChannelTrf);
        case ISC_CONFIG_MAX9286_ENABLE_REVERSE_CHANNEL_0123:
            return ControlReverseChannel(
                handle,
                transaction,
                0,
                true,
                false);
        case ISC_CONFIG_MAX9286_DISABLE_REVERSE_CHANNEL_0123:
            return ControlReverseChannel(
                handle,
                transaction,
                0,
                false,
                false);
        case ISC_CONFIG_MAX9286_DISABLE_ALL_CONTROL_CHANNEL:
            return EnableControlChannels(
                handle,
                transaction,
                NVMEDIA_FALSE);
        case ISC_CONFIG_MAX9286_REVERSE_CHANNEL_RESET:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_reverse_channel_reset),
                max9286_reverse_channel_reset);
        case ISC_CONFIG_MAX9286_POWER_UP_REVERSE_CHANNEL_TRANSMITTER:
            return PowerupReverseChannelTransmitter(
                handle,
                transaction,
                NVMEDIA_TRUE);
        case ISC_CONFIG_MAX9286_SWAP_DATA_LANES:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_swap_data_lanes),
                max9286_swap_data_lanes);
        case ISC_CONFIG_MAX9286_CAMERA_MAPPING:
            return CameraMapping(
                handle,
                transaction,
                &((_DriverHandle *)handle)->ctx.camMap, NVMEDIA_TRUE);
        case ISC_CONFIG_MAX9286_SWAP_12BIT_ORDER:
            ((_DriverHandle *)handle)->ctx.enSwapBitOrder = NVMEDIA_TRUE;
            return NVMEDIA_STATUS_OK;
        case ISC_CONFIG_MAX9286_REVERSE_CHANNEL_ENABLE:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_reverse_channel_enable),
                max9286_reverse_channel_enable);
        case ISC_CONFIG_MAX9286_REVERSE_CHANNEL_AMPL_L:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_reverse_channel_ampl_l),
                max9286_reverse_channel_ampl_l);
        case ISC_CONFIG_MAX9286_REVERSE_CHANNEL_AMPL_H:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_reverse_channel_ampl_h),
                max9286_reverse_channel_ampl_h);
        case ISC_CONFIG_MAX9286_DISABLE_CSI_OUT:
            return EnableCSI(handle,
                transaction,
                false);
        case ISC_CONFIG_MAX9286_DISABLE_OVLP_WINDOW:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9286_disable_ovlp_window),
                max9286_disable_ovlp_window);
        case ISC_CONFIG_MAX9286_ENABLE_HIBW:
            /* enable to ignore error, changing bws value causes error */
            if(SetIgnoreError(
                handle,
                transaction,
                true) != NVMEDIA_STATUS_OK)
                return NVMEDIA_STATUS_ERROR;
            /* set bws */
            if(funcs->Write(
                transaction,
                max9286_enable_hibw[0],
                &max9286_enable_hibw[1]) != NVMEDIA_STATUS_OK)
                return NVMEDIA_STATUS_ERROR;
            /* wait for lock again after changing bws */
            usleep(6000);
            /* revert ingnoring error */
            if(SetIgnoreError(
                handle,
                transaction,
                false) != NVMEDIA_STATUS_OK)
                return NVMEDIA_STATUS_ERROR;
            return NVMEDIA_STATUS_OK;
        case ISC_CONFIG_MAX9286_ENABLE_VC:
            return EnableVirtualChannels(
                handle,
                transaction);
        default:
            break;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetTemperature(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    float *temperature)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaStatus
ReadRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int registerNum,
    unsigned int dataLength,
    unsigned char *dataBuff)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char registerData[REGISTER_ADDRESS_BYTES];
    NvMediaStatus status;

    if(!handle || !dataBuff)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    registerData[0] = registerNum & 0xFF;
    status = funcs->Read(
        transaction,    // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        registerData,   // regData
        dataLength,     // dataLength
        dataBuff);      // data

    return status;
}

static NvMediaStatus
WriteRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int registerNum,
    unsigned int dataLength,
    unsigned char *dataBuff)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[REGISTER_ADDRESS_BYTES + REG_WRITE_BUFFER];
    NvMediaStatus status;

    if(!handle || !dataBuff)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = registerNum & 0xFF;
    memcpy(&data[1], dataBuff, MIN(REG_WRITE_BUFFER, dataLength));

    status = funcs->Write(
        transaction,                         // transaction
        dataLength + REGISTER_ADDRESS_BYTES, // dataLength
        data);                               // data

    return status;
}

static NvMediaStatus
WriteParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        unsigned int parameterSize,
        void *parameter)
{
    WriteParametersParamMAX9286 *param = parameter;

    if (!param)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    switch(parameterType) {
        case ISC_WRITE_PARAM_CMD_MAX9286_ENABLE_REVERSE_CHANNEL:
            if(parameterSize != sizeof(param->EnableReverseChannel))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return ControlReverseChannel(
                handle,
                transaction,
                param->EnableReverseChannel.id,
                true,
                true);
        case ISC_WRITE_PARAM_CMD_MAX9286_DISABLE_REVERSE_CHANNEL:
            if(parameterSize != sizeof(param->DisableReverseChannel))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return ControlReverseChannel(
                handle,
                transaction,
                param->DisableReverseChannel.id,
                false,
                true);
        case ISC_WRITE_PARAM_CMD_MAX9286_SET_PIXEL_INFO:
            if(parameterSize != sizeof(param->SetPixelInfo))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetPixelInfo(
                handle,
                transaction,
                param->SetPixelInfo.pixelInfo);
        case ISC_WRITE_PARAM_CMD_MAX9286_SET_SYNC_MODE:
            if(parameterSize != sizeof(param->SetFsyncMode))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetSyncMode(
                handle,
                transaction,
                param->SetFsyncMode.syncMode,
                param->SetFsyncMode.k_val);
        default:
            break;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DumpRegisters(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaStatus
GetErrorStatus (
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int parameterSize,
    void *parameter)
{
    NvMediaStatus status;
    unsigned char regValues[ISC_MAX9286_NUM_ERR_REG] = {0};
    unsigned char gmslLinks;
    unsigned int i;
    ErrorStatusMAX9286 *errorStatus;
    NvMediaISCSupportFunctions *funcs;

    if(!handle || !transaction || !parameter)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(parameterSize != sizeof(ErrorStatusMAX9286))
        return NVMEDIA_STATUS_BAD_PARAMETER;

    errorStatus = (ErrorStatusMAX9286 *)parameter;
    funcs = ((_DriverHandle *)handle)->funcs;
    gmslLinks = ((_DriverHandle *)handle)->ctx.gmslLinks;

    memset(errorStatus, 0, sizeof(ErrorStatusMAX9286));

    for(i = 0; i < ISC_MAX9286_NUM_ERR_REG; i++) {
        status = funcs->Read(
            transaction,
            1,
            &max9286ErrRegAddress[i],
            1,
            &regValues[i]);
        if(status != NVMEDIA_STATUS_OK) {
            return status;
        }
    }

    // Check for data activity
    for(i = 0; i < MAX_NUM_GMSL_LINKS; i++) {
        if(MAX9286_GMSL_LINK(i) & gmslLinks) {
            if(!(regValues[ISC_MAX9286_REG_DATA_OK] & MAX9286_GMSL_LINK(i))) {
                errorStatus->link |= MAX9286_GMSL_LINK(i);
                errorStatus->failureType = ISC_MAX9286_NO_DATA_ACTIVITY;
            }
        }
    }
    if(errorStatus->failureType != ISC_MAX9286_NO_ERROR)
        goto done;

    // Check for video link failure
    for(i = 0; i < MAX_NUM_GMSL_LINKS; i++) {
        if(MAX9286_GMSL_LINK(i) & gmslLinks) {
            if(!(regValues[ISC_MAX9286_REG_VIDEO_LINK] & MAX9286_GMSL_LINK(i))) {
                errorStatus->link |= MAX9286_GMSL_LINK(i);
                errorStatus->failureType = ISC_MAX9286_VIDEO_LINK_ERROR;
            }
        }
    }
    if(errorStatus->failureType != ISC_MAX9286_NO_ERROR)
        goto done;

    // Check for vsync detection
    for(i = 0; i < MAX_NUM_GMSL_LINKS; i++) {
        if(MAX9286_GMSL_LINK(i) & gmslLinks) {
            if(!(regValues[ISC_MAX9286_REG_VSYNC_DET] & MAX9286_GMSL_LINK(i))) {
                errorStatus->link |= MAX9286_GMSL_LINK(i);
                errorStatus->failureType = ISC_MAX9286_VSYNC_DETECT_FAILURE;
            }
        }
    }
    if(errorStatus->failureType != ISC_MAX9286_NO_ERROR)
        goto done;

    // Check for FSYNC loss of lock
    if(regValues[ISC_MAX9286_REG_FSYNC_LOCK] & MAX9286_FSYNC_LOSS_OF_LOCK) {
        errorStatus->failureType = ISC_MAX9286_FSYNC_LOSS_OF_LOCK;
        for(i = 0; i < MAX_NUM_GMSL_LINKS; i++) {
            if(MAX9286_GMSL_LINK(i) & gmslLinks) {
                errorStatus->link |= MAX9286_GMSL_LINK(i);
            }
        }
    }
    if(errorStatus->failureType != ISC_MAX9286_NO_ERROR)
        goto done;

    // Check for line length error
    for(i = 0; i < MAX_NUM_GMSL_LINKS; i++) {
        if(MAX9286_GMSL_LINK(i) & gmslLinks) {
            if(regValues[ISC_MAX9286_REG_LINE_BUF_ERR] & MAX9286_GMSL_LINK(i)) {
                errorStatus->link |= MAX9286_GMSL_LINK(i);
                errorStatus->failureType = ISC_MAX9286_LINE_LENGTH_ERROR;
            }
        }
    }
    if(errorStatus->failureType != ISC_MAX9286_NO_ERROR)
        goto done;

    // Check for line buffer overflow
    for(i = 0; i < MAX_NUM_GMSL_LINKS; i++) {
        if(MAX9286_GMSL_LINK(i) & gmslLinks) {
            if((regValues[ISC_MAX9286_REG_LINE_BUF_ERR] >> 4) & MAX9286_GMSL_LINK(i)) {
                errorStatus->link |= MAX9286_GMSL_LINK(i);
                errorStatus->failureType = ISC_MAX9286_LINE_BUFFER_OVERFLOW;
            }
        }
    }

done:
    return NVMEDIA_STATUS_OK;
}

static NvMediaISCDeviceDriver deviceDriver = {
    .deviceName = "Maxim 9286 Aggregator",
    .deviceType = NVMEDIA_ISC_DEVICE_AGGREGATOR,
    .regLength = 1,
    .dataLength = 1,
    .DriverCreate = DriverCreate,
    .DriverDestroy = DriverDestroy,
    .CheckPresence = CheckPresence,
    .CheckLink = CheckLink,
    .GetLinkStatus = GetLinkStatus,
    .EnableLink = EnableLink,
    .SetDefaults = SetDefaults,
    .SetDeviceConfig = SetDeviceConfig,
    .GetTemperature = GetTemperature,
    .GetErrorStatus = GetErrorStatus,
    .ReadRegister = ReadRegister,
    .WriteRegister = WriteRegister,
    .WriteParameters = WriteParameters,
    .DumpRegisters = DumpRegisters
};

NvMediaISCDeviceDriver *
GetMAX9286Driver(void)
{
    return &deviceDriver;
}

