#ifndef syscalls_h
#define syscalls_h

/*
 * syscalls.h - syscall wrappers which have nowhere better to live
 */

struct timespec32;
struct timespec;
struct timezone;
struct utsname;
typedef int clockid_t;

#if defined(__cplusplus)
extern "C" {
#endif

void sc_exit(void);
void sc_exit_group(int);
int sc_set_tid_address(int *);
int sc_uname(struct utsname *);
int sc_reboot(unsigned long, unsigned long, int, void *);
int sc_nanosleep(const struct timespec32 *, struct timespec32 *);
int sc_clock_gettime(clockid_t, struct timespec *);
int sc_clock_settime(clockid_t, const struct timespec *);
int sc_clock_settime32(clockid_t, const struct timespec32 *);
int sc_gettid();

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif
