#ifndef FOLDER_LISTING_TESTS_H
#define FOLDER_LISTING_TESTS_H

#include <stdint.h>

typedef struct {
  char d_name[12];         /* file name: filename.typ     00-11   */
  uint32_t d_offset_drive; /* dir position                12-15   */
  uint16_t d_curbyt;       /* byte pointer within current cluster 16-17 */
  uint16_t d_curcl;        /* current cluster number for file	   18-19 */
  uint8_t d_attr;          /* attributes of file          20      */
  uint8_t d_attrib;        /* attributes of f file 21 */
  uint16_t d_time;         /* time from file date 22-23 */
  uint16_t d_date;         /* date from file date 24-25 */
  uint32_t d_length;       /* file length in bytes 26-29 */
  char d_fname[14];        /* file name: filename.typ 30-43 */
} DTA;

int run_folder_listing_tests(int presskey);

#endif  // FOLDER_TESTS_H
