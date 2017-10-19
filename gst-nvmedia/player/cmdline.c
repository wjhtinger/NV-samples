/* Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cmdline.h"

static gint    s_section_start = 0;
static gint    s_section_stop  = 0;
static gint    s_section_size  = 0;
static gchar **s_section_ptr   = NULL;

static GstNvmResult
gst_nvm_tokenize_line (
    gchar *input,
    gchar **input_tokens,
    gint *tokens_num)
{
    gint input_length = strlen (input);
    gint i = 0, k;
    gchar str_temp[255];
    gboolean quoted_flag;

    *tokens_num = 0;
    for (i = 0; i < input_length; i++) {
        while (i < input_length && isspace (input[i])) {
            i++;
        }
        quoted_flag = FALSE;
        if (i < input_length && input[i] == '"') {
            quoted_flag = TRUE;
            i++;
        }
        k = 0;
        if (quoted_flag) {
            while (i < input_length && input[i] != '"') {
                str_temp[k++] = input[i++];
            }
            if (i < input_length && input[i] == '"') {
                i++;
            }
            str_temp[k] = '\0';
        } else {
            while (i < input_length && !isspace (input[i])) {
                str_temp[k++] = input[i++];
            }
            str_temp[k]='\0';
        }
        if (k) {
            input_tokens[*tokens_num] = (char *) g_malloc (k + 1);
            strcpy (input_tokens[(*tokens_num)++], str_temp);
        }
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
gst_nvm_read_line (
    FILE *file,
    gchar input[],
    gboolean is_valid_command)
{
    if (!file) {
        fgets (input, GST_NVM_MAX_STRING_SIZE, stdin);
    } else {
        fgets (input, GST_NVM_MAX_STRING_SIZE, file);
        //fgets doesn't omit the newline character
        if (input[strlen (input) - 1] == '\n')
            input[strlen (input) - 1] = '\0';
        if (/*g_verbose && */is_valid_command)
        g_print ("%s\n",input);
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
gst_nvm_is_section_enabled (
    gchar **tokens,
    gboolean *is_section_enabled)
{
    gchar section[GST_NVM_MAX_STRING_SIZE] = {0};
    gint  i = 0;

    if (!s_section_ptr)
        *is_section_enabled = FALSE;

    /* check whether section is enabled in input string */
    for (i = s_section_start; i < s_section_start + s_section_size; i++) {
        sprintf (section, "[%s]", s_section_ptr[i]);
        if (strstr (tokens[0], section)){
            *is_section_enabled = TRUE;
        }
    }
    *is_section_enabled = FALSE;

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_parse_next_command (
    FILE *file,
    gchar *input_line,
    gchar **input_tokens,
    gint *tokens_num)
{
    gboolean section_done_flag;
    gboolean is_section_enabled;

    gst_nvm_read_line (file, input_line, TRUE);
    gst_nvm_tokenize_line (input_line, input_tokens, tokens_num);

    /* start processing sections, if there are any */
    if (s_section_size && *tokens_num) {
        /* get start of the section */
        section_done_flag = FALSE;
        if ((input_tokens[0][0] == '[') &&
            (strcmp (input_tokens[0], "[/s]"))) {
            gst_nvm_is_section_enabled (input_tokens, &is_section_enabled);
            if (is_section_enabled) {
                while (!section_done_flag) {
                    gst_nvm_read_line (file, input_line, FALSE);
                    gst_nvm_tokenize_line (input_line,
                                           input_tokens,
                                           tokens_num);
                    if (*tokens_num) {
                        if (!strncmp (input_tokens[0], "[/s]", 4)) {
                            section_done_flag = TRUE;
                        }
                    }
                }
            } else {
                gst_nvm_read_line (file, input_line, TRUE);
                gst_nvm_tokenize_line (input_line, input_tokens, tokens_num);
            }
        }
    }

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_parse_command_line (
    gint argc,
    gchar *argv[],
    GstNvmPlayerTestArgs *test_args)
{
    char *config_file_name = NULL;
    gint i;

    memset (test_args->script_name, '\0', GST_NVM_MAX_STRING_SIZE);

    if (argc >= 2) {
        for (i = 0; i < argc; i++) {
            if (!strcmp (argv[i], "-s")) {
                test_args->cmd_line_script_flag = TRUE;
                strcpy (test_args->script_name, argv[i + 1]);
                GST_INFO ("Running script %s\n", test_args->script_name);
            }
            else if (!strcmp (argv[i], "-rsr")) {
                test_args->cmd_line_rscript_flag = TRUE;
                strcpy (test_args->script_name, argv[i + 1]);
                GST_INFO ("Running script %s recursively\n", test_args->script_name);
            } else if (!strcmp (argv[i], "-p")) {
                s_section_start = i + 1;
                s_section_stop = i + 1;
                while (s_section_stop < argc) {
                    if (argv[s_section_stop][0] != '-') {
                        s_section_stop++;
                    } else {
                        break;
                    }
                }
                s_section_size = s_section_stop - s_section_start;
                s_section_ptr  = argv;
                i += s_section_size;
            } else if (!strcmp (argv[i], "--enable-multi-inst")) {
                test_args->multi_inst_flag = TRUE;
            } else if (!strcmp (argv[i], "--capture")) {
                test_args->default_ctx_capture_flag = TRUE;
                if (argv[i + 1] && argv[i + 1][0] != '-') {
                    strncpy (test_args->capture_config_file, argv[i + 1], GST_NVM_MAX_STRING_SIZE);
                } else {
                    config_file_name = getenv ("GST_NVM_PLAYER_CAPTURE_CONFIG");
                    if (config_file_name)
                        strncpy (test_args->capture_config_file, config_file_name, GST_NVM_MAX_STRING_SIZE);
                    else
                        strcpy (test_args->capture_config_file, "configs/capture.conf");
                }
            } else if (!strcmp (argv[i], "--usb")) {
                test_args->default_ctx_usb_flag = TRUE;
                if (argv[i + 1] && argv[i + 1][0] != '-') {
                    strncpy (test_args->usb_device, argv[i + 1], GST_NVM_MAX_STRING_SIZE);
                } else {
                    strcpy (test_args->usb_device, "Android");
                }
            }
            else if (!strcmp (argv[i], "--avbsrc")) {
                test_args->default_ctx_avb_src_flag = TRUE;
            }
            else if (!strcmp (argv[i], "--avbsink")) {
                test_args->default_ctx_avb_sink_flag = TRUE;
            }
        }
    }

    return GST_NVM_RESULT_OK;
}

void
gst_nvm_print_options ()
{
    g_print ("Usage: gst-nvmedia-player [command line options]\n");
    g_print ("(Display device types supported are HDMI and DP only)\n");
    g_print ("\nAvailable command line options:\n");
    g_print ("-s [script name]                          (run script)\n");
    g_print ("-rsr [script_name]                        (run commands in script, recursively)\n");
    g_print ("--enable-multi-inst                       (enable multiple contexts)\n");
    g_print ("--capture [config]                        (start capture context (default: media-player))\n");
    g_print ("                                          ('config' is the capture configuration file to be used.\n");
    g_print ("                                          (if not specified, GST_NVM_PLAYER_CAPTURE_CONFIG environment variable will be used.\n");
    g_print ("                                          (if GST_NVM_PLAYER_CAPTURE_CONFIG is not set, default config file will be used: configs/capture.conf)\n");
    g_print ("--usb [mode]                              (start usb context (default: media-player))\n");
    g_print ("                                          ('mode' is the Operating System of the device.\n");
    g_print ("                                          (if not specified, Android OS will be taken as default.\n");
    g_print ("\nAvailable run-time commands:\n");
    g_print ("cctx [audio device]                       (create new player context)\n");
    g_print ("                                          (do aplay -l/-L to get list of available audio devices) \n");
    g_print ("                                          (first run of the player takes the default audio device from asound.conf) \n");
    g_print ("cctx [play/cap/usb/avbsrc/avbsink]        [audio device] \n");
    g_printf("                                          (create new player, capture or usb context)\n");
    g_print ("sctx [n]                                  (set control to context <n>)\n");
    g_print ("dcc                                       (describe current context)\n");
    g_print ("lac                                       (list available contexts)\n");
    g_print ("use-decodebin [n]                         (n=1: use decodebin; n=0: use playbin. default:0)\n");
    g_print ("enable-eglsink [GL_YUV/GL_RGB/            (enables egsink usage. use GL_YUV/GL_RGB/CUDA_YUV/CUDA_RGB for graphics/CUDA as consumer.\n");
    g_print ("                CUDA_YUV/CUDA_RGB]\n");
    g_print ("                                           set egl s=display using 'dd [egl/mirror-egl]')\n");
    g_print ("                                          (Note: only one context can be using eglsink at a time.\n");
    g_print ("disable-eglsink                           (disables egsink.)\n");
    g_print ("fpsdisp [0/1]                             (disable/enable fps display- set/unset before media playback)\n");
    g_print ("dmx [n]                                   (Audio downmixing. n=1: downmix to stereo; n=0: disable downmix; default:0)\n");
    g_print ("ldd                                       (print available display devices)\n");
    g_print ("dd                                        (set display type\n");
    g_print ("                                          (if 'NULL' is specified, context won't be using any display\n");
    g_print ("                                          (if 'dual' is specified, context will be using both displays if available\n");
    g_print ("                                          (if 'mirror-egl' is specified, context will ADD egl mirroring\n");
    g_print ("ddd                                       (set display type dynamically\n");
    g_print ("wi [n]                                    (set display window id [0-2]\n");
    g_print ("wd [n]                                    (set display window depth [0-255]\n");
    g_print ("wp [x0:y0:W:H]                            (set display position)\n");
    g_print ("dwi [n]                                   (set display window id dynamically [0-2]\n");
    g_print ("dwd [n]                                   (set display window depth dynamically [0-255]\n");
    g_print ("dwp [x0:y0:W:H]                           (set display position dynamically)\n");
    g_print ("brt  [value]                              (adjust the brightness [-0.5-0.5]\n");
    g_print ("cont [value]                              (adjust the contrast [0.1-2.0]\n");
    g_print ("sat  [value]                              (adjust the saturation [0.1-2.0]\n");
    g_print ("rs [script_name]                          (run commands in script)\n");
    g_print ("pr  'info'                                (echo the info on the screen)\n");
    g_print ("pinfo                                     (print the plugins getting used currently)\n");
    g_print ("kill [n]                                  (kill ctx n; n cannot be controlled ctx)\n");
    g_print ("q                                         (quit the application)\n");
    g_print ("\nCommands specific for media player context:\n");
    g_print ("pp                                        (pause/resume)\n");
    g_print ("po [milliseconds]                         (set-position [0 - length of the stream])\n");
    g_print ("ss [speed]                                (set-speed [-16 to 16])\n");
    g_print ("pv [file_name]                            (play video file)\n");
    g_print ("kv                                        (kill the currently running video)\n");
    g_print ("pa [file_name]                            (play the audio file)\n");
    g_print ("ka                                        (kill the currently running audio)\n");
    g_print ("wt [seconds]                              (wait for time specified)\n");
    g_print ("we                                        (wait till end of playback)\n");
    g_print ("xx  [value]                               (1/0 enable/disable media related messages)\n");
    g_print ("pm  [value]                               (1/0 enable/disable ptm values.\n");
    g_print ("                                           output format: curr_pos(ms)/total_len(ms))\n");
    g_print ("ad [audio device]                         (set audio device from the alsa audio list)\n");
    g_print ("                                          (If NULL is chosen, context won't be using any audio device\n");
    g_print ("                                           just ad will make it default device\n");
    g_print ("dad [audio device]                        (set audio device dynamically\n");
    g_print ("minfo                                     (print media info of playing media)\n");
    g_print ("\nCommands specific for capture context:\n");
    g_print ("lps                                       (list available param sets.\n");
    g_print ("                                           param sets are defined in capture.conf)\n");
    g_print ("scm                                       (set capture mode. options: [live/cb]. default: live\n");
    g_print ("scrc                                      (set crc checksum file location or NULL for no checks)\n");
    g_print ("                                           Available only in test mode. default: NULL)\n");
    g_print ("sc [param_set]                            (start capture. use parameters specified in\n");
    g_print ("                                           <param_set> section of the config file)\n");
    g_print ("kc                                        (kill capture) \n");
    g_print ("\nCommands specific for usb context:\n");
    g_print ("pu [input port]                           (play from usb from input port[ex-plughw:1,0])\n");
    g_print ("ku                                        (kill the currently running audio from usb)\n");
    g_print ("da [file name]                            (dump audio to file)\n");
    g_print ("da                                        (disable dump audio to file)\n");
    g_print ("s [usb device]                            (switch usb input device type)\n");
    g_print ("\nCommands specific for avbsrc context:\n");
    g_print ("pt [file_name]                            (specify filename if file source is used)\n");
    g_print ("                                          (send video over avb as AVB Talker)\n");
    g_print ("pt [eglstream width height socket_path surface_type fifo/mailbox low-latency-enable/low-latency-disable]\n");
    g_print ("                                          (send video over avb as AVB Talker)\n");
    g_print ("                                          (eglstream option for eglstream as source)\n");
    g_print ("                                          (width: input stream width)\n");
    g_print ("                                          (height: input stream height)\n");
    g_print ("                                          (socket_path: socket path)\n");
    g_print ("                                          (surface_type: see nvmediaeglstreamsrc plugin properties for supported surface types values)\n");
    g_print ("                                          (fifo : enable fifo mode, mailbox : enable mailbox mode)\n");
    g_print ("                                          (low-latency-enable : enable low latency encode, low-latency-disable : disable low-latency encode. It is optional. Disabled by default)\n");
    g_print ("st [eth interface]                        (selects the ethernet interface for AVB Talker)\n");
    g_print ("sst [stream_id]                           (selects the LSB 2 bytes of the stream id for AVB Talker)\n");
    g_print ("priot                                     (selects the vlan priority of the stream for AVB Talker)\n");
    g_print ("kt                                        (stop  AVB Talker)\n");
    g_print ("\nCommands specific for avbsink context:\n");
    g_print ("pl [mpegts/filempegts/audio/fileaudio/audio8/fileaudio8/aaf/fileaaf  low-latency-enable]\n");
    g_print ("                                          (starts playback of audio/video or dumps audio/video received over AVB)\n");
    g_print ("                                          (low-latency-enable : enable low-latency decode. It is optionali. low latency decode is disabled by default\n");
    g_print ("sl [eth interface]                        (selects the ethernet interface for AVB Listener)\n");
    g_print ("ssl [stream id]                           (selects the input stream id for AVB Listener)\n");
    g_print ("kl                                        (stop AVB Listener)\n");
    g_print ("h                                         (print the above information)\n");
}
