/*
 * Copyright (c) 2011-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>
#include <nvmnand.h>

/* common functions */
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

#define SECS_PER_DAY                                   (24 * 3600)
#define MNAND_SECS_TO_REFRESH(d)                       ((d) * SECS_PER_DAY)
#define MNAND_DESIRED_REFRESH_PERCENTAGE_PER_DAY(d, p) ((double)(p) / (d))
#define MNAND_DESIRED_REFRESH_PERCENTAGE_PER_SEC(d, p) (MNAND_DESIRED_REFRESH_PERCENTAGE_PER_DAY(d,p) / SECS_PER_DAY)
#define DEFAULT_REFRESH_PERCENTAGE_PER_DAY(d, p)       ((double)(p) / (d))
#define DEFAULT_REFRESH_PERCENTAGE_PER_HOUR(d, p)      (DEFAULT_REFRESH_PERCENTAGE_PER_DAY(d,p) / 24)
#define DEFAULT_REFRESH_PERCENTAGE_PER_SEC(d, p)       (DEFAULT_REFRESH_PERCENTAGE_PER_HOUR(d,p) / 3600)
#define MORE_THAN_X_DAYS_OLD(d, t, cur)                (difftime((cur), (t)) > MNAND_SECS_TO_REFRESH(d) ? 1 : 0)

#define MAX_LINES_IN_FILE                   250
#define MNAND_RFSH_FINISHED                 "/tmp/mnand_rfsh.finished"
#define DATETIME_FORMAT                     "%d%m%Y%H%M%S"

static int s_verbosity = 0;
volatile int s_stop = 0;
static int s_use_syslog = 0;

static void log_to_syslog(int level, const char *fmt, ...)
{
    int priority;
    va_list ap;

    switch (level) {
        case 1:
            priority = LOG_WARNING;
            break;
        case 2:
            priority = LOG_INFO;
            break;
        default:
            priority = LOG_ERR;
    }
    va_start(ap, fmt);
    vsyslog(priority, fmt, ap);
    va_end(ap);
}

#define VERBOSE_PRINTF(level, ...) \
    do { \
        if (s_verbosity >= level) {\
        if (s_use_syslog) \
            log_to_syslog(s_use_syslog, __VA_ARGS__); \
        else \
            printf(__VA_ARGS__); }\
    } while (0)

static int refresh_loop(mnand_chip *chip, uint32_t inter_rfsh_delay,
    uint32_t duration, double target_progress)
{
    mnand_refresh_progress rfsh_progress;
    int res = 0;
    struct timespec t;
    uint64_t starttime = 0;
    uint64_t endtime = 0;
    uint32_t rfsh_cnt = 0;
    uint32_t remain;
    double cur_progress = 0.0;

    if (duration) {
        clock_gettime(CLOCK_MONOTONIC, &t);
        starttime = (uint64_t)t.tv_sec * 1000;
    }

    while (s_stop == 0) {
        if (duration) {
            clock_gettime(CLOCK_MONOTONIC, &t);
            endtime = (uint64_t)t.tv_sec * 1000;
            if ((endtime - starttime) >= duration) /* duration reached */
                break;
        }

        remain = duration ? min(inter_rfsh_delay, duration) : inter_rfsh_delay;
        usleep(1000 * remain);

        VERBOSE_PRINTF(2, "Sending refresh (%d) ... \n", rfsh_cnt);
        if (mnand_send_refresh(chip, MNAND_MLC_BLOCK, 1, &rfsh_progress) != MNAND_OK) {
            VERBOSE_PRINTF(0, "Failure to refresh blocks\n");
            return -1;
        } else
            rfsh_cnt++;

        if (target_progress != 0.0) { /* check the progress if needed */
            if (rfsh_progress.progress_type == MNAND_RFSH_PROG_IN_PERCENT)
                cur_progress = rfsh_progress.progress.percentage;
            else {
#if 0
                /* may not need to get the progress every iteration?? */
                if ((chip->rfsh_properties.rfsh_calibrate_cnt != 0) &&
                    ((rfsh_cnt % chip->rfsh_properties.rfsh_calibrate_cnt) != 0))
                    continue;
#endif
                if (mnand_get_refresh_progress(chip, &cur_progress) != MNAND_OK) {
                    VERBOSE_PRINTF(0, "Failure to get refresh progress\n");
                    return -1;
                }
            }
            VERBOSE_PRINTF(2, "After refresh (%d) progress %.6f%%\n", rfsh_cnt - 1, cur_progress);
            if ((double)cur_progress >= target_progress) /* progress reached */
                break;
        }
    }
    return res;
}

