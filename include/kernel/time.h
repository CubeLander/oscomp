#ifndef _KERNEL_TIME_H
#define _KERNEL_TIME_H

#include <kernel/sched/signal.h>
#include <kernel/types.h>

extern uint64 jiffies; /* Current system time in jiffies */
/** defined in <sys/time.h>
 * struct timezone;
 * struct itimerval;
 *
 *
 *
 *
 *
 */

struct superblock;
/* Time limits and constants */
#define TIME_T_MAX ((1UL << ((sizeof(time_t) << 3) - 1)) - 1)
#define NSEC_PER_SEC 1000000000L
#define USEC_PER_SEC 1000000L
#define NSEC_PER_USEC 1000L

/* Clock identifiers */
#define CLOCK_REALTIME 0           /* System-wide real-time clock */
#define CLOCK_MONOTONIC 1          /* Monotonic system-wide clock */
#define CLOCK_BOOTTIME 2           /* Monotonic clock that includes time system was suspended */
#define CLOCK_PROCESS_CPUTIME_ID 3 /* Per-process CPU time clock */

/* Filesystem time range capabilities */
struct timerange {
	time_t min_time;    /* Earliest representable time */
	time_t max_time;    /* Latest representable time */
	uint32 granularity; /* Time granularity in nanoseconds (e.g., 1 for ns, 1000 for us) */
};

int32 do_gettimeofday(struct timeval* tv);
int32 do_clock_gettime(clockid_t which_clock, struct timespec* tp);
void update_sys_time_from_hw(void);
time_t do_time(time_t* timer);

/* Time comparison and manipulation functions */
static inline void timespec_to_timeval(struct timeval* tv, const struct timespec* ts);
static inline void timespec_sub(struct timespec* result, const struct timespec* a, const struct timespec* b);
static inline void timespec_add(struct timespec* result, const struct timespec* a, const struct timespec* b);
static inline int32 timespec_compare(const struct timespec* a, const struct timespec* b);
/*
 * Convert timeval to timespec
 */
static inline void timeval_to_timespec(struct timespec* ts, const struct timeval* tv) {
	ts->tv_sec = tv->tv_sec;
	ts->tv_nsec = tv->tv_usec * NSEC_PER_USEC;
}

/**
 * current_time - Get current system time
 * @sb: Superblock (optional, can be NULL)
 *
 * Returns the current system time as a timespec structure.
 * If a superblock is provided, adjusts the time to respect
 * the filesystem's time range capabilities.
 */
struct timespec current_time(struct superblock* sb);

/**
 * current_time_unix - Get current time in Unix seconds
 * @sb: Superblock (optional, can be NULL)
 *
 * Returns the current time as seconds since the Unix epoch,
 * adjusted for the filesystem's capabilities if a superblock is provided.
 */
time_t current_time_unix(struct superblock* sb);

/**
 * current_fs_time - Get filesystem-specific current time
 * @sb: Superblock for the filesystem
 *
 * Returns the current time formatted according to the filesystem's
 * time representation capabilities.
 */
struct timespec current_fs_time(struct superblock* sb);

/**
 * timespec_trunc - Truncate timespec to specified granularity
 * @ts: The timespec to truncate
 * @granularity: Time granularity in nanoseconds
 *
 * Returns the truncated timespec value.
 */
static inline struct timespec timespec_trunc(struct timespec ts, uint32 granularity) {
	if (granularity == 0 || granularity == 1) return ts; /* No truncation needed */

	ts.tv_nsec = ts.tv_nsec - (ts.tv_nsec % granularity);
	return ts;
}

/* Core time function prototypes */

/*
 * Get current time (CLOCK_REALTIME)
 */
int32 do_gettimeofday(struct timeval* tv);

/*
 * Set system time
 */
int32 do_settimeofday(const struct timeval* tv);

/*
 * Get time from specified clock
 */
int32 do_clock_gettime(clockid_t which_clock, struct timespec* tp);

/*
 * Set time of specified clock
 */
int32 do_clock_settime(clockid_t which_clock, const struct timespec* tp);

/*
 * Get resolution of specified clock
 */
int32 do_clock_getres(clockid_t which_clock, struct timespec* tp);

/*
 * Suspend execution for time interval
 */
int32 do_nanosleep(const struct timespec* req, struct timespec* rem);

/* Timer functions */

/*
 * Create a timer
 */
int32 timer_create(clockid_t clockid, struct sigevent* evp, timer_t* timerid);

/*
 * Delete a timer
 */
int32 timer_delete(timer_t timerid);

/*
 * Arm/disarm a timer
 */
int32 timer_settime(timer_t timerid, int32 flags, const struct itimerspec* value, struct itimerspec* ovalue);

/*
 * Get remaining time on a timer
 */
int32 timer_gettime(timer_t timerid, struct itimerspec* value);

/* Internal kernel time management */

/*
 * Convert jiffies to timespec
 */
void jiffies_to_timespec(uint64 jiffies, struct timespec* value);

/*
 * Update system time from hardware clock
 */
void update_sys_time_from_hw(void);

/*
 * Initialize time subsystem
 */
void time_init(void);

#endif /* _KERNEL_TIME_H */