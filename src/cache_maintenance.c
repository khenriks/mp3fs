/*
 * Cache maintenance for ffmpegfs
 *
 * Creates a POSIX timer that starts the cache maintenance in preset
 * intervals. To ensure that only one instance of ffmpegfs cleans up
 * the cache a shared memory area and a named semaphore is also created.
 *
 * The first ffmpegfs process acts as master, all subsequently started
 * instances will be clients. If the master process goes away one of
 * the clients will automatically take over as master.
 *
 * Copyright (c) 2017 by Norbert Schlia (nschlia@oblivion-software.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ffmpeg_utils.h"
#include "cache_maintenance.h"
#include "transcode.h"

#include <signal.h>
#include <unistd.h>
#include <sys/shm.h>        /* shmat(), IPC_RMID        */
#include <semaphore.h>      /* sem_open(), sem_destroy(), sem_wait().. */

#define CLOCKID         CLOCK_REALTIME
#define SIGMAINT        SIGRTMIN

#define SEM_OPEN_FILE   "/" PACKAGE_NAME "_04806785-b5fb-4615-ba56-b30a2946e80b"

static sigset_t mask;
static timer_t timerid;

static sem_t * sem;
static int shmid;
static pid_t *pid_master;
static int master;

static void maintenance_handler(int sig, __attribute__((unused)) siginfo_t *si, __attribute__((unused)) void *uc);
static int start_timer(time_t interval);
static int stop_timer();
static int link_up();
static void master_check();
static int link_down();

static void maintenance_handler(int sig, __attribute__((unused)) siginfo_t *si, __attribute__((unused)) void *uc)
{
    if (sig != SIGMAINT)
    {
        // Wrong signal. Should never happen.
        return;
    }

    master_check();

    if (master)
    {
        ffmpegfs_info("Running periodic cache maintenance.");
        //transcoder_cache_maintenance();
    }
}

static int start_timer(time_t interval)
{
    struct sigevent sev;
    struct itimerspec its;
    long long freq_nanosecs;
    struct sigaction sa;
    char cache_maintenance[100];

    format_time(cache_maintenance, sizeof(cache_maintenance), interval);

    freq_nanosecs = interval * 1000000000LL;

    ffmpegfs_info("Starting maintenance timer with %speriod.", cache_maintenance);

    // Establish maintenance_handler for timer signal
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = maintenance_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGMAINT, &sa, NULL) == -1)
    {
        ffmpegfs_error("start_timer(): sigaction failed: %s", strerror(errno));
        return -1;
    }

    // Block timer signal temporarily
    sigemptyset(&mask);
    sigaddset(&mask, SIGMAINT);
    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1)
    {
        ffmpegfs_error("start_timer(): sigprocmask(SIG_SETMASK) failed: %s", strerror(errno));
        return -1;
    }

    // Create the timer
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGMAINT;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCKID, &sev, &timerid) == -1)
    {
        ffmpegfs_error("start_timer(): timer_create failed: %s", strerror(errno));
        return -1;
    }

    // Start the timer
    its.it_value.tv_sec = freq_nanosecs / 1000000000;
    its.it_value.tv_nsec = freq_nanosecs % 1000000000;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timerid, 0, &its, NULL) == -1)
    {
        ffmpegfs_error("start_timer(): timer_settime failed: %s", strerror(errno));
        return -1;
    }

    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)
    {
        ffmpegfs_error("start_timer(): sigprocmask(SIG_UNBLOCK) failed: %s", strerror(errno));
    }

    ffmpegfs_trace("Maintenance timer started successfully.");

    return 0;
}