/**************************************
 * Management of historic information *
 **************************************/

typedef struct _info_node {
    struct _info_node *next;
    int index; /* simple node count */
    time_t date_time;
    double rfsh_progress;
} info_node;

/* Convert date/time string formatted as DDMMYYYYhhmmss to time_t */
static int str_to_time(char *s, time_t *t)
{
    struct tm dt;
    int res = 1;

    if (strlen(s) != 14)
        return res; /* invalite date/time str */

    if (strptime(s, DATETIME_FORMAT, &dt) == NULL)
        return 1;

    if (t)
        *t = mktime(&dt);
    return 0;
}

/*
 * Add all characters in string pointed by s shifting values to expand
 * result sum range.
 */
static unsigned int add_chars(char *s, int shift_start)
{
    unsigned int sum = 0;
    int shift = shift_start;

    if (!s)
        return 0;
    while (*s) {
        sum += *s++ << shift;
        shift += 2;
    }
    return sum;
}

/*
 * Given a full line, add values of all character of its two first
 * comma-separated fields. Commas not included.
 */
static unsigned int calc_line_checksum(char *line)
{
    char *line_bkp;
    char *p;
    unsigned int sum = 0;

    if (!line)
        return 0;
    line_bkp = malloc(strlen(line) + 1);
    if (!line_bkp)
        return 0;
    strcpy(line_bkp, line);
    p = strtok(line_bkp, ",");
    if (p) {
        sum += add_chars(p, 0);
        p = strtok(NULL, ",");
        if (p)
            sum += add_chars(p, 10);
    }
    free(line_bkp);
    return sum;
}

/* Parse a line from file into existing linked list node */
static int parse_line(char *line, info_node *node, int num_blocks)
{
    char *p;
    int res = 1;
    unsigned int expected_sum;
    unsigned int sum;

    if (!line || !node)
        return res;

    expected_sum = calc_line_checksum(line);
    p = strtok(line, ",");
    if (p) {
        str_to_time(p, &node->date_time);
        p = strtok(NULL, ",");
        if (p) {
            if (*p == '@') {
                p++;
                node->rfsh_progress = (double)atof(p);
            } else {
                /*
                 * old format with total ages, need to convert to progress percentage.
                 * under such scenario, num_blocks won't be 0.
                 */
                node->rfsh_progress = ((double)atoll(p) * 100.0) / num_blocks;
            }
            p = strtok(NULL, ",");

            /*
             * If checksum exists in line, check it. If not (old file),
             * assume line is correct.
             */
            if (p) {
                sum = strtoul(p, NULL, 0);
                if (sum == expected_sum)
                    res = 0;
            }
            else
                res = 0;
        }
    }
    return res;
}

/* Free entire linked list, clean up head */
static void free_info(info_node **head)
{
    info_node *node;

    if (!head)
        return;
    while (*head) {
        node = (*head)->next;
        free(*head);
        *head = node;
    }
}

/* Count items in list */
static int count_info(info_node **head)
{
    int count = 0;
    info_node *node;

    if (!head || !(*head))
        return count;
    node = *head;
    do {
        count++;
        node = node->next;
    } while (node);
    return count;
}

/* Return 1 if two date/times have same date */
static int same_day(time_t date_time1, time_t date_time2)
{
    struct tm *dt;
    struct tm dt1;
    struct tm dt2;

    dt = localtime(&date_time1);
    dt1 = *dt;
    dt = localtime(&date_time2);
    dt2 = *dt;
    return (dt1.tm_mday == dt2.tm_mday &&
        dt1.tm_mon == dt2.tm_mon &&
        dt1.tm_year == dt2.tm_year) ? 1 : 0;
}

