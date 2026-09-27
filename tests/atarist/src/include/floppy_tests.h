#ifndef FLOPPY_TESTS_H
#define FLOPPY_TESTS_H

/* The floppy tests, run by FLOPTEST against the disk in A: built by
   tools/dev/make_floppy_image.py: first from supervisor mode, then again from
   user mode (from_user_mode), without the cases that read supervisor memory
   or that the host triggers once. */
int run_floppy_tests(int from_user_mode);

#endif  // FLOPPY_TESTS_H
