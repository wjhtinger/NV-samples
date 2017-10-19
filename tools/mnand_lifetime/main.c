/*
 * Copyright (c) 2012-2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <signal.h>
#include <nvmnand.h>
#include "common.h"

/* Maximum allowed aging pace is 1000 P/E cycles per week */
#define MNAND_PACE_CYCLES_DEF       1000.0
#define MNAND_PACE_HOURS_DEF        (7 * 24)

#define COMMAND_INPUT               0                       // Interactive mode.
#define FILE_INPUT                  1                       // File Input mode.

#define KB                          1024                    // Kilo byte
#define MB                          (1 * KB * KB)           // Mega byte
#define GB                          (1 * MB * KB)           // Giga byte

#define DATA_SIZE                   128                     // Chunk data to be written at one to the file. Set to 128KB.
#define CHUNK_SIZE                  (DATA_SIZE * KB)        // Currently : 128KB.

#define NUM_THREADS                 100                     // Number of write invocations. Set to 100.
#define LINE_LENGTH                 150                     // Length of input command

#define MAX_YEARS                    15

#define MAX_FILES_TO_DELETE_DISK_FULL  5                    // Number of files to be deleted in case disk becomes full.
#define MAX_FILES_TO_DELETE_NO_ENTRY   100                  // Number of files to be deleted in case no more entries in directory

#define CHUNK_TO_WRITE_PER_THREAD   (1024 * 1024)           // Number of bytes to write in a thread before relinquishing to next

FILE *g_logFp = NULL;                                       // Global Log file descriptor

static uint64_t s_chunk_size = CHUNK_SIZE;
static char *s_buffer = NULL;                               // Random data buffer
static int s_stop = 0;
static int s_sync_write = 0;                                // Global sync on write
int g_verbosity = 0;                                        // Global verbosity level.
int s_debug = 0;                                            // Debug mode

double s_mlc_age[MAX_YEARS + 1] = {0};                      // Array for storing mNAND Avg MLC age. First entry is age
double s_slc_age[MAX_YEARS + 1] = {0};                      // Array for storing mNAND Avg SLC age. First entry is age
                                                            // at the beginning of execution

static pthread_mutex_t s_pace_check = PTHREAD_MUTEX_INITIALIZER;         // Mutex to lock access to pace calculatinf function.

static pthread_mutex_t s_mutex_file = PTHREAD_MUTEX_INITIALIZER; // Mutex to coordinate file writes
static pthread_cond_t s_cond_file = PTHREAD_COND_INITIALIZER; // Cond var to coordinate file writes
static int s_active_thread_idx = -1; // Current thread allowed to write (condition variable)

/* Command Structure for the write invocation */
struct query {
    char mount_point[PATH_MAX];
    int num_files;
    int current_file;
    uint64_t filesize;
    char prefix[30];
    int thread_index;
    int active;
    uint64_t chunk_to_write;
    uint64_t bytes_written_now;
    uint64_t bytes_to_write;
    int raw_mode;
    int sync_write;
};
struct query queries[NUM_THREADS] = {{ "" }};

static mnand_chip s_chip;    // Handle for the mnand device

/* Variables for main thread (tracking aging pace). */
static double s_cur_slc_avg_age;
static double s_cur_mlc_avg_age;
static double s_target_max_pace;
static double s_target_min_pace;

static int s_year_tracker = 0;                                        // Variable to track years.
static int s_thread_counter = 0;                                      // Number of Threads i.e no. of write invocations.

static int s_num_years = MAX_YEARS;                                   // Number of years for simulation. Default set to 15.

/* Dump to log file as well as Terminal. */
#define PRINTF_DUMP(g_logFp, ...) \
    do { \
            fprintf(stdout, __VA_ARGS__); \
            if (g_logFp) \
                fprintf(g_logFp, __VA_ARGS__); \
    } while (0)

#define PRINTF_DUMP_DEBUG(g_logFp, ...) \
    do { \
        if (s_debug) \
            PRINTF_DUMP(g_logFp, __VA_ARGS__); \
    } while (0)

/* If mount point full, delete some files.
 * Except the current opened file and ., .. entries. */