static int stop_timer()
{
    ffmpegfs_info("Stopping maintenance timer.");

    if (timer_delete(timerid) == -1)
    {
        ffmpegfs_error("stop_timer(): timer_delete failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int link_up()
{
    key_t shmkey;

    ffmpegfs_info("Activating " PACKAGE " inter-process link.");

    // initialise a shared variable in shared memory
    shmkey = ftok ("/dev/null", 5);     // valid directory name and a number

    if (shmkey == -1)
    {
        ffmpegfs_error("link_up(): ftok error %s", strerror(errno));
        return -1;
    }

    // First try to open existing memory.
    shmid = shmget (shmkey, sizeof (pid_t), S_IRUSR | S_IWUSR | S_IRGRP |S_IWGRP);
    if (shmid != -1)
    {
        // Shared memory already exists, seems we are client.
        master = 0;
    }
    else
    {
        // Ignore error at first, try to create memory.
        shmid = shmget (shmkey, sizeof (pid_t), IPC_CREAT | S_IRUSR | S_IWUSR | S_IRGRP |S_IWGRP);
        if (shmid != -1)
        {
            // Shared memory freshly created, seems we are master.
            master = 1;
        }
        else
        {
            ffmpegfs_error("link_up(): shmget error %s", strerror(errno));
            return -1;
        }
    }

    pid_master = (pid_t *) shmat (shmid, NULL, 0);   // attach pid_master to shared memory

    if (master)
    {
        *pid_master = getpid();
        ffmpegfs_info("Process with PID %i is now master.", *pid_master);
    }
    else
    {
        ffmpegfs_info("Process with PID %i is now client, master is PID %i.", getpid(), *pid_master);
    }

    // Also create inter-process semaphore.
    // First try to open existing semaphore.
    sem = sem_open(SEM_OPEN_FILE, 0, 0, 0);

    if (sem == SEM_FAILED)
    {
        if (errno == ENOENT)
        {
            // If semaphore does not exist, then try to create one.
            sem = sem_open((const char *)SEM_OPEN_FILE, O_CREAT | O_EXCL, 0777, 1);
        }

        if (sem == SEM_FAILED)
        {
            ffmpegfs_error("link_up(): sem_open error %s", strerror(errno));
            link_down();
            return -1;
        }
    }

    return 0;
}

static void master_check()
{
    pid_t pid_self = getpid();

    if (*pid_master == pid_self)
    {
        ffmpegfs_trace("PID %i is already master.", pid_self);
        return;
    }

    sem_wait(sem);

    // Check if master process still exists
    int master_running = (getpgid(*pid_master) >= 0);

    ffmpegfs_trace("Master with PID %i is %s running.", *pid_master, master_running ? "still" : "NOT");

    if (!master_running)
    {
        ffmpegfs_info("Master with PID %i is gone. PID %i taking over as new master.", *pid_master, pid_self);

        // Register us as master
        *pid_master = pid_self;
        master = 1;
    }

    sem_post(sem);
}

static int link_down()
{
    struct shmid_ds buf;
    int ret = 0;

    ffmpegfs_info("Shutting " PACKAGE " inter-process link down.");

    if (sem_close(sem))
    {
        ffmpegfs_error("link_down(): sem_close error %s", strerror(errno));
        ret = -1;
    }

    // shared memory detach
    if (shmdt (pid_master))
    {
        ffmpegfs_error("link_down(): shmdt error %s", strerror(errno));
        ret = -1;
    }

    if (shmctl(shmid, IPC_STAT, &buf))
    {
        ffmpegfs_error("link_down(): shmctl error %s", strerror(errno));
        ret = -1;
    }
    else
    {
        if (!buf.shm_nattch)
        {
            if (shmctl (shmid, IPC_RMID, 0))
            {
                ffmpegfs_error("link_down(): shmctl error %s", strerror(errno));
                ret = -1;
            }

            // unlink prevents the semaphore existing forever
            // if a crash occurs during the execution
            if (sem_unlink(SEM_OPEN_FILE))
            {
                ffmpegfs_error("link_down(): sem_unlink error %s", strerror(errno));
                ret = -1;
            }
        }
    }

    return ret;
}

int start_cache_maintenance(time_t interval)
{
    // Start link
    if (link_up() == -1)
    {
        return -1;
    }

    // Now start timer
    return start_timer(interval);
}

int stop_cache_maintenance()
{
    int ret = 0;

    // Stop timer first
    if (stop_timer() == -1)
    {
        ret = -1;
    }

    // Now shut down link
    if (link_down() == -1)
    {
        ret = -1;
    }

    return ret;
}