/*
 * Add node to end of linked list. If no_dup == 1 and the last entry has
 * the same date, do nothing.
 */
static int add_info(info_node **head, time_t date_time, double rfsh_prg, int no_dup)
{
    info_node *node;
    info_node *next_node;

    if (!head)
        return 1;
    node = *head;

    /* find last item in list */
    while (node && node->next)
        node = node->next;

    /*
     * in case list not empty and last node matches new node's date, do
     * nothing if no_dup is set.
     */
    if (node && no_dup && same_day(node->date_time, date_time))
        return 0;

    /* create new node and make tail node point at it */
    next_node = malloc(sizeof(info_node));
    if (!next_node)
        return 1;
    if (node)
        node->next = next_node;
    else
        *head = next_node;
    node = next_node;
    node->date_time = date_time;
    node->rfsh_progress = rfsh_prg;
    node->next = NULL;
    node->index = count_info(head) - 1;
    return 0;
}

/* Empty linked list and load historic data from file into linked list. */
static int load_info(char *file_name, info_node **head, int num_blocks)
{
    FILE *fp;
    char buf[100];
    int res = 1;
    info_node node = { 0, };
    info_node *new_node;
    info_node *cur_node = NULL;
    int index = 0;

    if (!head)
        return res;
    free_info(head);
    fp = fopen(file_name, "r");
    if (fp == NULL)
        return res;
    while (fgets(buf, sizeof(buf), fp)) {
        if (parse_line(buf, &node, num_blocks) == 0) {
            new_node = malloc(sizeof(info_node));
            if (!new_node) {
                res = 1;
                break;
            }
            *new_node = node;
            new_node->index = index++;
            new_node->next = NULL;
            if (cur_node)
                cur_node->next = new_node;
            else
                *head = new_node;
            cur_node = new_node;
            res = 0; /* at least one line of valid data was detected */
        }
    }
    fclose(fp);
    return res;
}

/* Convert linked list node to string pointed by p, up to len characters */
static int node_to_str(info_node *node, char *p, int len)
{
    struct tm *dt;
    char aux[100];

    if (!node || !p)
        return 1;
    dt = localtime(&node->date_time);
    if (!dt)
        return 1;
    strftime(p, len, DATETIME_FORMAT, dt);
    sprintf(aux, ",@%lf,", node->rfsh_progress);
    strncat(p, aux, len - strlen(p));
    sprintf(aux, "%u\n", calc_line_checksum(p));
    strncat(p, aux, len - strlen(p));
    return 0;
}

/*
 * Save historic linked list to file, up to max_entries. If list longer
 * than max_entries, only the list tail is saved.
 */
static int save_info(char *file_name, info_node **head, int max_entries)
{
    int res = 1;
    FILE *fp;
    char buf[100];
    info_node *node;
    int total_count;

    if (!head)
        return res;
    fp = fopen(file_name, "w");
    if (fp == NULL)
        return res;
    node = *head;
    total_count = count_info(head);
    while (node) {
        /*
         * If no limit in number of nodes in file, write node.
         * If limit is set, but total list doesn't pass it, write node.
         * If limit set and list exceeds it, write only nodes from beginning
         *   and end of list, skipping nodes in the center.
         */
        if (max_entries == 0 ||
            total_count <= max_entries ||
            node->index >= total_count - max_entries) {
            if (node_to_str(node, buf, sizeof(buf)) == 0) {
                if (fputs(buf, fp) >= 0)
                    res = 0;
            }
        }
        node = node->next;
    }
    fclose(fp);
    return res;
}

//#define DEBUG_DUMP_INFO
#ifdef DEBUG_DUMP_INFO
/* Display linked list info (debug) */
static void dump_info(info_node **head)
{
    info_node *node;
    char buf[100];

    printf("Info:\n");
    if (!head || !(*head))
        return;
    node = *head;
    while (node) {
        if (node_to_str(node, buf, sizeof(buf)) == 0)
            printf("%s", buf);
        node = node->next;
    }
}
#else
static inline void dump_info(info_node **head)
{
    ; /* do nothing */
}
#endif

