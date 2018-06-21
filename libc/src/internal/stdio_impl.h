#ifndef _STDIO_IMPL_H
#define _STDIO_IMPL_H

#include <stdio.h>
#include "syscall.h"
#include "libc.h"

#define UNGET 8

#define FFINALLOCK(f) ((f)->lock>=0 ? __lockfile((f)) : 0)
#define FLOCK(f)
#define FUNLOCK(f)

#define F_PERM 1
#define F_NORD 4
#define F_NOWR 8
#define F_EOF 16
#define F_ERR 32
#define F_SVB 64
#define F_APP 128

struct _IO_FILE {
	unsigned flags;
	unsigned char *rpos, *rend;
	unsigned char *wend, *wpos;
	unsigned char *wbase;
	size_t (*write)(FILE *, const unsigned char *, size_t);
	unsigned char *buf;
	size_t buf_size;
	signed char mode;
	signed char lbf;
	volatile int lock;
	void *cookie;
};

size_t __stdio_read(FILE *, unsigned char *, size_t);
size_t __stdio_write(FILE *, const unsigned char *, size_t);
size_t __stdout_write(FILE *, const unsigned char *, size_t);
off_t __stdio_seek(FILE *, off_t, int);
int __stdio_close(FILE *);

size_t __string_read(FILE *, unsigned char *, size_t);

int __toread(FILE *);
int __towrite(FILE *);

#if defined(__PIC__) && (100*__GNUC__+__GNUC_MINOR__ >= 303)
__attribute__((visibility("protected")))
#endif
int __overflow(FILE *, int), __uflow(FILE *);

int __fseeko(FILE *, off_t, int);
int __fseeko_unlocked(FILE *, off_t, int);
off_t __ftello(FILE *);
off_t __ftello_unlocked(FILE *);
size_t __fwritex(const unsigned char *, size_t, FILE *);
int __putc_unlocked(int, FILE *);

FILE *__fdopen(int, const char *);
int __fmodeflags(const char *);

FILE *__ofl_add(FILE *f);
FILE **__ofl_lock(void);
void __ofl_unlock(void);

#define feof(f) ((f)->flags & F_EOF)
#define ferror(f) ((f)->flags & F_ERR)

#define getc_unlocked(f) \
	( ((f)->rpos < (f)->rend) ? *(f)->rpos++ : __uflow((f)) )

#define putc_unlocked(c, f) \
	( ((unsigned char)(c)!=(f)->lbf && (f)->wpos<(f)->wend) \
	? *(f)->wpos++ = (c) : __overflow((f),(c)) )

/* Caller-allocated FILE * operations */
FILE *__fopen_rb_ca(const char *, FILE *, unsigned char *, size_t);
int __fclose_ca(FILE *);

#endif