static int mount_point_delete(struct query *Query, int max_files_to_delete)
{
    DIR* dirp;
    struct dirent* direntp;
    int fd;
    int num_files_deleted = 0;
    char filename[PATH_MAX];

    dirp = opendir(Query->mount_point);

    if(dirp != NULL) {
        for(;;) {
            direntp = readdir(dirp);
            if (direntp == NULL) break;
            /* Dont harm current, parent directory entries and boot entries. */
            if (!strcmp(direntp->d_name, ".") || !strcmp(direntp->d_name, "..") ||
                strstr(direntp->d_name, "boot"))
                continue;

             /* If disk full or file exceeds max size truncate to zero size. Deleting
              * it may cause other threads using the file not writing anything. */

            strcpy(filename, Query->mount_point);          // Get File Path.
            strcat(filename, "/");
            strcat(filename, direntp->d_name);

            fd = open(filename, O_RDWR);
            if (fd < 0) {
                continue;                                  // File already locked.
            }
            if (unlink(filename) == 0) {
                PRINTF_DUMP(g_logFp, "File: %s deleted for freeing disk space\n", filename);
                num_files_deleted++;
            }
            close(fd);
            if (num_files_deleted == max_files_to_delete)
                break;
        }
    }

    closedir(dirp);
    return num_files_deleted;
}

/* Generate Random data filled buffer. */
static void generate_random_data_buffer(void)
{
    uint32_t i;
    for (i=0; i<s_chunk_size; i++)
        s_buffer[i] = random()%256;
}

static void show_progress(void)
{
    int i;
    int res;
    mnand_life_time_info life;

    for (i = 0; i < s_thread_counter; i++) {
        PRINTF_DUMP_DEBUG(g_logFp, "*** curfile %d, numfiles %d, filesize %"PRIu64", bytes_to_write %"PRIu64"\n",
                queries[i].current_file, queries[i].num_files,
                queries[i].filesize, queries[i].bytes_to_write);
        PRINTF_DUMP(g_logFp, "Thread %2d: %7.3f%% (%6d of %6d files, each with %11"PRIu64" bytes)\n",
            i,
            queries[i].active ?
                ((double)queries[i].current_file / queries[i].num_files +
                 (double)(queries[i].filesize - queries[i].bytes_to_write) / queries[i].filesize / queries[i].num_files
                ) * 100.0 : 100.0,
            queries[i].current_file,
            queries[i].num_files,
            queries[i].filesize);
    }

    pthread_mutex_lock(&s_pace_check);                      // Get lock to access the pace calc fucntion
    /* Extract current total MLC age from mNAND */
    res = get_current_age_calc_pace(&s_chip, &s_cur_mlc_avg_age, s_target_min_pace,
        s_target_max_pace, MNAND_MLC_BLOCK);
    if (res == 0)
        res = get_current_age_calc_pace(&s_chip, &s_cur_slc_avg_age, s_target_min_pace,
            s_target_max_pace, MNAND_SLC_BLOCK);
    if (res == 0)
        res = (mnand_extract_life_time_info(&s_chip, &life) == MNAND_OK) ?
            0 : 1;
    if (res)
        PRINTF_DUMP(g_logFp, "Error. res = %d\n", res);
    else {
        if (life.life_time_type == MNAND_LIFETIME_IN_BLOCK)
            PRINTF_DUMP(g_logFp, "\tCurrent spare block count: %d\n", life.life_time.spare_blocks);
        else if (life.life_time_type == MNAND_LIFETIME_IN_PERCENT)
            PRINTF_DUMP(g_logFp, "\tCurrent life time: %.3f%%\n", life.life_time.percentage);
    }

    pthread_mutex_unlock(&s_pace_check);                      // Release LOck
}

static void wait_our_turn(struct query *Query)
{
    int err;
    err = pthread_mutex_lock(&s_mutex_file);
    if (err != 0)
        PRINTF_DUMP(g_logFp, "mutex lock error for file :%s\n", strerror(err));
    while (!s_stop && s_active_thread_idx != Query->thread_index)
        pthread_cond_wait(&s_cond_file, &s_mutex_file);
}

static void done_our_turn(struct query *Query, int unlock_mutex)
{
    int count = s_thread_counter;
    int i = Query->thread_index + 1;

    while (count--) {
        if (i >= s_thread_counter) {
            if (!s_stop)
                show_progress();
            i = 0; /* Wrap */
        }
        if (queries[i].active) {
            PRINTF_DUMP_DEBUG(g_logFp, "t%d line %d: pass to %d\n",
                Query->thread_index, __LINE__, i);
            s_active_thread_idx = i; /* pass control to next active thread */
            break;
        }
        i++;
    }
    if (unlock_mutex)
        pthread_mutex_unlock(&s_mutex_file);
    pthread_cond_broadcast(&s_cond_file); /* wake up all threads */
}

/* Write data to a file of size specified in a query in a year.*/
static int file_write(int fd, struct query *Query)
{
    uint64_t chunk;
    int bytes_written;

    // Write Data to file. Size to be written Query->filesize.
    while (!s_stop && Query->bytes_to_write &&
            Query->bytes_written_now < Query->chunk_to_write) {
        chunk = Query->bytes_to_write > s_chunk_size ? s_chunk_size : Query->bytes_to_write;
        bytes_written = write(fd, (void *)s_buffer, chunk);
        if (Query->sync_write)
            fsync(fd);
        PRINTF_DUMP_DEBUG(g_logFp,
            "t%d line %d: tw %"PRIu64" wrtn %d\n", Query->thread_index, __LINE__,
            Query->bytes_to_write, bytes_written);
        /*
         * If no space in Disk/Partition or if file reached max size
         * restriction within a process, make some disk space free.
         */
        if (bytes_written < 0) {
            /*
             * This may happen, for instance, if file larger than partition.
             * In that case, we simply return an error. The caller will take
             * care of closing and recreating the file to write only the left
             * over portion.
             */
            PRINTF_DUMP_DEBUG(g_logFp, "%d: Errno %d (%s)\n", __LINE__,
                errno, strerror(errno));
            return -1;
        } else if ((uint64_t)bytes_written < chunk) {
            if (Query->raw_mode) {
                /* Should not happen but print debug message anyway */
                PRINTF_DUMP_DEBUG(g_logFp, "%d: raw write less than expectedi %d vs %" PRIu64 "\n", __LINE__,
                    bytes_written, chunk);
            } else {
                /*
                 * If not even one file is deleted, reset the file pointer of
                 * the current file to the beginning.
                 */
                if (mount_point_delete(Query, MAX_FILES_TO_DELETE_DISK_FULL) == 0)
                    lseek(fd, 0, SEEK_SET);
            }
        }
        pace_delay();
        /*
         * Decrement bytes_to_write by number of bytes written. Handles
         * case where disk is full and some bytes have been written.
         */
        Query->bytes_to_write -= (bytes_written != -1) ? (uint64_t)bytes_written : 0;
        Query->bytes_written_now += (bytes_written != -1) ? (uint64_t)bytes_written : 0;
    }
    return 0;
}

/* Each independent write query calls this thread function.
 * Loops for num_files times and write filesize amount of data. */
static void *mount_point_write_thread(void *qry)
{
    int fd = -1;
    int new_file = 1;
    char filename[PATH_MAX];
    struct query *Query;
    int error = 0;
    int res;
    int continue_with_current_th = 0;

    Query = (struct query *)qry;
    pthread_mutex_lock(&s_mutex_file);
    Query->active = 1; /* Mark this thread as active */
    Query->bytes_written_now = 0;
    Query->current_file = 0;
    Query->bytes_to_write = (uint64_t)Query->filesize;
    pthread_mutex_unlock(&s_mutex_file);
    if (g_verbosity > 0) {
        if (Query->raw_mode)
            PRINTF_DUMP(g_logFp, "Thread %d: total iterations %d, each with %"PRIu64" bytes at %s\n",
                Query->thread_index, Query->num_files, Query->filesize, Query->mount_point);
        else
            PRINTF_DUMP(g_logFp, "Thread %d: creating %d files, each with %"PRIu64" bytes at %s\n",
                Query->thread_index, Query->num_files, Query->filesize, Query->mount_point);
    }
    wait_our_turn(Query);
    pthread_mutex_unlock(&s_mutex_file); /* We should enter loop with mutex unlocked */
    while (!s_stop && Query->current_file < Query->num_files) {
        if (Query->raw_mode) {
            if (fd < 0) {
                fd = open(Query->mount_point, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
                if (fd < 0) {
                    PRINTF_DUMP(g_logFp, "Thread %d: Failed to open device node (%s). errno %d (%s). Aborting.\n",
                        Query->thread_index, Query->mount_point, errno, strerror(errno));
                    error = 1;
                    break;
                }
            }
        } else {
            if (new_file) {
                sprintf(filename,"%s/%s_y%02d_t%02d_n%04d.bin", Query->mount_point, Query->prefix,
                    s_year_tracker, Query->thread_index, Query->current_file);
                do {
                    fd = open(filename, O_RDWR | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
                    if (fd < 0) {
                        if (errno == ENOSPC) {
                            PRINTF_DUMP(g_logFp, "Thread %d: No space to create new file. Freeing up space...\n",
                                    Query->thread_index);
                            if (mount_point_delete(Query, MAX_FILES_TO_DELETE_NO_ENTRY) == 0) {
                                PRINTF_DUMP(g_logFp, "Thread %d: Unable to free up space. Aborting.\n",
                                        Query->thread_index);
                                error = 1;
                                break;
                            }
                        }
                        else {
                            PRINTF_DUMP(g_logFp, "Thread %d: Failed to create file. errno %d (%s). Aborting.\n",
                                    Query->thread_index, errno, strerror(errno));
                            error = 1;
                            break;
                        }
                    }
                } while (fd < 0);
                if (error)
                    break;
                if (g_verbosity > 1)
                    PRINTF_DUMP(g_logFp, "Thread %d: creating %s with %"PRIu64" bytes\n",
                        Query->thread_index, filename, Query->bytes_to_write);
                new_file = 0;
            }
        }
        if (Query->bytes_written_now == 0 && continue_with_current_th == 0)
            wait_our_turn(Query);
        res = file_write(fd, Query);
        if (res < 0) {
            /*
             * Special case: error during write file maybe due to lack of space.
             * Action: close file and set new_file. This will cause the loop
             * to re-create the file and continue writing only bytes_to_write
             * left over.
             */
            fsync(fd);
            close(fd);
            fd = -1;
            if (!Query->raw_mode) {
                res = unlink(filename);
                PRINTF_DUMP(g_logFp, "Thread %d: Erased %s to make space\n",
                    Query->thread_index, filename);
                if (mount_point_delete(Query, MAX_FILES_TO_DELETE_DISK_FULL) == 0)
                    PRINTF_DUMP(g_logFp, "Failed to erase more files\n");
                new_file = 1;
            }
        } else if (Query->bytes_to_write == 0) {
            close(fd);
            fd = -1;
            Query->current_file++;
            Query->bytes_to_write = (uint64_t)Query->filesize;
            if (!Query->raw_mode)
                new_file = 1;
        }
        if (!s_stop &&
            Query->current_file < Query->num_files &&
            Query->bytes_written_now >= Query->chunk_to_write) {
            if (g_verbosity > 0) {
                PRINTF_DUMP(g_logFp, "Thread %d: done writing %" PRIu64 " bytes, relinquishing to next thread\n",
                    Query->thread_index, Query->chunk_to_write);
            }
            done_our_turn(Query, 1);
            Query->bytes_written_now = 0;
        } else if (Query->bytes_written_now == 0)
            continue_with_current_th = 1;
    }
    if (!s_stop && g_verbosity > 0 && !error) {
        if (Query->raw_mode)
            PRINTF_DUMP(g_logFp, "Thread %d: done iteration %d with %"PRIu64" bytes at %s\n",
                Query->thread_index, Query->num_files, Query->filesize, Query->mount_point);
        else
            PRINTF_DUMP(g_logFp, "Thread %d: finished creating %d files with %"PRIu64" bytes at %s\n",
                Query->thread_index, Query->num_files, Query->filesize, Query->mount_point);
    } else if (s_stop)
        PRINTF_DUMP(g_logFp, "Thread %d: stopping\n", Query->thread_index);
    else if (error)
        PRINTF_DUMP(g_logFp, "Thread %d: stopping due to error\n", Query->thread_index);
    Query->active = 0; /* Thread now inactive */
    done_our_turn(Query, error ? 0 : 1);

    return NULL;
}

/* Print yearly status. */
static void get_yearly_status(mnand_chip *chip)
{
    int res = 1;
    mnand_life_time_info life;

    pthread_mutex_lock(&s_pace_check);                      // Get lock to access the pace calc fucntion

    /* Extract current total MLC age and life time info from mNAND */
    res = get_current_age_calc_pace(chip, &s_cur_mlc_avg_age, s_target_min_pace,
        s_target_max_pace, MNAND_MLC_BLOCK);
    if (res == 0)
        res = get_current_age_calc_pace(chip, &s_cur_slc_avg_age, s_target_min_pace,
        s_target_max_pace, MNAND_SLC_BLOCK);
    if (res == 0)
        res = (mnand_extract_life_time_info(&s_chip, &life) == MNAND_OK) ?
            0 : 1;

    pthread_mutex_unlock(&s_pace_check);                      // Release LOck

    if (res) {
        PRINTF_DUMP(g_logFp, "Error. res = %d\n", res);
        return;
    }

    s_mlc_age[s_year_tracker] = s_cur_mlc_avg_age;               // Save current years Avg MLC age.
    s_slc_age[s_year_tracker] = s_cur_slc_avg_age;               // Save current years Avg SLC age.

    if (s_year_tracker == 0)
        PRINTF_DUMP(g_logFp, "P/E Cycle Values at the beginning:\n\n");
    else
        PRINTF_DUMP(g_logFp, "P/E Cycle Values at the End of Year: %d\n\n", s_year_tracker);
    PRINTF_DUMP(g_logFp, "\tCurrent Avg MLC Age : %.3lf\n", s_mlc_age[s_year_tracker]);
    PRINTF_DUMP(g_logFp, "\tCurrent Avg SLC Age : %.3lf\n", s_slc_age[s_year_tracker]);
    if (s_year_tracker > 0) {
        PRINTF_DUMP(g_logFp, "\tDifference of Avg MLC Age of %dth year with beginning: %lf\n", s_year_tracker, (s_mlc_age[s_year_tracker] - s_mlc_age[0]));
        PRINTF_DUMP(g_logFp, "\tDifference of Avg SLC Age of %dth year with beginning: %lf\n", s_year_tracker, (s_slc_age[s_year_tracker] - s_slc_age[0]));
    }
    if (life.life_time_type == MNAND_LIFETIME_IN_BLOCK)
        PRINTF_DUMP(g_logFp, "\tCurrent spare block count: %d\n", life.life_time.spare_blocks);
    else if (life.life_time_type == MNAND_LIFETIME_IN_PERCENT)
        PRINTF_DUMP(g_logFp, "\tCurrent life time: %.3f%%\n", life.life_time.percentage);
    PRINTF_DUMP(g_logFp, "\n");
}

/* Parse line and store the query parameters. */
static int parse(char line[], struct query *Query)
{
    char *str_ptr, *end;
    enum filesize_type {sb = 0, kb = 1, mb = 2, gb = 3} type = sb;  // Select type of filesize given in the query.
    int duplicate = 0;                                              // Check whether -d and -y both are specified in a query.
    char delims[] = " ,";

    Query->chunk_to_write = CHUNK_TO_WRITE_PER_THREAD; // Default number of bytes to write before relinquishing thread
    Query->raw_mode = 0; /* initialize it to be 0 */
    Query->sync_write = s_sync_write; /* initialize it based on global flag first */

    str_ptr = strtok(line, delims);
    while (str_ptr != NULL) {
        if (!strcmp(str_ptr, "-m")) {
            str_ptr = strtok(NULL, delims);
            if (str_ptr)
                strcpy(Query->mount_point, str_ptr);
        } else if (!strcmp(str_ptr, "-r")) {
            str_ptr = strtok(NULL, delims);
            if (str_ptr) {
                strcpy(Query->mount_point, str_ptr);
                Query->raw_mode = 1;
            }
        } else if (!strcmp(str_ptr, "-d")) {
            str_ptr = strtok(NULL, delims);
            if (str_ptr)
                Query->num_files = atoi(str_ptr)*365;
            str_ptr = strtok(NULL, delims);
            if (str_ptr)
                Query->filesize = (uint64_t)strtoull(str_ptr, &end, 0);
            duplicate++;
        } else if (!strcmp(str_ptr, "-y")) {
            str_ptr = strtok(NULL, delims);
            if (str_ptr)
                Query->num_files = atoi(str_ptr);
            str_ptr = strtok(NULL, delims);
            if (str_ptr)
                Query->filesize = (uint64_t)strtoull(str_ptr, &end, 0);
            duplicate++;
        } else if (!strcmp(str_ptr, "-f")) {
            str_ptr = strtok(NULL, delims);
            if (str_ptr)
                Query->chunk_to_write = ((uint64_t)atoi(str_ptr)) * MB;
        } else if (!strcmp(str_ptr, "-p")) {
            str_ptr = strtok(NULL, delims);
            if (str_ptr)
                strcpy(Query->prefix, str_ptr);
        } else if (!strcmp(str_ptr, "-s")) {
            Query->sync_write = 1;
        } else if (!strcmp(str_ptr, "-K")) {
            type = kb;
        } else if (!strcmp(str_ptr, "-M")) {
            type = mb;
        } else if (!strcmp(str_ptr, "-G")) {
            type = gb;
        } else {
            PRINTF_DUMP(g_logFp, "\tInvalid Usage. !!\n");
            return -1;
        }
        str_ptr = strtok(NULL, delims);
    }

    /* If error, show usage mode */
    if (Query->mount_point[0] == '\0' || Query->num_files == 0 ||
        duplicate != 1 || Query->filesize == 0 || (Query->raw_mode == 0 && Query->prefix[0] == '\0')) {
        PRINTF_DUMP(g_logFp, "\tInvalid Usage. !!\n");
        return -1;
    }

    /* Default filesize is in bytes. So, if input is in KB or MB or GB,
     * convert to bytes and store. */
    if (type == kb) {
        Query->filesize *= KB;
    } else if (type == mb) {
        Query->filesize *= MB;
    } else if (type == gb) {
        Query->filesize *= GB;
    }
    return 0;
}

/* Invoking main: Usage function */
static void main_usage(void)
{
    printf("main_usage: mnand_lifetime_test -d <path to mnand device> [-v 0-3] [-t 1-15] [-f file] [-l log file name] [-s]\n\n"
           "   -d mnand_path        Path to mnand device (for instance, /dev/mnand0).\n"
           "   -p cycles,hours      Maximum aging pace expressed in P/E cycles and hours.\n"
           "                        Default is 1000,168 meaning the tool will age at most\n"
           "                        1000 P/E cycles every 168 hours (or 7 days).\n"
           "   -c chunk_size        Chunk of data written to file (in KB). Default %d\n"
           "   -v verbosity         Set verbosity level (0 - 3). Default 0.\n"
           "   -t lifetime age      Lifetime age [1 - 15]. Default 15.\n"
           "   -f filepath          File which has all write commands.\n"
           "   -l log file name     Log file name to dump the statistics.\n"
           "   -s                   Perform fsync on write for all threads\n\n"
           "   -debug               Include debug messages.\n\n", DATA_SIZE);
    exit(1);
}

/* Usage: write to mount point */
static int write_usage(void)
{
    printf("write_usage: -m <Target partition mount point name > || -r <device node> -d <number of files, size of each file> || -y <number of files, size of each file>"
           " -p <prefix for th file generated> [-K || -M || -G]\n\n"
           "   -m Target partition mount point name            Path to Target partition mount point.\n"
           "   -r device node name                             Device node represent the Target partition.\n"
           "   -d <number of files, size of each file>         Number of files and size of each file to be written per day.\n"
           "   -y <number of files, size of each file>         Number of files and size of each file to be written per year.\n"
           "   -p <unique prefix for the file going to get generated>.\n"
           "   -s                                              Perform fsync on write (if global flag is set, it takes precedence)\n"
           "   -f <number of MB>                               Number of MB to write in thread before relinquishing. Default is 1MB.\n"
           "   -K || -M || -G  Filesize type. -K: KB, -M: MB, -G: GB (Default: bytes).\n"
           " Please Note: -d and -y are mutually exclusive and both cannot be given in a single invocation.\n\n");
    return 1;
}

static void sig_handler(int sig_num)
{
    PRINTF_DUMP(g_logFp, "Aborting mnand_lifetime_test.\n");
    s_stop = 1;
}

int main(int argc, char **argv)
{
    pthread_t threads[NUM_THREADS];       // Thread for each write invocation
    int thread_iterator = 0;              // Loop variable for the threads.
    int mode = COMMAND_INPUT;             // Input mode: either interactive or file input.
    int skip = 0;
    int ret;
    FILE *fp;
    char filename[PATH_MAX] = "\0";             // Input file containing all write invocations.
    char logfile[PATH_MAX] = "\0";              // Log file containing all the dump.
    char devnode[PATH_MAX] = "\0";              // Path to mNAND device.
    int target_pace_cycles = MNAND_PACE_CYCLES_DEF;
    int target_pace_hours = MNAND_PACE_HOURS_DEF;
    int res = 0;
    pthread_mutexattr_t mutex_file_attr;

    PRINTF_DUMP(g_logFp, "mNAND Lifetime Test Tool\n");

    /* Extract command line parameters */
    argc--;
    argv++;

    if (!argc)
        main_usage();

    while(argc > 0) {
        skip = 2;
        if (!strcmp(argv[0], "-d")) {
            if(argc < 2)
                main_usage();
            strncpy(devnode, argv[1], sizeof(devnode) - 1);
        } else if (!strcmp(argv[0], "-t")) {
            if(argc < 2)
                main_usage();
            s_num_years = strtoul(argv[1], NULL, 0);
        } else if (!strcmp(argv[0], "-v")) {
            if(argc < 2)
                main_usage();
            g_verbosity = strtoul(argv[1], NULL, 0);
        } else if (!strcmp(argv[0], "-c")) {
            if(argc < 2)
                main_usage();
            s_chunk_size = strtoul(argv[1], NULL, 0) * KB;
        } else if (!strcmp(argv[0], "-p")) {
            char buf[50];
            char *p;
            if (argc < 2)
                main_usage();
            strncpy(buf, argv[1], sizeof(buf));
            p = strtok(buf, ",");
            if (p) {
                target_pace_cycles = strtoul(p, NULL, 0);
                p = strtok(NULL, ",");
                if (p)
                    target_pace_hours = strtoul(p, NULL, 0);
            }
        } else if (!strcmp(argv[0], "-l")) {
            if(argc < 2)
                main_usage();
            strncpy(logfile, argv[1], sizeof(logfile) - 1);
        } else if (!strcmp(argv[0], "-f")) {
            if(argc < 2)
                main_usage();
            strncpy(filename, argv[1], sizeof(filename) - 1);
            mode = FILE_INPUT;
        } else if (!strcmp(argv[0], "-debug")) {
            s_debug = 1;
            skip = 1;
        } else if (!strcmp(argv[0], "-s")) {
            s_sync_write = 1;
            skip = 1;
        } else {
            printf("Invalid paramter: %s\n", argv[0]);
            main_usage();
        }
        argc -= skip;
        argv += skip;
    }

    if (devnode[0] == '\0') {
        printf("mNAND path must be provided\n");
        main_usage();
    }
    if (target_pace_hours == 0) {
        printf("Invalid target pace hours.\n");
        main_usage();
    }
    if (s_num_years < 0 || s_num_years > MAX_YEARS) {
        printf("Invalid life time years.\n");
        main_usage();
    }
    if (s_chunk_size == 0) {
        printf("Invalid chunk size.\n");
        main_usage();
    }

    /* Register signal handlers for graceful termination */
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    /* Open Log file for dumping status. */
    if (strlen(logfile)) {
        g_logFp = fopen(logfile, "w");
        if (g_logFp == NULL) {
            printf("can't open file %s to write\n", logfile);
            res = 0;
            goto out;
        }
    }

    /* Read all write commands in non-interactive case through file input or else via command line. */
    char line[LINE_LENGTH];
    if (mode == FILE_INPUT) {
        fp = fopen(filename, "r");
        if(fp == NULL) {
            printf("Error opening input file: %s\n", filename);
            res = 1;
            goto out;
        }
    } else {
        fp = stdin;
        write_usage();
        PRINTF_DUMP(g_logFp, "\t\t\tEnter Write Commands:\n\nCMD# ");
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        if(line[0] == '\n' || s_thread_counter == NUM_THREADS)
            break;
        if (line[strlen(line) - 1] == '\n')
            line[strlen(line) - 1] = '\0';
        memset(&queries[s_thread_counter], 0, sizeof(struct query));
        ret = parse(line, &queries[s_thread_counter]);
        if (mode == COMMAND_INPUT)
            PRINTF_DUMP(g_logFp, "CMD# ");
        if (ret == -1)
            continue;
        s_thread_counter++;
    }

    if (s_thread_counter == 0) {
       printf("Not provided any write invocation query. Exiting.\n\n");
       res = 0;
       goto out;
    }

     /* Open mNAND device */
    if (mnand_open(devnode, &s_chip) != MNAND_OK) {
        printf("Failed to access/identify mNAND.\n");
        res = 1;
        goto out;
    }

    if ((s_buffer = malloc(s_chunk_size)) == NULL) {
        printf("Failed to allocate buffer memory.\n");
        res = 1;
        goto out;
    }

    pthread_mutexattr_init(&mutex_file_attr);
    pthread_mutexattr_settype(&mutex_file_attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&s_mutex_file, &mutex_file_attr);

    /* Calculate pace limits in p/e cycles per second */
    s_target_max_pace = (double)target_pace_cycles / (target_pace_hours * 3600);
    s_target_min_pace = 0.9 * s_target_max_pace;

    PRINTF_DUMP(g_logFp, "\n\tTarget age specified for simulation: %d\n", s_num_years);

    PRINTF_DUMP(g_logFp, "\tTarget pace between %.3f and %.3f p/e cycles/sec "
            "for max aging %d cycles per %d hours\n",
            s_target_min_pace, s_target_max_pace, target_pace_cycles, target_pace_hours);

    PRINTF_DUMP(g_logFp, "\tNumber of write invocations : %d\n\n", s_thread_counter);

    // Get 0th year status
    get_yearly_status(&s_chip);

    // Generate random data buffer
    generate_random_data_buffer();

    for (s_year_tracker=1; s_year_tracker<=s_num_years; s_year_tracker++) {
        s_active_thread_idx = -1;
        for (thread_iterator=0; thread_iterator<s_thread_counter; thread_iterator++) {
            queries[thread_iterator].thread_index = thread_iterator;
            ret = pthread_create(&threads[thread_iterator], NULL, mount_point_write_thread, (void *) &queries[thread_iterator]);
            assert(0 == ret);
        }

        // Tell write threads to start working (1st thread to write is thread index zero,
        // as defined by s_active_thread_idx).
        s_active_thread_idx = 0;
        sleep(1); // Wait for threads to be waiting
        pthread_cond_broadcast(&s_cond_file);

        for(thread_iterator=0; thread_iterator<s_thread_counter; thread_iterator++) {
            ret = pthread_join(threads[thread_iterator], NULL);
            assert(0 == ret);
        }
        if (s_stop)
            break;
        else
            get_yearly_status(&s_chip);                             // Print Yearly Status
    }

out:

    if (s_buffer)
        free(s_buffer);

    if (g_logFp)
        fclose(g_logFp);

    mnand_close(&s_chip);
    return res;
}