/* Calculate the desired progress should be */
static double calculate_desired_progress(uint32_t def_rfsh_days, info_node **head,
    double cur_progress, time_t cur_time)
{
    info_node *first;
    info_node *node;
    double desired_progress;
    double prog_within_past_xdays = 0;
    char first_dt_str[100] = "Invalid";
    char cur_dt_str[100] = "Invalid";
    struct tm *dt;

    if (!head || !(*head))
        return 0;
    /*
     * Use current age/time and entry from def_rfsh_days ago (chip specific) in
     * historic data to calculate number of blocks to refresh.
     */
    node = first = *head;

    /*
     * Sweep list to look for out-of-order elements. If any
     * out-of-order date is found, force "first" to point at it to ensure
     * only the last valid sequence of dates is used.
     */
    while (node->next) {
        if (difftime(node->next->date_time, node->date_time) < 0)
            first = node->next;
        node = node->next;
    }

    /*
     * Locate last entry from more than def_rfsh_days ago, if any, and store it in first.
     * Otherwise, store first valid entry in first (will be a recent entry).
     */
    while (first->next &&
           MORE_THAN_X_DAYS_OLD(def_rfsh_days, first->next->date_time, cur_time))
        first = first->next;

    /* Find progress from first historical entry that's less than def_rfsh_days old */
    if (!MORE_THAN_X_DAYS_OLD(def_rfsh_days, first->date_time, cur_time))
        node = first;
    else if (first->next && !MORE_THAN_X_DAYS_OLD(def_rfsh_days, first->next->date_time, cur_time))
        node = first->next;
    else
        node = NULL;

    if (node) {
        prog_within_past_xdays = node->rfsh_progress;
        VERBOSE_PRINTF(1, "Using progress from data point less than %d days old as %.6f%%\n",
            def_rfsh_days, prog_within_past_xdays);
    }

    dt = localtime(&first->date_time);
    if (dt)
        strftime(first_dt_str, sizeof(first_dt_str), "%d %b %Y", dt);
    dt = localtime(&cur_time);
    if (dt)
        strftime(cur_dt_str, sizeof(cur_dt_str), "%d %b %Y", dt);
    VERBOSE_PRINTF(1, "Using data points (%s, %.6f%%) and (%s, %.6f%%)\n",
        first_dt_str, first->rfsh_progress, cur_dt_str, cur_progress);

    desired_progress = first->rfsh_progress +
        (MNAND_DESIRED_REFRESH_PERCENTAGE_PER_SEC(def_rfsh_days, 100) *
        difftime(cur_time, first->date_time));

    VERBOSE_PRINTF(2, "MNAND_DESIRED_REFRESH_PERCENTAGE_PER_SEC(%d,100) %.6f%%\n", def_rfsh_days,
        MNAND_DESIRED_REFRESH_PERCENTAGE_PER_SEC(def_rfsh_days, 100));
    VERBOSE_PRINTF(2, "difftime %.6f days\n", difftime(cur_time, first->date_time) / SECS_PER_DAY);
    VERBOSE_PRINTF(2, "first_progress %.6f%%, cur_progress %.6f%%, desired_progress %.6f%%\n",
        first->rfsh_progress, cur_progress, desired_progress);

    return desired_progress;
}

