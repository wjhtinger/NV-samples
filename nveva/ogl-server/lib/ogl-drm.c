/*
 * Copyright (c) 2015, NVIDIA Corporation.  All Rights Reserved.
 *
 * BY INSTALLING THE SOFTWARE THE USER AGREES TO THE TERMS BELOW.
 *
 * User agrees to use the software under carefully controlled conditions
 * and to inform all employees and contractors who have access to the software
 * that the source code of the software is confidential and proprietary
 * information of NVIDIA and is licensed to user as such.  User acknowledges
 * and agrees that protection of the source code is essential and user shall
 * retain the source code in strict confidence.  User shall restrict access to
 * the source code of the software to those employees and contractors of user
 * who have agreed to be bound by a confidentiality obligation which
 * incorporates the protections and restrictions substantially set forth
 * herein, and who have a need to access the source code in order to carry out
 * the business purpose between NVIDIA and user.  The software provided
 * herewith to user may only be used so long as the software is used solely
 * with NVIDIA products and no other third party products (hardware or
 * software).   The software must carry the NVIDIA copyright notice shown
 * above.  User must not disclose, copy, duplicate, reproduce, modify,
 * publicly display, create derivative works of the software other than as
 * expressly authorized herein.  User must not under any circumstances,
 * distribute or in any way disseminate the information contained in the
 * source code and/or the source code itself to third parties except as
 * expressly agreed to by NVIDIA.  In the event that user discovers any bugs
 * in the software, such bugs must be reported to NVIDIA and any fixes may be
 * inserted into the source code of the software by NVIDIA only.  User shall
 * not modify the source code of the software in any way.  User shall be fully
 * responsible for the conduct of all of its employees, contractors and
 * representatives who may in any way violate these restrictions.
 *
 * NO WARRANTY
 * THE ACCOMPANYING SOFTWARE (INCLUDING OBJECT AND SOURCE CODE) PROVIDED BY
 * NVIDIA TO USER IS PROVIDED "AS IS."  NVIDIA DISCLAIMS ALL WARRANTIES,
 * EXPRESS, IMPLIED OR STATUTORY, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF TITLE, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.

 * LIMITATION OF LIABILITY
 * NVIDIA SHALL NOT BE LIABLE TO USER, USERS CUSTOMERS, OR ANY OTHER PERSON
 * OR ENTITY CLAIMING THROUGH OR UNDER USER FOR ANY LOSS OF PROFITS, INCOME,
 * SAVINGS, OR ANY OTHER CONSEQUENTIAL, INCIDENTAL, SPECIAL, PUNITIVE, DIRECT
 * OR INDIRECT DAMAGES (WHETHER IN AN ACTION IN CONTRACT, TORT OR BASED ON A
 * WARRANTY), EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGES.  THESE LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF THE
 * ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.  IN NO EVENT SHALL NVIDIAS
 * AGGREGATE LIABILITY TO USER OR ANY OTHER PERSON OR ENTITY CLAIMING THROUGH
 * OR UNDER USER EXCEED THE AMOUNT OF MONEY ACTUALLY PAID BY USER TO NVIDIA
 * FOR THE SOFTWARE PROVIDED HEREWITH.
 */

#include <stdlib.h>

#include "ogl-drm.h"
#include "ogl-debug.h"

const char *drmConnectorName(int type)
{
	switch (type) {
	case DRM_MODE_CONNECTOR_VGA:
		return "VGA";
	case DRM_MODE_CONNECTOR_DVII:
		return "DVII";
	case DRM_MODE_CONNECTOR_DVID:
		return "DVID";
	case DRM_MODE_CONNECTOR_DVIA:
		return "DVIA";
	case DRM_MODE_CONNECTOR_Composite:
		return "Composite";
	case DRM_MODE_CONNECTOR_SVIDEO:
		return "SVIDEO";
	case DRM_MODE_CONNECTOR_LVDS:
		return "LVDS";
	case DRM_MODE_CONNECTOR_Component:
		return "Component";
	case DRM_MODE_CONNECTOR_9PinDIN:
		return "9PinDIN";
	case DRM_MODE_CONNECTOR_DisplayPort:
		return "DisplayPort";
	case DRM_MODE_CONNECTOR_HDMIA:
		return "HDMIA";
	case DRM_MODE_CONNECTOR_HDMIB:
		return "HDMIB";
	case DRM_MODE_CONNECTOR_TV:
		return "TV";
	case DRM_MODE_CONNECTOR_eDP:
		return "eDP";
	default:
		break;
	}
	return "Unknown";
}

drmModeCrtcPtr oglInitCRTC(int fd_drm, int disp_idx, int mode_idx)
{
	drmModeResPtr mode_res;
	drmModeCrtcPtr crtc = NULL;
	drmModeConnectorPtr conn;
	drmModeEncoderPtr enc;
	drmModeModeInfo mode;
	uint32_t crtc_id;
	int retval;

	if (fd_drm < 0) {
		ogl_debug("condition failed");
		return NULL;
	}

	mode_res = drmModeGetResources(fd_drm);
	if (!mode_res) {
		ogl_debug("condition failed");
		return NULL;
	}

	if (disp_idx < 0 || disp_idx > mode_res->count_connectors) {
		for (disp_idx = 0; disp_idx < mode_res->count_connectors; ++disp_idx) {
			conn = drmModeGetConnector(fd_drm, mode_res->connectors[disp_idx]);
			if (!conn)
				continue;

			if (conn->connection == DRM_MODE_CONNECTED &&
			    conn->count_modes > 0)
				break;
			drmModeFreeConnector(conn);
		}

		if (disp_idx >= mode_res->count_connectors) {
			ogl_debug("condition failed");
			goto out_mode_res;
		}
	} else {
		conn = drmModeGetConnector(fd_drm, mode_res->connectors[disp_idx]);
		if (!conn) {
			ogl_debug("condition failed");
			goto out_mode_res;
		}

		if (conn->count_modes < 1) {
			ogl_debug("condition failed");
			drmModeFreeConnector(conn);
			goto out_mode_res;
		}

		if (!conn->connection == DRM_MODE_CONNECTED)
			ogl_warn("display %i disconnected", disp_idx);
	}

	enc = drmModeGetEncoder(fd_drm, conn->encoder_id);
	if (!enc)
	       goto out_conn;

	crtc_id = enc->crtc_id;
	drmModeFreeEncoder(enc);

	crtc = drmModeGetCrtc(fd_drm, crtc_id);
	if (!crtc) {
		ogl_debug("condition failed");
		goto out_conn;
	}

	if (mode_idx < 0 || mode_idx > conn->count_modes) {
		if (!crtc->mode_valid) {
			mode_idx = 0;
			ogl_warn("Display %i has no active mode and --mode <num> was not "
				"specified.  Defaulting to mode %d (%d, %d)\n",
				disp_idx, mode_idx,
				conn->modes[mode_idx].hdisplay,
				conn->modes[mode_idx].vdisplay);
		} else {
			mode = conn->modes[0];
			ogl_debug("skipping mode set");
			goto out;
		}
	}

	drmModeFreeCrtc(crtc);
	mode = conn->modes[mode_idx];
	retval = drmModeSetCrtc(fd_drm, crtc_id, (unsigned)-1, 0, 0,
				&conn->connector_id, 1, &mode);
	if (retval) {
		ogl_debug("condition failed");
		goto out_conn;
	}

	crtc = drmModeGetCrtc(fd_drm, crtc_id);
	if (!crtc) {
		ogl_debug("condition failed %u", crtc_id);
		goto out_conn;
	}
out:
	ogl_debug("connector: %s mode: %ux%u@%u",
		  drmConnectorName(conn->connector_type),
		  mode.hdisplay, mode.vdisplay, mode.vrefresh);
out_conn:
	drmModeFreeConnector(conn);
out_mode_res:
	drmModeFreeResources(mode_res);
	return crtc;
}
