#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "qemu/error-report.h"
#include "external_sim.h"
#include <limits.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>

/* This is a Linux only feature */

#ifndef _WIN32

/* Semaphores and shared memory segments for coordinating
 * communication with a simulator.
 */
#define NAME_SIZE 100
static char name_str[NAME_SIZE];
static sem_t *sem[3];
static long* buf;
static const size_t memlen = 2*sizeof(long);
static const char* semname[3] = {
    "/qemu_sem_a",
    "/qemu_sem_b",
    "/qemu_sem_c"
};
static const char* memname = "/qemu_mem";

static void init_external_sim(void) {
    int idx, mem_fd;
    void *addr;
    /* Get the shared memory segment that was created by the simulator */
    snprintf(name_str, NAME_SIZE, "%s_%d", memname, getppid());
    mem_fd = shm_open(name_str, O_RDWR, 0);
    if (mem_fd == -1) {
        error_report("shm_open failed");
        exit(0);
    }
    /* Remove the name because we don't need it anymore. Shared memory
     * will be deleted by the OS when this process and the simulator exit. */
    shm_unlink(name_str);
    /* Get the semaphores that were created by the simulator. */
    for (idx = 0; idx < 3; idx++) {
        /* Get the semaphore */
        snprintf(name_str, NAME_SIZE, "%s_%d", semname[idx], getppid());
        sem[idx] = sem_open(name_str, O_RDWR);
        if (sem[idx] == SEM_FAILED) {
            error_report("sem_open failed");
            exit(0);
        }
        /* Remove the name because we don't need it anymore. Semaphore
         * might be deleted by the OS when this process and the simulator
         * exit. Simulator should try to delete it on exit. */
        sem_unlink(name_str);
    }
     /* Map the shared memory segment into our address space. This space
     * has already been created by the simulator. */
    addr = mmap(NULL, memlen, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, 0);
    if (addr == (void*)-1) {
        error_report("mmap failed:");
        exit(0);
    }
    /* We'll be writing and reading longs to the shared memory. */
    buf = (long*)addr;
}

static void handshake_sim(void) {
    /* The simulator is waiting for us to post here after initializing
     * the shared semaphores and memory. This lets it know that we
     * are done getting set up. */
    sem_post(sem[0]);
}

static void run_sim(long *h) {
    /* Wait for the simulator to write a time step to shared memory
     * and then go get that time step value. */
    sem_wait(sem[1]);
    *h = *(buf);
}

static void sync_sim(long e, long h) {
    /* Tell the simulator how large a time step we would like and
     * how long a step we actually took when we ran. */
    *(buf) = h;
    *(buf + 1) = e;
    sem_post(sem[2]);
}

static bool enabled = false, syncing = true;
static int64_t t;
static QEMUTimer *sync_timer;
static QemuMutex external_sim_mutex;
static QemuCond external_sim_cond;

bool external_sim_enabled(void)
{
    return enabled;
}

void external_sim_sync(void)
{
    /* kvm-all.c will call this function before running
     * instructions with kvm. Because syncing will be
     * true while external_sim is waiting for a new time advance
     * from the simulation, no instructions will execute
     * while the machine is supposed to be suspended in
     * simulation time.
     */
    qemu_mutex_lock(&external_sim_mutex);
    while (syncing) {
        qemu_cond_wait(&external_sim_cond, &external_sim_mutex);
    }
    qemu_mutex_unlock(&external_sim_mutex);
}

static void start_emulator(void)
{
    if (kvm_enabled()) {
        /* Setting syncing to false tells kvm-all that
         * it can execute guest instructions.
         */
        qemu_mutex_lock(&external_sim_mutex);
        syncing = false;
        qemu_mutex_unlock(&external_sim_mutex);
        qemu_cond_signal(&external_sim_cond);
        /* Restart the emulator clock */
        cpu_enable_ticks();
    }
}

static void stop_emulator(void)
{
    if (kvm_enabled()) {
        /* Tell the emulator that it is not allowed to
         * execute guest instructions.
         */
        qemu_mutex_lock(&external_sim_mutex);
        syncing = true;
        qemu_mutex_unlock(&external_sim_mutex);
        /* Kick KVM off of the CPU and stop the emulator clock. */
        cpu_disable_ticks();
        kick_all_vcpus();
    }
}

static void schedule_next_event(void)
{
    int64_t h, elapsed;
    /* Report the actual elapsed time to the external simulator. */
    h = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    elapsed = h - t;
    t = h;
    /* Request a time advance and report the elapsed time */
    h = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);
    if (h < 0) {
        h = LONG_MAX;
    }
    sync_sim(elapsed, h);
    /* Get the allowed time advance. This will be less than or
     * equal to the request. */
    run_sim(&h);
    /* Schedule the next synchronization point */
       timer_mod(sync_timer, t + h);
    /* Start advancing cpu ticks and the wall clock */
    start_emulator();
}

static void sync_func(void *data)
{
    /* Stop advancing cpu ticks and the wall clock */
    stop_emulator();
    /* Schedule the next event */
    schedule_next_event();
}

void setup_external_sim(void)
{
    /* The module has been enabled */
    enabled = true;
    if (kvm_enabled()) {
        qemu_mutex_init(&external_sim_mutex);
        qemu_cond_init(&external_sim_cond);
    }
    /* Stop the clock while the simulation is initialized */
    stop_emulator();
    /* Setup the synchronization channel */
    init_external_sim();
    handshake_sim();
    /* Initialize the simulation clock */
    t = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* Start the timer to ensure time warps advance the clock */
    sync_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sync_func, NULL);
    /* Get the time advance that is requested by the simulation */
    schedule_next_event();
}

#else

void setup_external_sim(void)
{
    error_report(stderr, "-external_sim is not supported on Windows, exiting\n");
    exit(0);
}

#endif