static int usage(const char *progname)
{
    printf("usage: Session-mode: %s -d <path to mnand device> -p <path-to-persist> -S <time_sec> -s <session_time> [-t <date/time>] [-y days] [-v verbosity]\n\n"
           "   This tool attempts to ensure that the entire content of mNAND is refreshed within\n"
           "   a given number of days by checking the current refresh progress against the progress\n"
           "   logged a number of days ago (as defined by the \"-y\" parameter or the oldest log available).\n"
           "   The tool issues the mNAND refresh command as many times as needed to stay within the expected target.\n\n"
           "usage: Catch-up mode: %s -d <path to mnand device> -p <path-to-persist> -C <time_msec> [-t <date/time>] [-m time_sec] [-y days] [-v verbosity]\n\n"
           "   This tool with above set of arguments attempts to ensure that the content of mNAND is refreshed within a given time limit \"-m\"\n"
           "   by checking the current refresh progress against the progress logged a number of days ago\n"
           "   (as defined by the \"-y\" parameter or the oldest log available)\n\n"
           "usage: Blind-mode: %s -d <path to mnand device> -R <time_sec> [-v verbosity]\n\n"
           "   This tool with above set of arguments issues refresh continuously for predefined interval.\n\n"
           "   -d <mnand_path>   Path to mnand device (e.g. /dev/mnand00, /dev/emmc0, /dev/mmcblk0).\n"
           "   -p <path>         File name where tool can persist its information.\n"
           "   -t <date/time>    Current date/time in format DDMMYYYYhhmmss (in UTC).\n"
           "                     Use system time if not specified.\n"
           "   -y <days>         Number of days to refresh the whole device. 0 uses chip data.\n"
           "   -C <time_msec>    Catch-up mode. Delay between refresh commands (in msec). Default 100msec.\n"
           "                     This option requires '-p' for historic data\n"
           "                     cannnot be used with '-S' or '-R' options.\n"
           "   -m <time_sec>     Catch-up mode. Maximum time to run this tool (in seconds). Default (0)\n"
           "                     for no limit.\n"
           "                     This option requires '-p' for historic data\n"
           "                     cannnot be used with '-S' or '-R' options.\n"
           "   -S <time_sec>     Session mode. Perform proactive refresh continuously while monitoring the\n"
           "                     progress. Delay between refresh commands (in sec). 0 uses chip data\n"
           "                     This option requires '-p' for historic data and\n"
           "                     cannnot be used with '-m' or '-C' options.\n"
           "   -s <session_time> In session mode, delay between sessions (in seconds). 0 uses\n"
           "                     chip data\n"
           "   -R <time_sec>     Perform refresh with given number of seconds between refreshes (0 - use\n"
           "                     default). No historic/persistent data required and no progress be monitored.\n"
           "                     This option cannnot be used with '-S' or '-C' options.\n"
           "   -v <verbosity>    Set verbosity level (0 - 2). Default 0.\n"
           , progname, progname, progname);
    return 1;
}

static void sig_handler(int sig_num)
{
    s_stop = 1;
}

