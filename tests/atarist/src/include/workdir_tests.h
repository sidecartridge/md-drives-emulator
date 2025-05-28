#ifndef WORKDIR_TESTS_H
#define WORKDIR_TESTS_H

typedef struct {
  unsigned long b_free;     // número de clusters libres
  unsigned long b_total;    // número total de clusters
  unsigned long b_secsize;  // tamaño del sector en bytes
  unsigned long b_clsiz;    // sectores por cluster
} Dfree;

int run_workdir_tests();

#endif  // WORKDIR_TESTS_H
