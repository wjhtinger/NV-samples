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

#ifndef __OGL_DEBUG_H__
#define __OGL_DEBUG_H__

#include <stdio.h>
#include <string.h>

#define __OGL_STRINGIFY(x, ...)	#x, ##__VA_ARGS__
#define OGL_STRINGIFY(x, ...)	__OGL_STRINGIFY(x)

#define OGL_LOG_STREAM	stdout
#define ogl_printf(arg, ...) fprintf(OGL_LOG_STREAM, arg, ##__VA_ARGS__)

#define OGL_ERROR	"ERROR: "
#define OGL_WARN	"WARNING: "
#define OGL_INFO	"INFO: "
#define OGL_DEBUG	"DEBUG: "

#define __FILENAME__ (strrchr("/" __FILE__, '/') + 1)

#define ogl_pr(level, arg, ...) ogl_printf(level "%s: %u: " arg "\n" ,	\
					__FILENAME__, __LINE__, ##__VA_ARGS__)

#define ogl_error(arg, ...)	ogl_pr(OGL_ERROR, arg, ##__VA_ARGS__)
#define ogl_warn(arg, ...)	ogl_pr(OGL_WARN, arg, ##__VA_ARGS__)
#define ogl_info(arg, ...)	ogl_pr(OGL_INFO, arg, ##__VA_ARGS__)

#ifdef DEBUG
#define ogl_debug(arg, ...)	ogl_pr(OGL_DEBUG, arg, ##__VA_ARGS__)
#else
#define ogl_debug(arg, ...)
#endif

#endif	/* __OGL_DEBUG_H__ */