int main(int argc, char **argv)
{
    MNAND_STATUS mnand_status;
    mnand_chip chip;
    info_node *info_list = NULL;
    int mnand_bound = 0;
    int mnand_fd = -1;
    int res = 1;
    int blk_count = 0;
    char devnode[40] = "\0";
    char date_time_str[40] = "\0";
    char file_path[200] = "\0";
    int c;
    unsigned int max_time = 0;
    uint8_t valid_stored_info;
    time_t start_time;
    time_t base_time;
    time_t cur_time;
    time_t tmp;
    struct tm *dt;
#ifdef QNX
    uint32_t cmdlog = 6;
    uint32_t cur_cmdlog = 0;
#endif
    uint32_t blind_rfsh_interval = 0;
    uint32_t session_rfsh_interval = 0;
    uint32_t catchup_rfsh_interval = 100;
    uint32_t session_interval = 0;
    uint32_t refresh_in_days = 0;
    uint8_t blind_mode = 0;
    uint8_t session_mode = 0;
    uint8_t catchup_mode = 0;
    const char *prog = argv[0];
    struct timespec ts;
    double cur_rfsh_progress;
    double desired_progress;

    /* Remove node that notifies we are done */
    unlink(MNAND_RFSH_FINISHED);

    tmp = time(NULL); /* Use system time for now */
    dt = gmtime(&tmp);
    strftime(date_time_str, sizeof(date_time_str), DATETIME_FORMAT, dt);

    /* Use this as base so we can compute cur_time later */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    base_time = ts.tv_sec;

    while ((c = getopt(argc, argv, "d:p:v:m:C:s:S:R:y:t:h")) != -1) {
        switch (c) {
            case 'd':
                strncpy(devnode, optarg, sizeof(devnode) - 1);
                break;
            case 'p':
                strncpy(file_path, optarg, sizeof(file_path) - 1);
                break;
            case 'v':
                s_verbosity = strtoul(optarg, NULL, 0);
                break;
            case 'm':
                catchup_mode = 1;
                max_time = strtoul(optarg, NULL, 0);
                break;
            case 'C':
                catchup_mode = 1;
                catchup_rfsh_interval = strtoul(optarg, NULL, 0);
                break;
            case 's':
                session_mode = 1;
                session_interval = strtoul(optarg, NULL, 0);
                break;
            case 'S':
                session_mode = 1;
                session_rfsh_interval = strtoul(optarg, NULL, 0);
                break;
            case 'R':
                blind_mode = 1;
                blind_rfsh_interval = strtoul(optarg, NULL, 0);
                break;
            case 'y':
                refresh_in_days = strtoul(optarg, NULL, 0);
                break;
            case 't': {
                    /* Override system time as the time is given from command line */
                    strncpy(date_time_str, optarg, sizeof(date_time_str) - 1);
                    if (str_to_time(date_time_str, &tmp) != 0) {
                        printf("Invalid time: %s\n", date_time_str);
                        return usage(prog);
                    }
                }
                break;
            case '?':
            case 'h':
                return usage(prog);
            case ':':
            default:
                printf("Invalid argument %c %c\n", c, optopt);
                return usage(prog);
        }
    }

    if (catchup_mode && session_mode) {
        printf("Cannot use '-S' or '-s' option along with '-m' or '-C'\n");
        return usage(prog);
    }

    if (catchup_mode && blind_mode) {
        printf("Cannot use '-R' option along with '-m' or '-C'\n");
        return usage(prog);
    }

    if (session_mode && blind_mode) {
        printf("Cannot use '-S' or '-s' option along with '-R'\n");
        return usage(prog);
    }

    if (session_mode)
        s_use_syslog = 1;

    if (!session_mode && !blind_mode)
        catchup_mode = 1; /* default mode */

    if (devnode[0] == '\0') {
        printf("mNAND path must be provided\n");
        return usage(prog);
    }

    if (!blind_mode) {
        if (file_path[0] == '\0') {
            printf("Path to persistence file must be provided\n");
            return usage(prog);
        }
    }

    /* Getting the start_time (either given fron command line, or from system) */
    str_to_time(date_time_str, &start_time);

    /* Open mNAND device */
    if ((mnand_fd = open(devnode, O_RDWR)) < 0) {
        VERBOSE_PRINTF(0, "Failed to open mNAND.\n");
        return 1;
    } else
        VERBOSE_PRINTF(1, "Opened mNAND device %s\n", devnode);

    VERBOSE_PRINTF(0, "mNAND Refresh Tool (%s)\n", devnode);

#ifdef QNX
    /* In case running the actual catch up refresh loop, enable cmdlog verbosity */
    if (catchup_mode) {
        /* Get the current verbosity level */
        if (devctl(mnand_fd, DCMD_MMCSD_GET_CMDLOG, &cur_cmdlog, sizeof(cur_cmdlog), NULL) != EOK)
            printf("Failed to get current verbosity\n");
        else
            VERBOSE_PRINTF(1, "mNAND current verbosity is %u\n", cur_cmdlog);

        if (cur_cmdlog != cmdlog) {
            /* Enable highest verbosity for mNAND device */
            if (devctl(mnand_fd, DCMD_MMCSD_SET_CMDLOG, &cmdlog, sizeof(cmdlog), NULL) != EOK)
                printf("Failed to set verbosity to %u\n", cmdlog);
            else
                VERBOSE_PRINTF(1, "mNAND device verbosity set to %u\n", cmdlog);
        }
    }
#endif

    /* Bind mNAND device */
    if (mnand_bind(mnand_fd, &chip) != MNAND_OK) {
        printf("Failed to bind mNAND.\n");
        goto exit_out;
    } else {
        VERBOSE_PRINTF(1, "mNAND bind successful\n");
        mnand_bound = 1;
        if (!mnand_is_refresh_available(&chip)) {
            VERBOSE_PRINTF(0, "***************************************************\n");
            VERBOSE_PRINTF(0, "Refresh is not available due to un-supported mNAND.\n");
            VERBOSE_PRINTF(0, "***************************************************\n");
            goto exit_out;
        }
    }

    /* Register signal handlers for graceful termination */
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    if (blind_mode) {
        /* Perform proactive refresh based on the fixed given interval (blind mode) */
        if (blind_rfsh_interval == 0)
            blind_rfsh_interval = chip.rfsh_properties.def_rfsh_interval / 1000;

        VERBOSE_PRINTF(1, "Refresh interval = %d sec.\n", blind_rfsh_interval);
        res = refresh_loop(&chip, blind_rfsh_interval * 1000, 0, 0.0); /* loop until being stopped */
        goto exit_out;
    }

    if (refresh_in_days == 0)
        refresh_in_days = chip.rfsh_properties.rfsh_days;
    VERBOSE_PRINTF(1, "Whole device refresh in %d days\n", refresh_in_days);

    /*
     * The rest of the code assume persistent file has been specified
     */

    if (!mnand_is_block_info_available(&chip)) {
        uint64_t cur_age;
        double avg_age;
        /* Try to figure out #blks available to do the conversion if in need */
        mnand_status = mnand_extract_age_info(&chip, MNAND_MLC_BLOCK, &cur_age, &blk_count, &avg_age);
        if (mnand_status != MNAND_OK) {
            VERBOSE_PRINTF(0, "Failed to extract mNAND age info (%d)\n", mnand_status);
            goto exit_out;
        }
    }

recalc:

    clock_gettime(CLOCK_MONOTONIC, &ts);
    cur_time = start_time + (ts.tv_sec - base_time); /* Getting the cur_time */

    /* Try to load info from existing file */
    if (load_info(file_path, &info_list, blk_count)) {
        VERBOSE_PRINTF(0, "Could not find valid stored info.\n");
        valid_stored_info = 0;
        if (catchup_mode) {
            VERBOSE_PRINTF(0, "Catch-up mode requires valid historic data.\n");
            res = 1;
            goto exit_out;
        } else
            VERBOSE_PRINTF(0, "Using defaults.\n");
    } else {
        VERBOSE_PRINTF(1, "Loaded %d data points from file\n", count_info(&info_list));
        valid_stored_info = 1;
        dump_info(&info_list);
    }

    /* Extract refresh progress from mNAND */
    mnand_status = mnand_get_refresh_progress(&chip, &cur_rfsh_progress);
    if (mnand_status != MNAND_OK) {
        VERBOSE_PRINTF(1, "Failed to extract mNAND refresh progress (%d)\n", mnand_status);
        goto exit_out;
    }
    VERBOSE_PRINTF(1, "Refresh progress before refresh: %.6f%%.\n", cur_rfsh_progress);

    /* Add to linked list */
    VERBOSE_PRINTF(1, "Adding info: %.6f%%\n", cur_rfsh_progress);
    res = add_info(&info_list, cur_time, cur_rfsh_progress, count_info(&info_list) > 1);
    dump_info(&info_list);

    if (!valid_stored_info) {
       /* Catch up per hour progress first */
       desired_progress = cur_rfsh_progress +
            DEFAULT_REFRESH_PERCENTAGE_PER_SEC(refresh_in_days, 100) * (chip.rfsh_properties.def_rfsh_interval / 1000);
       VERBOSE_PRINTF(1, "Total refresh in days: %d\n", refresh_in_days);
       VERBOSE_PRINTF(1, "Default desired progress: %.6f%%\n", desired_progress);
    } else {
       desired_progress = calculate_desired_progress(refresh_in_days, &info_list, (double)cur_rfsh_progress, cur_time);
       VERBOSE_PRINTF(1, "Desired progress: %.6f%%\n", desired_progress);
    }

    /* Put a cap on maximum refresh progress */
    desired_progress = fmin(desired_progress, cur_rfsh_progress + 100.0);

    if (catchup_mode) {
        /* Refresh blocks (in "catch-up" mode) */
        uint32_t rfsh_delay = max(chip.rfsh_properties.shutdown_rfsh_interval, catchup_rfsh_interval);

        VERBOSE_PRINTF(1, "Time limit: %d sec\n", max_time);
        VERBOSE_PRINTF(1, "Delay between refreshes (catch-up): %d ms\n", rfsh_delay);
        VERBOSE_PRINTF(1, "Refresh started. Ctrl + C or slay command to abort early.\n");
        if ((double)cur_rfsh_progress < desired_progress)
            res = refresh_loop(&chip, rfsh_delay, max_time * 1000, desired_progress);
        else
            VERBOSE_PRINTF(1, "On or ahead of schedule.\n");
    } else {
        /* Refresh blocks (in "session" mode) */
        if ((double)cur_rfsh_progress < desired_progress) {
            uint32_t rfsh_delay;

            rfsh_delay = session_rfsh_interval ?
                (session_rfsh_interval * 1000) : chip.rfsh_properties.min_rfsh_interval;
            VERBOSE_PRINTF(1, "Delay between refreshes (session): %d ms\n", rfsh_delay);

            res = refresh_loop(&chip, rfsh_delay, 0, desired_progress);
        }
        if ((s_stop == 0) && (res >= 0)) {
            res = save_info(file_path, &info_list, MAX_LINES_IN_FILE);
            if (res) {
                printf("Failed to save persistent data\n");
                goto exit_out;
            }
            if (session_interval == 0)
                session_interval = chip.rfsh_properties.def_rfsh_interval / 1000;
            VERBOSE_PRINTF(1, "On or ahead of schedule (session), wait for %d secs\n",
                session_interval);
            sleep(session_interval);

            if (s_stop == 0)
                goto recalc;
        }
    }

    /* Extract MLC refresh progress from mNAND after refresh */
    mnand_status = mnand_get_refresh_progress(&chip, &cur_rfsh_progress);
    if (mnand_status != MNAND_OK) {
        VERBOSE_PRINTF(0, "Failed to extract mNAND refresh progress (%d)\n", mnand_status);
        goto exit_out;
    }
    VERBOSE_PRINTF(1, "Refresh progress after refresh: %.6f%%.\n", cur_rfsh_progress);

    /*
     * Add to linked list only in case date has changed (in rare case tool
     * ran refresh cycle over actual date change).
     * Usually we want to store only the age before the refresh cycle.
     */
    res = add_info(&info_list, cur_time, cur_rfsh_progress, count_info(&info_list) > 1);
    if (res) {
        VERBOSE_PRINTF(0, "Failed to add info\n");
        goto exit_out;
    }
    dump_info(&info_list);

    /* Update file content (if valid date was provided) */
    res = save_info(file_path, &info_list, MAX_LINES_IN_FILE);
    if (res) {
        VERBOSE_PRINTF(0, "Failed to save persistent data\n");
        goto exit_out;
    }

    res = 0;

exit_out:

    if (res)
        printf("res = %d\n", res);

    if (mnand_bound)
        mnand_unbind(&chip);

    if (info_list)
        free_info(&info_list);

    if (catchup_mode) {
        int fd = -1;

#ifdef QNX
        if (cur_cmdlog != cmdlog) {
            /* Set verbosity for mNAND device to the original level*/
            if (devctl(mnand_fd, DCMD_MMCSD_SET_CMDLOG, &cur_cmdlog, sizeof(cur_cmdlog), NULL) != EOK)
                printf("Failed to set verbosity to the original level\n");
            else
                VERBOSE_PRINTF(1, "mNAND device verbosity reset to %u\n", cur_cmdlog);
        }
#endif

        /*
         * Notify we are done. Do it only when tool used in catch-up mode
         */
        fd = open(MNAND_RFSH_FINISHED, O_CREAT | O_RDWR, 0644);
        if (fd >= 0)
            close(fd);
    }

    if (mnand_fd >= 0)
        close(mnand_fd);

    return res;
}
