/**
 * File: gemdrive.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2023-April 2025
 * Copyright: 2023 2025 - GOODDATA LABS SL
 * Description: Emulate GEMDOS hard disk driver
 */

#include "gemdrive.h"

// The GEMDOS calls
const char *GEMDOS_CALLS[93] = {
    "Pterm0",    // 0x00
    "Conin",     // 0x01
    "Cconout",   // 0x02
    "Cauxin",    // 0x03
    "Cauxout",   // 0x04
    "Cprnout",   // 0x05
    "Crawio",    // 0x06
    "Crawcin",   // 0x07
    "Cnecin",    // 0x08
    "Cconws",    // 0x09
    "Cconrs",    // 0x0A
    "Cconis",    // 0x0B
    "",          // 0x0C
    "",          // 0x0D
    "Dsetdrv",   // 0x0E
    "",          // 0x0F
    "Cconos",    // 0x10
    "Cprnos",    // 0x11
    "Cauxis",    // 0x12
    "Cauxos",    // 0x13
    "Maddalt",   // 0x14
    "",          // 0x15
    "",          // 0x16
    "",          // 0x17
    "",          // 0x18
    "Dgetdrv",   // 0x19
    "Fsetdta",   // 0x1A
    "",          // 0x1B
    "",          // 0x1C
    "",          // 0x1D
    "",          // 0x1E
    "",          // 0x1F
    "Super",     // 0x20
    "",          // 0x21
    "",          // 0x22
    "",          // 0x23
    "",          // 0x24
    "",          // 0x25
    "",          // 0x26
    "",          // 0x27
    "",          // 0x28
    "",          // 0x29
    "Tgetdate",  // 0x2A
    "Tsetdate",  // 0x2B
    "Tgettime",  // 0x2C
    "Tsettime",  // 0x2D
    "",          // 0x2E
    "Fgetdta",   // 0x2F
    "Sversion",  // 0x30
    "Ptermres",  // 0x31
    "",          // 0x32
    "",          // 0x33
    "",          // 0x34
    "",          // 0x35
    "Dfree",     // 0x36
    "",          // 0x37
    "",          // 0x38
    "Dcreate",   // 0x39
    "Ddelete",   // 0x3A
    "Dsetpath",  // 0x3B
    "Fcreate",   // 0x3C
    "Fopen",     // 0x3D
    "Fclose",    // 0x3E
    "Fread",     // 0x3F
    "Fwrite",    // 0x40
    "Fdelete",   // 0x41
    "Fseek",     // 0x42
    "Fattrib",   // 0x43
    "Mxalloc",   // 0x44
    "Fdup",      // 0x45
    "Fforce",    // 0x46
    "Dgetpath",  // 0x47
    "Malloc",    // 0x48
    "Mfree",     // 0x49
    "Mshrink",   // 0x4A
    "Pexec",     // 0x4B
    "Pterm",     // 0x4C
    "",          // 0x4D
    "Fsfirst",   // 0x4E
    "Fsnext",    // 0x4F
    "",          // 0x50
    "",          // 0x51
    "",          // 0x52
    "",          // 0x53
    "",          // 0x54
    "",          // 0x55
    "Frename",   // 0x56
    "Fdatime",   // 0x57
    "",          // 0x58
    "",          // 0x59
    "",          // 0x5A
    "",          // 0x5B
    "Flock",     // 0x5C
};

const uint8_t BLACKLISTED_GEMDOS_CALLS[52] = {
    //    0x00, // "Pterm0",
    0x01,  // "Conin"
    0x02,  // "Cconout"
    0x03,  // "Cauxin"
    0x04,  // "Cauxout"
    0x05,  // "Cprnout"
    0x06,  // "Crawio"
    0x07,  // "Crawcin"
    0x08,  // "Cnecin"
    0x09,  // "Cconws"
    0x0A,  // "Cconrs"
    0x0B,  // "Cconis"
    //    0x0E, // "Dsetdrv"
    0x10,  // "Cconos"
    0x11,  // "Cprnos"
    0x12,  // "Cauxis"
    0x13,  // "Cauxos"
           //    0x14, // "Maddalt"
           //    0x1A, // "Fsetdta"
           //    0x20, // "Super"
           //    0x2A, // "Tgetdate"
           //    0x2B, // "Tsetdate"
           //    0x2C, // "Tgettime"
           //    0x2D, // "Tsettime"
           //    0x2F, // "Fgetdta"
           //    0x30, // "Sversion"
           //    0x31, // "Ptermres"
           //    0x36, // "Dfree"
           //    0x39, // "Dcreate"
           //    0x3A, // "Ddelete"
           //    0x3B, // "Dsetpath"
           //    0x3C, // "Fcreate"
           //    0x3D, // "Fopen"
           //    0x3E, // "Fclose"
           //    0x3F, // "Fread"
           //    0x40, // "Fwrite"
           //    0x41, // "Fdelete"
           //    0x42, // "Fseek"
           //    0x43, // "Fattrib"
           //    0x44, // "Mxalloc"
           //    0x45, // "Fdup"
           //    0x46, // "Fforce"
           //    0x47, // "Dgetpath"
           //    0x48, // "Malloc"
           //    0x49, // "Mfree"
           //    0x4A, // "Mshrink"
           //    0x4B, // "Pexec"
           //    0x4C, // "Pterm"
           //    0x4E, // "Fsfirst"
           //    0x4F, // "Fsnext"
           //    0x56, // "Frename"
           //    0x57, // "Fdatime"
           //    0x5C, // "Flock"
};

static FATFS filesys;

// Let's substitute the flags
static char hdFolder[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
static char drive = 'C';
static uint32_t driveNum = 0;

// Save Dsetpath variables
static char dpathStr[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};

// Save Fsetdta variables
// Pre-allocated node pool and free-list head
static DTANode dtaPool[DTA_POOL_SIZE];
static DTANode *dtaFreeList;
static DTANode *dtaTbl[DTA_HASH_TABLE_SIZE];

// Structures to store the file descriptors
static FileDescriptors *fdescriptors =
    NULL;  // Initialize the head of the list to NULL

// Pexec structures
static PD *pexec_pd = NULL;
static ExecHeader *pexec_exec_header = NULL;

#define FIND_RECURS 4 /* Maximum number of wildcard terms */

static DWORD __not_in_flash_func(get_achar)(const TCHAR **ptr) {
  DWORD chr;

  chr = (BYTE) * (*ptr)++;                         /* Get a byte */
  if (((chr) >= 'a' && (chr) <= 'z')) chr -= 0x20; /* To upper ASCII char */
  if (chr >= 0x80) chr = chr & 0x7F;               /* Use lower 7 bits */
  return chr;
}

/* 0:mismatched, 1:matched */
static int __not_in_flash_func(pattern_match)(
    const TCHAR *pat, /* Matching pattern */
    const TCHAR *nam, /* String to be tested */
    UINT skip,        /* Number of pre-skip chars (number of ?s,
                         b8:infinite (* specified)) */
    UINT recur        /* Recursion count */
) {
  const TCHAR *pptr;
  const TCHAR *nptr;
  DWORD pchr, nchr;
  UINT sk;

  while ((skip & 0xFF) != 0) {      /* Pre-skip name chars */
    if (!get_achar(&nam)) return 0; /* Branch mismatched if less name chars */
    skip--;
  }
  if (*pat == 0 && skip) return 1; /* Matched? (short circuit) */

  do {
    pptr = pat;
    nptr = nam; /* Top of pattern and name to match */
    for (;;) {
      if (*pptr == '\?' || *pptr == '*') { /* Wildcard term? */
        if (recur == 0) return 0;          /* Too many wildcard terms? */
        sk = 0;
        do { /* Analyze the wildcard term */
          if (*pptr++ == '\?') {
            sk++;
          } else {
            sk |= 0x100;
          }
        } while (*pptr == '\?' || *pptr == '*');
        if (pattern_match(pptr, nptr, sk, recur - 1))
          return 1; /* Test new branch (recursive call) */
        nchr = *nptr;
        break; /* Branch mismatched */
      }
      pchr = get_achar(&pptr); /* Get a pattern char */
      nchr = get_achar(&nptr); /* Get a name char */
      if (pchr != nchr) break; /* Branch mismatched? */
      if (pchr == 0)
        return 1; /* Branch matched? (matched at end of both strings) */
    }
    get_achar(&nam); /* nam++ */
  } while (skip &&
           nchr); /* Retry until end of name if infinite search is specified */

  return 0;
}

static bool isTrue(const char *value) {
  if ((value == NULL) || (value[0] == '\0')) {
    return false;
  }
  if ((value[0] == 't') || (value[0] == 'T') || (value[0] == 'y') ||
      (value[0] == 'Y') || (value[0] == '1')) {
    return true;
  }
  return false;
}

// Erase the values in the DTA transfer area
static void __not_in_flash_func(nullifyDTA)(uint32_t mem) {
  memset((void *)(mem + GEMDRIVE_DTA_TRANSFER), 0, DTA_SIZE_ON_ST);
}

// Helpers: allocate/release from pool
static DTANode *dta_node_alloc(void) {
  if (!dtaFreeList) return NULL;
  DTANode *n = dtaFreeList;
  dtaFreeList = n->next;
  return n;
}
static void dta_node_free(DTANode *n) {
  // free allocated copies
  if (n->dj) {
    free(n->dj);
    n->dj = NULL;
  }
  if (n->pat) {
    free(n->pat);
    n->pat = NULL;
  }
  // return node to free list
  n->next = dtaFreeList;
  dtaFreeList = n;
}

// Hash function
static unsigned int __not_in_flash_func(hash)(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352D;
  x ^= x >> 15;
  return x & (DTA_HASH_TABLE_SIZE - 1);
}

// Insert function (allocates DTA copy)
static int __not_in_flash_func(insertDTA)(uint32_t key) {
  unsigned idx = hash(key);
  DTANode *n = dta_node_alloc();
  if (!n) return -1;

  n->key = key;
  n->attribs = 0xFFFFFFFF;

  // TODO: Replace this stub with actual data fetch
  DTA data_src = {"filename", 0, 0, 0, 0, 0, 0, 0, 0, "filename"};
  n->data = data_src;

  // DIR is allocated by caller; just store pointers
  // Ensure dta_node_free will not free them
  n->dj = NULL;

  n->pat = NULL;          // no pattern yet
  n->next = dtaTbl[idx];  // separate chaining
  dtaTbl[idx] = n;

  DPRINTF("Insert DTA key: %08X at idx %u\n", n->key, idx);
  return 0;
}

// Lookup function
static DTANode *__not_in_flash_func(lookupDTA)(uint32_t key) {
  unsigned idx = hash(key);
  DTANode *p = dtaTbl[idx];
  while (p) {
    if (p->key == key) {
      // restore pattern into DIR if needed
      if (p->dj) p->dj->pat = p->pat;
      if (p->dj) {
        DPRINTF(
            "Retrieved DTA key: %x, filinfo_fname: %s, dir_obj: %x, pattern: "
            "%s\n",
            p->key, p->fname, (void *)p->dj, p->dj->pat);
      } else {
        DPRINTF("Retrieved DTA key: %x. Empty DTA\n", p->key);
      }
      return p;
    }
    p = p->next;
  }
  DPRINTF("DTA key: %x not found\n", key);
  return NULL;
}

// Release (remove) function
static void __not_in_flash_func(releaseDTA)(uint32_t key) {
  unsigned idx = hash(key);
  DTANode *p = dtaTbl[idx], *prev = NULL;
  while (p) {
    if (p->key == key) {
      if (prev)
        prev->next = p->next;
      else
        dtaTbl[idx] = p->next;
      if (p->dj) {
        DPRINTF(
            "Retrieved DTA key: %x, filinfo_fname: %s, dir_obj: %x, pattern: "
            "%s\n",
            p->key, p->fname, (void *)p->dj, p->dj->pat);
      } else {
        DPRINTF("Retrieved DTA key: %x. Empty DTA\n");
      }
      dta_node_free(p);
      return;
    }
    prev = p;
    p = p->next;
  }
}

// Count the number of elements
static unsigned int __not_in_flash_func(countDTA)(void) {
  unsigned cnt = 0;
  for (int i = 0; i < DTA_HASH_TABLE_SIZE; ++i) {
    for (DTANode *p = dtaTbl[i]; p; p = p->next) ++cnt;
  }
  DPRINTF("DTA count: %d/%u\n", cnt, DTA_POOL_SIZE);
  return cnt;
}

// Initialize the hash table
static void __not_in_flash_func(initializeDTAHashTable)() {
  // build free list
  dtaFreeList = &dtaPool[0];
  for (int i = 0; i < DTA_POOL_SIZE - 1; ++i) {
    dtaPool[i].next = &dtaPool[i + 1];
  }
  dtaPool[DTA_POOL_SIZE - 1].next = NULL;
  // clear buckets
  for (int i = 0; i < DTA_HASH_TABLE_SIZE; ++i) {
    dtaTbl[i] = NULL;
  }
}

// Clean the hash table
static void __not_in_flash_func(cleanDTAHashTable)(void) {
  for (int i = 0; i < DTA_HASH_TABLE_SIZE; ++i) {
    DTANode *p = dtaTbl[i];
    while (p) {
      DTANode *nx = p->next;
      dta_node_free(p);
      p = nx;
    }
    dtaTbl[i] = NULL;
  }
}

static void __not_in_flash_func(searchPath2ST)(
    const char *fspec_str,
    char *internal_path,  // caller must ensure this is at least
                          // 2*GEMDRIVE_MAX_FOLDER_LENGTH
    char *path_forwardslash, char *name_pattern) {
  char drive[4] = {0};  // room for "C:" + nul or longer labels
  char tmp_path[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};

  sdcard_splitFullpath(fspec_str, drive, path_forwardslash, name_pattern);

  // safe concat: check for overflow
  if (snprintf(tmp_path, sizeof(tmp_path), "%s%s", drive, path_forwardslash) >=
      (int)sizeof(tmp_path)) {
    // handle overflow (e.g. return error)
  }

  sdcard_back2ForwardSlash(path_forwardslash);

  // build the internal Path: caller-supplied buffer must be big enough!
  if (snprintf(internal_path, GEMDRIVE_FATFS_MAX_FOLDER_LENGTH, "%s/%s",
               hdFolder,
               path_forwardslash) >= GEMDRIVE_FATFS_MAX_FOLDER_LENGTH) {
    // handle overflow
    DPRINTF("ERROR: Internal path buffer overflow\n");
  }

  // collapse any "//" safely, including the nul terminator
  char *p = internal_path;
  while ((p = strstr(p, "//")) != NULL) {
    memmove(p, p + 1, strlen(p + 1) + 1);
  }

  // strip trailing ".*"
  size_t np_len = strlen(name_pattern);
  if (np_len >= 2 && name_pattern[np_len - 1] == '*' &&
      name_pattern[np_len - 2] == '.') {
    name_pattern[np_len - 2] = '\0';
  }

  // strip leading slash or backslash
  if (name_pattern[0] == '/' || name_pattern[0] == '\\') {
    memmove(name_pattern, name_pattern + 1, strlen(name_pattern + 1) + 1);
  }
}

static void __not_in_flash_func(remove_trailing_spaces)(char *str) {
  int len = strlen(str);

  // Start from the end of the string and move backwards
  while (len > 0 && str[len - 1] == ' ') {
    len--;
  }

  // Null-terminate the string at the new length
  str[len] = '\0';
}

static void __not_in_flash_func(populateDTA)(uint32_t memory_address_dta,
                                             uint32_t dta_address,
                                             int16_t gemdos_err_code,
                                             FILINFO *fno) {
  nullifyDTA(memory_address_dta);
  // Search the folder for the files
  DTANode *dataNode = lookupDTA(dta_address);
  DTA *data = dataNode != NULL ? &dataNode->data : NULL;
  if (data != NULL) {
    *((volatile uint16_t *)(memory_address_dta + GEMDRIVE_DTA_F_FOUND)) = 0;
    if (fno) {
      strcpy(data->d_name, fno->altname);
      strcpy(data->d_fname, fno->altname);
      data->d_offset_drive = 0;
      data->d_curbyt = 0;
      data->d_curcl = 0;
      data->d_attr = sdcard_attribsFAT2ST(fno->fattrib);
      data->d_attrib = sdcard_attribsFAT2ST(fno->fattrib);
      data->d_time = fno->ftime;
      data->d_date = fno->fdate;
      data->d_length = (uint32_t)fno->fsize;
      // Ignore the reserved field

      // Transfer the DTA to the Atari ST
      // Copy the DTA to the shared memory
      for (uint8_t i = 0; i < 12; i += 1) {
        *((volatile uint8_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER +
                               i)) = (uint8_t)data->d_name[i];
      }
      CHANGE_ENDIANESS_BLOCK16(memory_address_dta + GEMDRIVE_DTA_TRANSFER + 30,
                               14);
      *((volatile uint32_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER +
                              12)) = data->d_offset_drive;
      *((volatile uint16_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER +
                              16)) = data->d_curbyt;
      *((volatile uint16_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER +
                              18)) = data->d_curcl;
      *((volatile uint8_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER + 20)) =
          data->d_attr;
      *((volatile uint8_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER + 21)) =
          data->d_attrib;
      CHANGE_ENDIANESS_BLOCK16(memory_address_dta + GEMDRIVE_DTA_TRANSFER + 20,
                               2);
      *((volatile uint16_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER +
                              22)) = data->d_time;
      *((volatile uint16_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER +
                              24)) = data->d_date;
      // Assuming memory_address_dta is a byte-addressable pointer (e.g.,
      // uint8_t*)
      uint32_t value = ((data->d_length << 16) & 0xFFFF0000) |
                       ((data->d_length >> 16) & 0xFFFF);
      uint16_t *address =
          (uint16_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER + 26);
      address[1] = (value >> 16) & 0xFFFF;  // Most significant 16 bits
      address[0] = value & 0xFFFF;          // Least significant 16 bits
      for (uint8_t i = 0; i < 14; i += 1) {
        *((volatile uint8_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER + 30 +
                               i)) = (uint8_t)data->d_fname[i];
      }
      CHANGE_ENDIANESS_BLOCK16(memory_address_dta + GEMDRIVE_DTA_TRANSFER + 30,
                               14);
      char attribsStr[7] = "";
      sdcard_getAttribsSTStr(
          attribsStr, *((volatile uint8_t *)(memory_address_dta +
                                             GEMDRIVE_DTA_TRANSFER + 21)));
      DPRINTF(
          "Populate DTA. addr: %x - attrib: %s - time: %d - date: %d - length: "
          "%x - filename: %s\n",
          dta_address, attribsStr,
          *((volatile uint16_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER +
                                  22)),
          *((volatile uint16_t *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER +
                                  24)),
          ((uint32_t)address[0] << 16) | address[1],
          (char *)(memory_address_dta + GEMDRIVE_DTA_TRANSFER + 30));
    } else {
      // If no more files found, return ENMFIL for Fsnext
      // If no files found, return EFILNF for Fsfirst
      DPRINTF("DTA at %x showing error code: %x\n", dta_address,
              gemdos_err_code);
      *((volatile int16_t *)(memory_address_dta + GEMDRIVE_DTA_F_FOUND)) =
          (int16_t)gemdos_err_code;
      // release the memory allocated for the hash table
      releaseDTA(dta_address);
      DPRINTF("DTA at %x released. DTA table elements: %d\n", dta_address,
              countDTA());
      if (gemdos_err_code == GEMDOS_EFILNF) {
        DPRINTF("Files not found in FSFIRST.\n");
      } else {
        DPRINTF("No more files found in FSNEXT.\n");
      }
    }
  } else {
    // No DTA structure found, return error
    DPRINTF("DTA not found at %x\n", dta_address);
    *((volatile uint16_t *)(memory_address_dta + GEMDRIVE_DTA_F_FOUND)) =
        0xFFFF;
  }
}

static void __not_in_flash_func(addFile)(FileDescriptors **head,
                                         FileDescriptors *newFDescriptor,
                                         const char *fpath, FIL fobject,
                                         uint16_t new_fd) {
  strncpy(newFDescriptor->fpath, fpath, GEMDRIVE_MAX_FOLDER_LENGTH);
  newFDescriptor->fpath[GEMDRIVE_MAX_FOLDER_LENGTH] =
      '\0';  // Ensure null-termination
  newFDescriptor->fobject = fobject;
  newFDescriptor->fd = new_fd;
  newFDescriptor->offset = 0;
  newFDescriptor->next = *head;
  *head = newFDescriptor;
  DPRINTF("File %s added with fd %i\n", fpath, new_fd);
}

static void __not_in_flash_func(printFDs)(FileDescriptors *head) {
  for (const FileDescriptors *cur = head; cur; cur = cur->next) {
    DPRINTF("File descriptor: %u - Path: %s\n", cur->fd, cur->fpath);
  }
}

static FileDescriptors *__not_in_flash_func(getFileByPath)(
    FileDescriptors *head, const char *fpath) {
  while (head) {
    if (strcmp(head->fpath, fpath) == 0) return head;
    head = head->next;
  }
  return NULL;
}

static FileDescriptors *__not_in_flash_func(getFileByFD)(FileDescriptors *head,
                                                         uint16_t fd) {
  while (head) {
    DPRINTF("Comparing %i with %i\n", head->fd, fd);
    if (head->fd == fd) {
      DPRINTF("File descriptor found. Returning %i\n", head->fd);
      return head;
    }
    head = head->next;
  }
  return NULL;
}

static int __not_in_flash_func(deleteFileByFD)(FileDescriptors **head,
                                               uint16_t fd) {
  FileDescriptors *cur = *head;
  FileDescriptors *prev = NULL;

  while (cur) {
    if (cur->fd == fd) {
      // Unlink
      if (prev) {
        prev->next = cur->next;
      } else {
        *head = cur->next;
      }
      // Free the node’s memory
      free(cur);
      return 1;  // one node deleted
    }
    prev = cur;
    cur = cur->next;
  }
  return 0;  // no matching fd found
}
// Find the first available file descriptor
static uint16_t __not_in_flash_func(getFirstAvailableFD)(
    FileDescriptors *head) {
  // Find the lowest available FD >= FIRST_FILE_DESCRIPTOR, scanning unsorted
  // list.
  uint16_t candidate = FIRST_FILE_DESCRIPTOR;
  while (1) {
    int found = 0;
    for (FileDescriptors *cur = head; cur; cur = cur->next) {
      if (cur->fd == candidate) {
        found = 1;
        break;
      }
    }
    if (!found) return candidate;
    candidate++;
  }
}

// Clean all file descriptorsº
static void __not_in_flash_func(cleanFileDescriptors)(FileDescriptors **head) {
  FileDescriptors *cur = *head;
  FileDescriptors *next;

  while (cur) {
    // Close the file if still open
    f_close(&cur->fobject);
    // Keep track of the next node
    next = cur->next;
    // Free this node
    free(cur);
    // Move to next
    cur = next;
  }
  // Reset head to empty list
  *head = NULL;
  DPRINTF("All file descriptors cleaned.\n");
}

// dpathStr, hdFolder are global variables
static void __not_in_flash_func(getLocalFullPathname)(uint16_t *pyldPtr,
                                                      char *tmp_filepath) {
  // Obtain the fname string and keep it in memory
  // concatenated path and filename
  char path_filename[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
  char tmp_path[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};

  COPY_AND_CHANGE_ENDIANESS_BLOCK16(pyldPtr, path_filename,
                                    GEMDRIVE_MAX_FOLDER_LENGTH);
  DPRINTF("dpathStr: %s\n", dpathStr);
  DPRINTF("path_filename: %s\n", path_filename);
  if (path_filename[1] == ':') {
    // If the path has the drive letter, jump two positions
    // and ignore the dpathStr
    snprintf(path_filename, GEMDRIVE_MAX_FOLDER_LENGTH, "%s",
             path_filename + 2);
    DPRINTF("New path_filename: %s\n", path_filename);
    snprintf(tmp_path, GEMDRIVE_MAX_FOLDER_LENGTH, "%s/", hdFolder);
  } else if (path_filename[0] == '\\') {
    // If the path filename has a backslash, ignore the dpathStr
    DPRINTF("New path_filename: %s\n", path_filename);
    snprintf(tmp_path, GEMDRIVE_MAX_FOLDER_LENGTH, "%s/", hdFolder);
  } else {
    // If the path filename does not have a drive letter,
    // concatenate the path with the hdFolder and the filename
    // If the path has the drive letter, jump two positions
    if (dpathStr[1] == ':') {
      snprintf(tmp_path, GEMDRIVE_MAX_FOLDER_LENGTH, "%s/%s", hdFolder,
               dpathStr + 2);
    } else {
      snprintf(tmp_path, GEMDRIVE_MAX_FOLDER_LENGTH, "%s/%s", hdFolder,
               dpathStr);
    }
  }
  snprintf(tmp_filepath, GEMDRIVE_MAX_FOLDER_LENGTH, "%s/%s", tmp_path,
           path_filename);
  sdcard_back2ForwardSlash(tmp_filepath);

  // Remove duplicated forward slashes
  sdcard_removeDupSlashes(tmp_filepath);
}

static void printVars(uint32_t mem) {
  // DPRINTF("Printing shared variables\n");
  DPRINTF(" GEMDRIVE_REENTRY_TRAP(0x%04x): %x\n", GEMDRIVE_REENTRY_TRAP,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_REENTRY_TRAP))));
  // For strings, swapping is not applicable, so print as is.
  DPRINTF(" GEMDRIVE_DEFAULT_PATH(0x%04x): %s\n", GEMDRIVE_DEFAULT_PATH,
          (char *)(mem + GEMDRIVE_DEFAULT_PATH));
  DPRINTF(" GEMDRIVE_DTA_F_FOUND(0x%04x): %x\n", GEMDRIVE_DTA_F_FOUND,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_DTA_F_FOUND))));
  DPRINTF(" GEMDRIVE_DTA_TRANSFER(0x%04x): %x\n", GEMDRIVE_DTA_TRANSFER,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_DTA_TRANSFER))));
  DPRINTF(" GEMDRIVE_DTA_EXIST(0x%04x): %x\n", GEMDRIVE_DTA_EXIST,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_DTA_EXIST))));
  DPRINTF(" GEMDRIVE_DTA_RELEASE(0x%04x): %x\n", GEMDRIVE_DTA_RELEASE,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_DTA_RELEASE))));
  DPRINTF(
      " GEMDRIVE_SET_DPATH_STATUS(0x%04x): %x\n", GEMDRIVE_SET_DPATH_STATUS,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_SET_DPATH_STATUS))));
  DPRINTF(" GEMDRIVE_FOPEN_HANDLE(0x%04x): %x\n", GEMDRIVE_FOPEN_HANDLE,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FOPEN_HANDLE))));
  DPRINTF(" GEMDRIVE_READ_BYTES(0x%04x): %x\n", GEMDRIVE_READ_BYTES,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_READ_BYTES))));
  DPRINTF(" GEMDRIVE_READ_BUFF(0x%04x): %x\n", GEMDRIVE_READ_BUFF,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_READ_BUFF))));
  DPRINTF(" GEMDRIVE_WRITE_BYTES(0x%04x): %x\n", GEMDRIVE_WRITE_BYTES,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_WRITE_BYTES))));
  DPRINTF(" GEMDRIVE_WRITE_CHK(0x%04x): %x\n", GEMDRIVE_WRITE_CHK,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_WRITE_CHK))));
  DPRINTF(" GEMDRIVE_WRITE_CONFIRM_STATUS(0x%04x): %x\n",
          GEMDRIVE_WRITE_CONFIRM_STATUS,
          SWAP_LONGWORD(
              *((volatile uint32_t *)(mem + GEMDRIVE_WRITE_CONFIRM_STATUS))));
  DPRINTF(
      " GEMDRIVE_FCLOSE_STATUS(0x%04x): %x\n", GEMDRIVE_FCLOSE_STATUS,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FCLOSE_STATUS))));
  DPRINTF(
      " GEMDRIVE_DCREATE_STATUS(0x%04x): %x\n", GEMDRIVE_DCREATE_STATUS,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_DCREATE_STATUS))));
  DPRINTF(
      " GEMDRIVE_DDELETE_STATUS(0x%04x): %x\n", GEMDRIVE_DDELETE_STATUS,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_DDELETE_STATUS))));
  DPRINTF(
      " GEMDRIVE_FCREATE_HANDLE(0x%04x): %x\n", GEMDRIVE_FCREATE_HANDLE,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FCREATE_HANDLE))));
  DPRINTF(
      " GEMDRIVE_FDELETE_STATUS(0x%04x): %x\n", GEMDRIVE_FDELETE_STATUS,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FDELETE_STATUS))));
  DPRINTF(" GEMDRIVE_FSEEK_STATUS(0x%04x): %x\n", GEMDRIVE_FSEEK_STATUS,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FSEEK_STATUS))));
  DPRINTF(
      " GEMDRIVE_FATTRIB_STATUS(0x%04x): %x\n", GEMDRIVE_FATTRIB_STATUS,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FATTRIB_STATUS))));
  DPRINTF(
      " GEMDRIVE_FRENAME_STATUS(0x%04x): %x\n", GEMDRIVE_FRENAME_STATUS,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FRENAME_STATUS))));
  DPRINTF(
      " GEMDRIVE_FDATETIME_DATE(0x%04x): %x\n", GEMDRIVE_FDATETIME_DATE,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FDATETIME_DATE))));
  DPRINTF(
      " GEMDRIVE_FDATETIME_TIME(0x%04x): %x\n", GEMDRIVE_FDATETIME_TIME,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FDATETIME_TIME))));
  DPRINTF(
      " GEMDRIVE_FDATETIME_STATUS(0x%04x): %x\n", GEMDRIVE_FDATETIME_STATUS,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_FDATETIME_STATUS))));
  DPRINTF(" GEMDRIVE_DFREE_STATUS(0x%04x): %x\n", GEMDRIVE_DFREE_STATUS,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_DFREE_STATUS))));
  DPRINTF(" GEMDRIVE_DFREE_STRUCT(0x%04x): %x\n", GEMDRIVE_DFREE_STRUCT,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_DFREE_STRUCT))));
  DPRINTF(" GEMDRIVE_PEXEC_MODE(0x%04x): %x\n", GEMDRIVE_PEXEC_MODE,
          SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_PEXEC_MODE))));
  DPRINTF(
      " GEMDRIVE_PEXEC_STACK_ADDR(0x%04x): %x\n", GEMDRIVE_PEXEC_STACK_ADDR,
      SWAP_LONGWORD(*((volatile uint32_t *)(mem + GEMDRIVE_PEXEC_STACK_ADDR))));
  // For strings, swapping is not applicable, so print as is.
  DPRINTF(" GEMDRIVE_PEXEC_FNAME(0x%04x): %s\n", GEMDRIVE_PEXEC_FNAME,
          (char *)(mem + GEMDRIVE_PEXEC_FNAME));
  DPRINTF(" GEMDRIVE_PEXEC_CMDLINE(0x%04x): %s\n", GEMDRIVE_PEXEC_CMDLINE,
          (char *)(mem + GEMDRIVE_PEXEC_CMDLINE));
  DPRINTF(" GEMDRIVE_PEXEC_ENVSTR(0x%04x): %s\n", GEMDRIVE_PEXEC_ENVSTR,
          (char *)(mem + GEMDRIVE_PEXEC_ENVSTR));
}

static void printPayload(uint8_t *payloadShowBytesPtr) {
  if (!payloadShowBytesPtr) return;
  const int bytesPerLine = 8;
  char ascii[bytesPerLine + 1];
  // Display the first 256 bytes: 8 bytes per line with ASCII
  for (int i = 0; i < 256; i += bytesPerLine) {
    for (int j = 0; j < bytesPerLine; ++j) {
      uint8_t c = payloadShowBytesPtr[i + j];
      ascii[j] = (c >= 32 && c <= 126) ? c : '.';
    }
    ascii[bytesPerLine] = '\0';
    DPRINTF("%04x - %02x %02x %02x %02x %02x %02x %02x %02x | %s\n", i,
            payloadShowBytesPtr[i + 0], payloadShowBytesPtr[i + 1],
            payloadShowBytesPtr[i + 2], payloadShowBytesPtr[i + 3],
            payloadShowBytesPtr[i + 4], payloadShowBytesPtr[i + 5],
            payloadShowBytesPtr[i + 6], payloadShowBytesPtr[i + 7], ascii);
  }
}

#define NUM_SHARED_VARS 4096
static uint32_t memorySharedAddress = 0;
static uint32_t memoryRandomTokenAddress = 0;
static uint32_t memoryRandomTokenSeedAddress = 0;
static uint32_t memoryFirmwareCode = 0;

static void initVariables(uint32_t mem) {
  const uint16_t numSharedVars = (0x10000 - GEMDRIVE_RANDOM_TOKEN_OFFSET) / 4;
  DPRINTF("Initializing shared variables\n");
  for (uint32_t i = 0; i < numSharedVars; i++) {
    *((volatile uint32_t *)(mem + (i * 4))) = 0;
  }
}
void __not_in_flash_func(gemdrive_init)() {
  FRESULT fr; /* FatFs function common result code */

  srand(time(0));
  DPRINTF("Initializing GEMDRIVE...\n");  // Print alwayse

  dpathStr[0] = '\\';  // Set the root folder as default
  dpathStr[1] = '\0';

  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
  memoryRandomTokenAddress = memorySharedAddress + GEMDRIVE_RANDOM_TOKEN_OFFSET;
  memoryRandomTokenSeedAddress =
      memorySharedAddress + GEMDRIVE_RANDOM_TOKEN_SEED_OFFSET;

  memoryFirmwareCode = memorySharedAddress;

  initVariables(memorySharedAddress + GEMDRIVE_RANDOM_TOKEN_OFFSET);

  SettingsConfigEntry *gemDriveLetter = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_DRIVE);
  drive = 'C';
  if (gemDriveLetter != NULL) {
    drive = gemDriveLetter->value[0];
  }
  driveNum =
      (uint8_t)toupper(drive) - 65;  // Convert the drive letter to a number.
                                     // Add 1 because 0 is the current drive
  DPRINTF("Drive letter: %c, Drive number: %u\n", drive, driveNum);

  // Mount drive. For testing purposes.
  fr = f_mount(&filesys, "0:", 1);
  bool sdMounted = (fr == FR_OK);
  DPRINTF("SD card mounted: %s\n", sdMounted ? "OK" : "Failed");

  // Read the GEMDRIVE folder from the settings
  SettingsConfigEntry *gemDriveFolder = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_FOLDER);
  if (gemDriveFolder != NULL) {
    strncpy(hdFolder, gemDriveFolder->value, sizeof(hdFolder) - 1);
    hdFolder[sizeof(hdFolder) - 1] = '\0';  // Ensure null-termination
    DPRINTF("GEMDRIVE folder: %s\n", hdFolder);
  } else {
    DPRINTF("GEMDRIVE folder not found. Using default.\n");
    strncpy(hdFolder, "/hd", sizeof(hdFolder) - 1);
    hdFolder[sizeof(hdFolder) - 1] = '\0';  // Ensure null-termination
  }
  initializeDTAHashTable();
  DPRINTF("DTA table elements: %d\n", countDTA());
  dpathStr[0] = '\\';  // Set the root folder as default
  dpathStr[1] = '\0';

  // Enabled?
  bool gemDriveEnabled = false;
  SettingsConfigEntry *gemDriveEnabledParam = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_ENABLED);
  if (gemDriveEnabledParam != NULL) {
    if (isTrue(gemDriveEnabledParam->value)) {
      gemDriveEnabled = true;
      DPRINTF("GEMDRIVE enabled.\n");
    } else {
      gemDriveEnabled = false;
      DPRINTF("GEMDRIVE disabled.\n");
    }
  } else {
    DPRINTF("GEMDRIVE enabled setting not found. Defaulting to disabled.\n");
    return;
  }

  uint16_t buffType = 0;  // 0: Diskbuffer, 1: Stack

  SET_SHARED_VAR(GEMDRIVE_SHARED_VARIABLE_FIRST_FILE_DESCRIPTOR,
                 FIRST_FILE_DESCRIPTOR, memorySharedAddress,
                 GEMDRIVE_SHARED_VARIABLES_OFFSET);
  SET_SHARED_VAR(GEMDRIVE_SHARED_VARIABLE_DRIVE_LETTER, drive,
                 memorySharedAddress, GEMDRIVE_SHARED_VARIABLES_OFFSET);
  SET_SHARED_VAR(GEMDRIVE_SHARED_VARIABLE_DRIVE_NUMBER, driveNum,
                 memorySharedAddress, GEMDRIVE_SHARED_VARIABLES_OFFSET);
  SET_SHARED_VAR(GEMDRIVE_BUFFER_TYPE, buffType, memorySharedAddress,
                 GEMDRIVE_SHARED_VARIABLES_OFFSET);
  SET_SHARED_VAR(GEMDRIVE_SHARED_VARIABLE_ENABLED,
                 gemDriveEnabled ? 0xFFFFFFFF : 0, memorySharedAddress,
                 GEMDRIVE_SHARED_VARIABLES_OFFSET);

  if (memoryRandomTokenAddress != 0) {
    uint32_t randomToken = 0;
    DPRINTF("Init random token: %08X\n", randomToken);
    // Set the random token in the shared memory
    TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenAddress, randomToken);
    // Init the random token seed in the shared memory for the next command
    uint32_t newRandomSeedToken = get_rand_32();
    DPRINTF("Set the new random token seed: %08X\n", newRandomSeedToken);
    TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenSeedAddress, newRandomSeedToken);
  }

  DPRINTF("GEMDRIVE initialized with Drive Letter: %c, Drive Number: %u\n",
          drive, driveNum);
  DPRINTF("Waiting for commands...\n");
}

// Invoke this function to process the commands from the active loop in the
// main function
void __not_in_flash_func(gemdrive_loop)(TransmissionProtocol *lastProtocol,
                                        uint16_t *payloadPtr) {
  // #if defined(_DEBUG) && (_DEBUG != 0)
  //     uint16_t *ptr = ((uint16_t *)(lastProtocol).payload);

  //     // Jump the random token
  //     TPROTO_NEXT32_PAYLOAD_PTR(ptr);

  //     // Read the payload parameters
  //     uint16_t payloadSizeTmp = 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= GEMDRIVE_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D3: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= GEMDRIVE_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D4: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= GEMDRIVE_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D5: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= GEMDRIVE_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D6: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  // #endif

  // Only check the GEMDRVEMUL commands
  if (((lastProtocol->command_id >> 8) & 0xFF) != APP_GEMDRVEMUL) return;

  // Handle the command
  switch (lastProtocol->command_id) {
    case GEMDRVEMUL_DEBUG: {
      uint32_t d3 = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // Read d3
      DPRINTF("DEBUG D3: %x\n", d3);
      uint32_t d4 = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(
          payloadPtr);  // Move pointer and read d4
      DPRINTF("DEBUG D4: %x\n", d4);
      uint32_t d5 = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(
          payloadPtr);  // Move pointer and read d5
      DPRINTF("DEBUG D5: %x\n", d5);
      if (lastProtocol->payload_size <= 20) {
        uint32_t d6 = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(
            payloadPtr);  // Move pointer and read d6
        DPRINTF("DEBUG D6: %x\n", d6);
      } else {
        TPROTO_NEXT32_PAYLOAD_PTR(
            payloadPtr);  // Move pointer to payload buffer
        uint8_t *payloadShowBytesPtr = (uint8_t *)payloadPtr;
        printPayload(payloadShowBytesPtr);
      }
      break;
    }
    case GEMDRVEMUL_RESET: {
      DPRINTF("Resetting GEMDRIVE\n");
      // Reset the shared variables
      cleanDTAHashTable();
      cleanFileDescriptors(&fdescriptors);
      // Set the continue to continue booting
      SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_START);
      break;
    }
    case GEMDRVEMUL_SAVE_VECTORS: {
      DPRINTF("Saving vectors\n");
      uint32_t gemdos_trap_address_old = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);
      uint32_t gemdos_trap_address_xbra =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
      // Save the vectors needed for the floppy emulation
      DPRINTF("gemdos_trap_addres_xbra: %x\n", gemdos_trap_address_xbra);
      DPRINTF("gemdos_trap_address_old: %x\n", gemdos_trap_address_old);
      // DPRINTF("random token: %x\n", random_token);
      // Self modifying code to create the old and venerable XBRA structure
      uint32_t xbra_addr_offset = gemdos_trap_address_xbra & 0xFFFF;
      WRITE_AND_SWAP_LONGWORD(memoryFirmwareCode, xbra_addr_offset,
                              gemdos_trap_address_old);

      break;
    }
    case GEMDRVEMUL_SHOW_VECTOR_CALL: {
      uint16_t trapCall = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      // Check if the call is blacklisted
      bool isBlacklisted = false;
      for (int i = 0; i < sizeof(BLACKLISTED_GEMDOS_CALLS); i++) {
        if (trapCall == BLACKLISTED_GEMDOS_CALLS[i]) {
          isBlacklisted = true;  // Found the call in the blacklist
          break;
        }
      }
      // if (!isBlacklisted)
      // {
      // If the call is not blacklisted, print its information
      DPRINTF("GEMDOS CALL: %s (%x)\n", GEMDOS_CALLS[trapCall], trapCall);
      // }
      break;
    }
    case GEMDRVEMUL_SET_SHARED_VAR: {
      uint32_t sharedVarIdx = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);
      uint32_t sharedVarValue = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
      SET_SHARED_VAR(sharedVarIdx, sharedVarValue, memorySharedAddress,
                     GEMDRIVE_SHARED_VARIABLES_OFFSET);
      break;
    }
    case GEMDRVEMUL_DGETDRV_CALL: {
      // Get the drive letter
      uint16_t dgetdriveVal = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      DPRINTF("Dgetdrive value: %x\n", dgetdriveVal);
      break;
    }
    case GEMDRVEMUL_REENTRY_LOCK: {
      WRITE_WORD(memorySharedAddress, GEMDRIVE_REENTRY_TRAP, 0xFFFF);
      break;
    }
    case GEMDRVEMUL_REENTRY_UNLOCK: {
      WRITE_WORD(memorySharedAddress, GEMDRIVE_REENTRY_TRAP, 0);
      break;
    }
    case GEMDRVEMUL_DFREE_CALL: {
      uint16_t dfreeUnit = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      DPRINTF("DFREE unit: %x. (0=Default, 1=A, 2=B, 3=C, etc...)\n",
              dfreeUnit);
      DPRINTF("Current drive: %c. Unit number: %u\n", drive, driveNum);
      // Check the free space
      DWORD freeClusters;
      FATFS *fs;
      FRESULT fr;
      // Get free space
      fr = f_getfree(hdFolder, &freeClusters, &fs);
      if (fr != FR_OK) {
        WRITE_LONGWORD_RAW(memorySharedAddress, GEMDRIVE_DFREE_STATUS,
                           GEMDOS_ERROR);
      } else {
        // Calculate the total number of free bytes
        uint64_t freeBytes = freeClusters * fs->csize * NUM_BYTES_PER_SECTOR;
        DPRINTF(
            "Total clusters: %d, free clusters: %d, bytes per sector: %d, "
            "sectors per cluster: %d\n",
            fs->n_fatent - 2, freeClusters, NUM_BYTES_PER_SECTOR, fs->csize);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_DFREE_STRUCT,
                                freeClusters);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_DFREE_STRUCT + 4,
                                fs->n_fatent - 2);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_DFREE_STRUCT + 8,
                                NUM_BYTES_PER_SECTOR);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_DFREE_STRUCT + 12,
                                fs->csize);
        WRITE_LONGWORD_RAW(memorySharedAddress, GEMDRIVE_DFREE_STATUS,
                           GEMDOS_EOK);
      }
      break;
    }
    case GEMDRVEMUL_DGETPATH_CALL: {
      uint16_t dpathDrive = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);

      DPRINTF("Dpath drive: %x\n", dpathDrive);
      DPRINTF("Dpath string: %s\n", dpathStr);

      char tmpPath[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      // Copy default path safely and ensure null-termination
      strncpy(tmpPath, dpathStr, sizeof(tmpPath) - 1);
      tmpPath[sizeof(tmpPath) - 1] = '\0';
      sdcard_forward2Backslash(tmpPath);

      // Log and remove trailing backslash if present
      // size_t tmpLen = strlen(tmpPath);
      // DPRINTF("Dpath backslash string: %s\n", tmpPath);
      // if (tmpLen > 0 && tmpPath[tmpLen - 1] == '\\') {
      //   tmpPath[tmpLen - 1] = '\0';
      // }

      DPRINTF("Dpath backslash string (no last backslash): %s\n", tmpPath);

      COPY_AND_CHANGE_ENDIANESS_BLOCK16(
          tmpPath, memorySharedAddress + GEMDRIVE_DEFAULT_PATH,
          GEMDRIVE_MAX_FOLDER_LENGTH);
      break;
    }
    case GEMDRVEMUL_DSETPATH_CALL: {
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d3
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d4
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d5

      // Obtain the fname string and keep it in memory
      char dpathTmp[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      COPY_AND_CHANGE_ENDIANESS_BLOCK16(payloadPtr, dpathTmp,
                                        GEMDRIVE_MAX_FOLDER_LENGTH);
      DPRINTF("Default path string: %s, size: %zu\n", dpathTmp,
              strlen(dpathTmp));
      // Check if the directory exists
      char tmpPath[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};

      // If path has drive prefix 'C:' remove it
      if (dpathTmp[0] == drive && dpathTmp[1] == ':') {
        DPRINTF("Drive letter found: %c. Removing it.\n", drive);
        // Remove the drive letter and colon
        size_t rem = strlen(dpathTmp + 2);
        memmove(dpathTmp, dpathTmp + 2, rem + 1);
      }

      DPRINTF("Dpath string: %s\n", dpathStr);
      DPRINTF("Dpath tmp: %s\n", dpathTmp);

      // Check if the path is relative or absolute
      if ((dpathTmp[0] != '\\') && (dpathTmp[0] != '/')) {
        // Concatenate the path with the existing dpathStr
        char tmpPathConcat[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
        snprintf(tmpPathConcat, sizeof(tmpPathConcat), "%s/%s", dpathStr,
                 dpathTmp);
        DPRINTF("Concatenated path: %s\n", tmpPathConcat);
        strncpy(dpathTmp, tmpPathConcat, sizeof(dpathTmp) - 1);
        dpathTmp[sizeof(dpathTmp) - 1] = '\0';
        DPRINTF("Dpath tmp: %s\n", dpathTmp);
      } else {
        DPRINTF("Do not concatenate the path\n");
      }
      sdcard_back2ForwardSlash(dpathTmp);

      // Normalize the path
      sdcard_normalizePath(dpathTmp);
      DPRINTF("Normalized path: %s\n", dpathTmp);

      // Concatenate the path with the hdFolder
      snprintf(tmpPath, sizeof(tmpPath), "%s/%s", hdFolder, dpathTmp);

      // Remove duplicated forward slashes
      sdcard_removeDupSlashes(tmpPath);
      sdcard_removeDupSlashes(dpathTmp);

      FILINFO fno;
      FRESULT res = f_stat(tmpPath, &fno);

      if ((res == FR_OK && (fno.fattrib & AM_DIR))) {
        DPRINTF("Directory exists: %s\n", tmpPath);
        // Copy dpathTmp to dpathStr
        strcpy(dpathStr, dpathTmp);
        DPRINTF("The new default path is: %s\n", dpathStr);
        WRITE_WORD(memorySharedAddress, GEMDRIVE_SET_DPATH_STATUS, GEMDOS_EOK);
      } else {
        DPRINTF("Directory does not exist: %s\n", tmpPath);
        WRITE_WORD(memorySharedAddress, GEMDRIVE_SET_DPATH_STATUS,
                   GEMDOS_EPTHNF);
      }
      break;
    }
    case GEMDRVEMUL_DCREATE_CALL: {
      // Obtain the pathname string and keep it in memory
      // concatenated with the local harddisk folder and the default path
      // (if any)
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // Skip d3, d4, d5

      char tmpPath[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      getLocalFullPathname(payloadPtr, tmpPath);
      DPRINTF("Folder to create: %s\n", tmpPath);

      // Check if the folder exists. If not, return an error
      uint16_t dcreateCode = GEMDOS_ERROR;
      // Create the folder
      FRESULT ferr = f_mkdir(tmpPath);
      if (ferr != FR_OK) {
        DPRINTF("ERROR: Could not create folder (%d)\r\n", ferr);
        if (ferr == FR_NO_PATH) {
          dcreateCode = GEMDOS_EPTHNF;
        } else {
          dcreateCode = GEMDOS_EACCDN;
        }
      } else {
        DPRINTF("Folder created\n");
        dcreateCode = GEMDOS_EOK;
      }
      WRITE_WORD(memorySharedAddress, GEMDRIVE_DCREATE_STATUS, dcreateCode);
      break;
    }
    case GEMDRVEMUL_DDELETE_CALL: {
      // Obtain the pathname string and keep it in memory
      // concatenated with the local harddisk folder and the default path
      // (if any)
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // Skip d3, d4, d5

      char tmpPath[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      getLocalFullPathname(payloadPtr, tmpPath);
      DPRINTF("Folder to delete: %s\n", tmpPath);

      // Check if the folder exists. If not, return an error
      uint16_t ddeleteCode = GEMDOS_ERROR;
      if (sdcard_dirExist(tmpPath) == 0) {
        DPRINTF("ERROR: Folder does not exist\n");
        ddeleteCode = GEMDOS_EPTHNF;
      } else {
        // Delete the folder
        FRESULT ferr = f_unlink(tmpPath);
        if (ferr != FR_OK) {
          DPRINTF("ERROR: Could not delete folder (%d)\r\n", ferr);
          if (ferr == FR_DENIED) {
            DPRINTF("ERROR: Folder is not empty\n");
            ddeleteCode = GEMDOS_EACCDN;
          } else if (ferr == FR_NO_PATH) {
            DPRINTF("ERROR: Folder does not exist\n");
            ddeleteCode = GEMDOS_EPTHNF;
          } else {
            DPRINTF("ERROR: Internal error: %d\n", ferr);
            ddeleteCode = GEMDOS_EINTRN;
          }
        } else {
          DPRINTF("Folder deleted\n");
          ddeleteCode = GEMDOS_EOK;
        }
      }
      WRITE_WORD(memorySharedAddress, GEMDRIVE_DDELETE_STATUS, ddeleteCode);
      break;
    }
    case GEMDRVEMUL_FSETDTA_CALL: {
      uint32_t ndta = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // d3 register
      DTANode *currentDTANode = lookupDTA(ndta);
      if (currentDTANode) {
        // We don't release the DTA if it already exists. Wait for FsFirst
        // to do it
        DPRINTF("DTA at %x already exists.\n", ndta);
      } else {
        DPRINTF("Setting DTA: %x\n", ndta);
        int err = insertDTA(ndta);
        if (err == 0) {
          DPRINTF("Added ndta: %x.\n", ndta);
        } else {
          DPRINTF("Error adding ndta: %x.\n", ndta);
        }
      }
      break;
    }
    case GEMDRVEMUL_DTA_EXIST_CALL: {
      uint32_t ndta = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // d3 register
      DTANode *currentDTANode = lookupDTA(ndta);
      DPRINTF("DTA %x exists: %s\n", ndta, (currentDTANode) ? "TRUE" : "FALSE");
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_DTA_EXIST,
                              (currentDTANode ? ndta : 0));
      break;
    }
    case GEMDRVEMUL_DTA_RELEASE_CALL: {
      uint32_t ndta = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // d3 register
      DPRINTF("Releasing DTA: %x\n", ndta);
      DTANode *dtaNode = lookupDTA(ndta);
      if (dtaNode) {
        releaseDTA(ndta);
        DPRINTF("Existing DTA at %x released. DTA table elements: %d\n", ndta,
                countDTA());
      }
      nullifyDTA(memorySharedAddress);

      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_DTA_RELEASE,
                              countDTA());
      break;
    }
    case GEMDRVEMUL_FSFIRST_CALL: {
      uint32_t ndta = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // d3 register
      uint32_t attribs = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d4
      uint32_t fspecSTBufAddr =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d5
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);              // Skip d6 register
      char pattern[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      char internalPath[GEMDRIVE_FATFS_MAX_FOLDER_LENGTH] = {0};
      {
        char fspecString[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
        char tmpString[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
        char pathForwardslash[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};

        // Read the fspecSTBufAddr string swapping and changing the
        // endianness
        COPY_AND_CHANGE_ENDIANESS_BLOCK16(payloadPtr, tmpString,
                                          GEMDRIVE_MAX_FOLDER_LENGTH);
        DPRINTF("Fspec string: %s\n", tmpString);
        sdcard_back2ForwardSlash(tmpString);
        DPRINTF("Fspec string backslash: %s\n", tmpString);

        if (tmpString[1] == ':') {
          // If the path has the drive letter, jump two positions
          // and ignore the dpathStr
          snprintf(tmpString, GEMDRIVE_MAX_FOLDER_LENGTH, "%s", tmpString + 2);
          DPRINTF("New path_filename: %s\n", tmpString);
        }
        if (tmpString[0] == '/') {
          DPRINTF("Root folder found. Ignoring default path.\n");
          strcpy(fspecString, tmpString);
        } else {
          DPRINTF("Need to concatenate the default path: %s\n", dpathStr);
          snprintf(fspecString, sizeof(fspecString), "%s/%s", dpathStr,
                   tmpString);
          DPRINTF("Full fspecSTBufAddr string: %s\n", fspecString);
        }

        // Remove duplicated forward slashes
        sdcard_removeDupSlashes(fspecString);
        searchPath2ST(fspecString, internalPath, pathForwardslash, pattern);
        // Get the attributes string
        char attribsStr[7] = "";
        sdcard_getAttribsSTStr(attribsStr, attribs);

        DPRINTF(
            "FSFIRST params ndta: %x, attribs: %s, fspecSTBufAddr: %x, "
            "fspecSTBufAddr string: "
            "%s\n",
            ndta, attribsStr, fspecSTBufAddr, fspecString);
      }

      // Remove all the trailing spaces in the pattern
      remove_trailing_spaces(pattern);

      DTANode *currentDTANode = lookupDTA(ndta);

      if (currentDTANode) {
        DPRINTF("DTA at %x already exists.\n", ndta);
      } else {
        DPRINTF("Setting DTA: %x\n", ndta);
        int err = insertDTA(ndta);
        if (err == 0) {
          DPRINTF("Added ndta: %x.\n", ndta);
          currentDTANode = lookupDTA(ndta);
          DPRINTF("DTA at %x added.\n", ndta);
        } else {
          DPRINTF("Error adding ndta: %x. NOT POPULATING.\n", ndta);
        }
      }

      if (!(attribs & FS_ST_LABEL)) {
        attribs |= FS_ST_ARCH;
      }

      currentDTANode->attribs = attribs;

      if (currentDTANode->dj != NULL) {
        DPRINTF("DTA at %x already has a directory object. Freeing it\n", ndta);
        free(currentDTANode->dj);
        currentDTANode->dj = NULL;
      }
      DPRINTF("Creating new directory object\n");
      currentDTANode->dj = (DIR *)malloc(sizeof(DIR));

      // FILINFO is an output structure
      FILINFO fno = {0};

      size_t len = strlen(pattern) + 1;  // include terminator
      char *buf = malloc(len);           // single allocation
      if (!buf) {
        DPRINTF("Error allocating memory for buf\n");
        currentDTANode->pat = NULL;
        currentDTANode->dj->pat = NULL;
      } else {
        memcpy(buf, pattern, len);  // one copy
        currentDTANode->pat = buf;
        currentDTANode->dj->pat = buf;
      }

      DPRINTF("Fsfirst Full internal path: %s, filename pattern: %s[%d]\n",
              internalPath, pattern, strlen(pattern));

      FRESULT fr = f_opendir(currentDTANode->dj, internalPath);
      char rawFname[2] = "._";
      while (fr == FR_OK && ((rawFname[0] == '.') ||
                             (rawFname[0] == '.' && rawFname[1] == '_'))) {
        //          fr = f_findnext(currentDTANode->dj,
        //          currentDTANode->fno);
        for (;;) {
          fr = f_readdir(currentDTANode->dj, &fno); /* Get a directory item */
          if (fr != FR_OK || !fno.fname[0]) {
            DPRINTF("Nothing returned from f_readdir: %d\n", fr);
            break;
          }
          // DPRINTF("f_readdir: '%s.'", fno.fname);
          if (pattern_match(currentDTANode->pat, fno.fname, 0, FIND_RECURS)) {
            // DPRINTFRAW("MATCH: %s\n", fno.fname);
            break;
          } else {
            // DPRINTFRAW("NOT MATCH: %s\n", fno.fname);
          }
        }

        DPRINTF("Fsfirst fr: %d and filename: %s\n", fr, fno.fname);
        if (fno.fname[0]) {
          if (attribs & sdcard_attribsFAT2ST(fno.fattrib)) {
            if (fr == FR_OK) {
              rawFname[0] = fno.fname[0];
              rawFname[1] = fno.fname[1];
            }
          }
        } else {
          rawFname[0] = 'x';  // Force exit, no more elements
          rawFname[1] = 'x';  // Force exit, no more elements
        }
      }

      if (fr == FR_OK && fno.fname[0]) {
        if (fno.altname[0] == 0) {
          // Copy the fname to altname. It's already a 8.3 file format
          memcpy(fno.altname, fno.fname, sizeof(fno.altname));
        }
        uint8_t attribsConvST = sdcard_attribsFAT2ST(fno.fattrib);
        char attribsStr[7] = "";
        sdcard_getAttribsSTStr(attribsStr, attribsConvST);
        char shortenFname[14];
        char upperFilename[14];
        char filteredFilename[14];
        sdcard_filterFname(fno.altname, filteredFilename);
        sdcard_upperFname(filteredFilename, upperFilename);
        sdcard_shortenFname(upperFilename, shortenFname);

        strcpy(fno.altname, shortenFname);

        // Filter out elements that do not match the attributes
        if (attribsConvST & attribs) {
          DPRINTF("Found: %s, attr: %s\n", fno.altname, attribsStr);
          populateDTA(memorySharedAddress, ndta, GEMDOS_EFILNF, &fno);
          DPRINTF("DTA at %x populated with: %s\n", ndta, fno.altname);
        } else {
          DPRINTF("Skipped: %s, attr: %s\n", fno.altname, attribsStr);
          int16_t errorCode = GEMDOS_EFILNF;
          DPRINTF("DTA at %x showing error code: %x\n", ndta, errorCode);
          WRITE_WORD(memorySharedAddress, GEMDRIVE_DTA_F_FOUND, errorCode);
          if (currentDTANode) {
            releaseDTA(ndta);
            DPRINTF("Existing DTA at %x released. DTA table elements: %d\n",
                    ndta, countDTA());
            nullifyDTA(memorySharedAddress);
          }
        }
      } else {
        f_closedir(currentDTANode->dj);
        if (currentDTANode->dj != NULL) {
          free(currentDTANode->dj);
          currentDTANode->dj = NULL;
        }
        DPRINTF("Nothing returned from Fsfirst\n");
        int16_t errorCode = GEMDOS_EFILNF;
        DPRINTF("DTA at %x showing error code: %x\n", ndta, errorCode);
        if (currentDTANode) {
          releaseDTA(ndta);
          DPRINTF("Existing DTA at %x released. DTA table elements: %d\n", ndta,
                  countDTA());
        }
        WRITE_WORD(memorySharedAddress, GEMDRIVE_DTA_F_FOUND, errorCode);
        nullifyDTA(memorySharedAddress);
      }
      break;
    }
    case GEMDRVEMUL_FSNEXT_CALL: {
      uint32_t ndta = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // d3 register
      DPRINTF("Fsnext ndta: %x\n", ndta);

      FRESULT fr; /* Return value */
      DTANode *dtaNode = lookupDTA(ndta);

      bool ndtaExists = dtaNode ? true : false;
      if (dtaNode != NULL && dtaNode->dj != NULL && ndtaExists) {
        uint32_t attribs = dtaNode->attribs;
        if (!(attribs & FS_ST_LABEL)) {
          attribs |= FS_ST_ARCH;
        }

        // We need to filter out the elements that does not make sense in
        // the FsFat environment And in the Atari ST environment
        char rawFilaname[2] = "._";
        fr = FR_OK;
        FILINFO fno = {0};
        while (fr == FR_OK &&
               ((rawFilaname[0] == '.') ||
                (rawFilaname[0] == '.' && rawFilaname[1] == '_'))) {
          // fr = f_findnext(dtaNode->dj, dtaNode->fno);
          for (;;) {
            fr = f_readdir(dtaNode->dj, &fno); /* Get a directory item */
            if (fr != FR_OK || !fno.fname[0]) {
              DPRINTF("Nothing returned from f_readdir: %d\n", fr);
              break;
            }
            // DPRINTF("f_readdir: '%s.'", fno.fname);
            if (pattern_match(dtaNode->pat, fno.fname, 0, FIND_RECURS)) {
              // DPRINTFRAW("MATCH: %s\n", fno.fname);
              break;
            } else {
              // DPRINTFRAW("NOT MATCH: %s\n", fno.fname);
            }
          }

          if (fr != FR_OK) {
            DPRINTF("ERROR: Could not find next file (%d)\r\n", fr);
          }
          DPRINTF("Fsnext fr: %d and filename: %s\n", fr, fno.fname);
          if (fno.fname[0]) {
            if (attribs & sdcard_attribsFAT2ST(fno.fattrib)) {
              if (fr == FR_OK) {
                rawFilaname[0] = fno.fname[0];
                rawFilaname[1] = fno.fname[1];
              }
            }
          } else {
            rawFilaname[0] = 'X';  // Force exit, no more elements
            rawFilaname[1] = 'X';  // Force exit, no more elements
          }
        }
        if (fr == FR_OK && fno.fname[0]) {
          if (fno.altname[0] == 0) {
            // Copy the fname to altname. It's already a 8.3 file format
            memcpy(fno.altname, fno.fname, sizeof(fno.altname));
          }
          DPRINTF("Found: %s\n", fno.altname);
          char shortenFilename[14];
          char upperFilename[14];
          char filteredFilename[14];
          sdcard_filterFname(fno.altname, filteredFilename);
          sdcard_upperFname(filteredFilename, upperFilename);
          sdcard_shortenFname(upperFilename, shortenFilename);
          strcpy(fno.altname, shortenFilename);

          uint8_t attribs = fno.fattrib;
          uint8_t attribsConvST = sdcard_attribsFAT2ST(attribs);
          if (!(attribs & (FS_ST_LABEL))) {
            attribs |= FS_ST_ARCH;
          }
          char attribsStr[7] = "";
          sdcard_getAttribsSTStr(attribsStr, attribsConvST);
          DPRINTF("Found: %s, attr: %s\n", fno.altname, attribsStr);
          // Populate the DTA with the next file found
          populateDTA(memorySharedAddress, ndta, GEMDOS_ENMFIL, &fno);
        } else {
          f_closedir(dtaNode->dj);
          if (dtaNode->dj != NULL) {
            free(dtaNode->dj);
            dtaNode->dj = NULL;
          }
          DPRINTF("Nothing found\n");
          int16_t errorCode = GEMDOS_ENMFIL;
          DPRINTF("DTA at %x showing error code: %x\n", ndta, errorCode);
          WRITE_WORD(memorySharedAddress, GEMDRIVE_DTA_F_FOUND, errorCode);
          if (ndtaExists) {
            releaseDTA(ndta);
            DPRINTF("Existing DTA at %x released. DTA table elements: %d\n",
                    ndta, countDTA());
          }
          nullifyDTA(memorySharedAddress);
        }
      } else {
        f_closedir(dtaNode->dj);
        if (dtaNode->dj != NULL) {
          free(dtaNode->dj);
          dtaNode->dj = NULL;
        }
        DPRINTF("FsFirst not initalized\n");
        int16_t errorCode = GEMDOS_EINTRN;
        DPRINTF("DTA at %x showing error code: %x\n", ndta, errorCode);
        WRITE_WORD(memorySharedAddress, GEMDRIVE_DTA_F_FOUND, errorCode);
        if (ndtaExists) {
          releaseDTA(ndta);
          DPRINTF("Existing DTA at %x released. DTAtable elements: %d\n", ndta,
                  countDTA());
        }
        nullifyDTA(memorySharedAddress);
      }
      break;
    }
    case GEMDRVEMUL_FOPEN_CALL: {
      uint16_t fopenMode = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d3
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d4
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d5

      char tmpFilepath[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      getLocalFullPathname(payloadPtr, tmpFilepath);
      DPRINTF("Opening file: %s with mode: %x\n", tmpFilepath, fopenMode);
      // Convert the fopenMode to FatFs mode
      DPRINTF("Fopen mode: %x\n", fopenMode);
      BYTE FatFSOpenMode = 0;
      switch (fopenMode) {
        case 0:  // Read only
          FatFSOpenMode = FA_READ;
          break;
        case 1:  // Write only
          FatFSOpenMode = FA_WRITE;
          break;
        case 2:  // Read/Write
          FatFSOpenMode = FA_READ | FA_WRITE;
          break;
        default:
          DPRINTF("ERROR: Invalid mode: %x\n", fopenMode);
          WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FOPEN_HANDLE,
                                  GEMDOS_EACCDN);
          break;
      }
      DPRINTF("FatFs open mode: %x\n", FatFSOpenMode);
      if (fopenMode <= 2) {
        // Open the file with FatFs
        FIL fobj;
        FRESULT fr = f_open(&fobj, tmpFilepath, FatFSOpenMode);
        if (fr != FR_OK) {
          DPRINTF("ERROR: Could not open file (%d)\r\n", fr);
          WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FOPEN_HANDLE,
                                  GEMDOS_EFILNF);
        } else {
          // Add the file to the list of open files
          int fdCount = getFirstAvailableFD(fdescriptors);
          DPRINTF("Opening file with new file descriptor: %d\n", fdCount);
          FileDescriptors *newFDescriptor = malloc(sizeof(FileDescriptors));
          if (newFDescriptor == NULL) {
            DPRINTF("Memory allocation failed for new FileDescriptors\n");
            DPRINTF("ERROR: Could not add file to the list of open files\n");
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FOPEN_HANDLE,
                                    GEMDOS_EINTRN);
          } else {
            addFile(&fdescriptors, newFDescriptor, tmpFilepath, fobj, fdCount);

            // Initialize the file offset
            FileDescriptors *file = getFileByFD(fdescriptors, fdCount);
            if (file != NULL) {
              file->offset = 0;  // Initialize the offset to 0
              DPRINTF("File offset initialized to 0 for fd: %d\n", fdCount);
            } else {
              DPRINTF("ERROR: Could not find file descriptor %d\n", fdCount);
            }

            DPRINTF("File opened with file descriptor: %d\n", fdCount);
            // Return the file descriptor
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FOPEN_HANDLE,
                                    fdCount);
          }
        }
      }
      break;
    }
    case GEMDRVEMUL_FCLOSE_CALL: {
      uint16_t fcloseFD = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      DPRINTF("Closing file with fd: %x\n", fcloseFD);
      // Obtain the file descriptor
      FileDescriptors *file = getFileByFD(fdescriptors, fcloseFD);
      uint16_t exitCode = GEMDOS_EOK;
      if (file == NULL) {
        DPRINTF("ERROR: File descriptor not found\n");
        exitCode = GEMDOS_EIHNDL;
      } else {
        // Close the file with FatFs
        FRESULT ferr = f_close(&file->fobject);
        if (ferr == FR_INVALID_OBJECT) {
          DPRINTF("ERROR: File descriptor is not valid\n");
          exitCode = GEMDOS_EIHNDL;
        } else if (ferr != FR_OK) {
          DPRINTF("ERROR: Could not close file (%d)\r\n", ferr);
          exitCode = GEMDOS_EINTRN;
        } else {
          // Remove the file from the list of open files
          deleteFileByFD(&fdescriptors, fcloseFD);
          DPRINTF("File closed\n");
        }
      }
      WRITE_WORD(memorySharedAddress, GEMDRIVE_FCLOSE_STATUS, exitCode);
      break;
    }

    case GEMDRVEMUL_FCREATE_CALL: {
      uint16_t fCreateMode =
          TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // d3 register
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);       // skip d3
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);       // skip d4
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);       // skip d5

      // Obtain the fname string and keep it in memory
      // concatenated path and filename
      char tmpFilepath[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      getLocalFullPathname(payloadPtr, tmpFilepath);
      DPRINTF("Creating file: %s with mode: %x\n", tmpFilepath, fCreateMode);

      // CREATE ALWAYS MODE
      BYTE fatFSCreateMode = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;
      DPRINTF("FatFs create mode: %x\n", fatFSCreateMode);

      // Open the file with FatFs
      FIL fObj;
      FRESULT ferr = f_open(&fObj, tmpFilepath, fatFSCreateMode);
      uint16_t errorCode = GEMDOS_EOK;
      if (ferr != FR_OK) {
        DPRINTF("ERROR: Could not create file (%d)\r\n", ferr);
        errorCode = GEMDOS_EPTHNF;
      } else {
        // Add the file to the list of open files
        int fdCounter = getFirstAvailableFD(fdescriptors);
        DPRINTF("File created with file descriptor: %d\n", fdCounter);
        FileDescriptors *newFDescriptor = malloc(sizeof(FileDescriptors));
        if (newFDescriptor == NULL) {
          DPRINTF("Memory allocation failed for new FileDescriptors\n");
          DPRINTF("ERROR: Could not add file to the list of open files\n");
          errorCode = GEMDOS_EINTRN;
        } else {
          addFile(&fdescriptors, newFDescriptor, tmpFilepath, fObj, fdCounter);

          // Initialize the file offset
          FileDescriptors *file = getFileByFD(fdescriptors, fdCounter);
          if (file != NULL) {
            file->offset = 0;  // Initialize the offset to 0
            DPRINTF("File offset initialized to 0 for fd: %d\n", fdCounter);
          } else {
            DPRINTF("ERROR: Could not find file descriptor %d\n", fdCounter);
          }

          // MISSING ATTRIBUTE MODIFICATION
          char fattrSTStr[7] = "";
          sdcard_getAttribsSTStr(fattrSTStr, fCreateMode);
          DPRINTF("New file attributes: %s\n", fattrSTStr);
          BYTE fattrFatFSNew = (BYTE)sdcard_attribsST2FAT(fCreateMode);
          ferr = f_chmod(tmpFilepath, fattrFatFSNew, AM_RDO | AM_HID | AM_SYS);
          if (ferr != FR_OK) {
            DPRINTF("ERROR: Could not set file attributes (%d)\r\n", ferr);
            errorCode = GEMDOS_EACCDN;
          }

          // Return the file descriptor
          errorCode = fdCounter;
        }
      }
      WRITE_WORD(memorySharedAddress, GEMDRIVE_FCREATE_HANDLE, errorCode);

      break;
    }
    case GEMDRVEMUL_FDELETE_CALL: {
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d3
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d4
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d5
      // Obtain the fname string and keep it in memory
      // concatenated path and filename
      char tmpFilePath[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      getLocalFullPathname(payloadPtr, tmpFilePath);
      uint32_t status = GEMDOS_EOK;
      // Check first if the file is open. If so, cancel the operation
      FileDescriptors *file = getFileByPath(fdescriptors, tmpFilePath);
      if (file != NULL) {
        DPRINTF("File is open. Access denied. Cancelling operation\n");
        status = GEMDOS_EACCDN;
      }
      // If the file was open and it was not possible to close it, return an
      // error
      if (status == GEMDOS_EOK) {
        DPRINTF("Deleting file: %s\n", tmpFilePath);
        // Delete the file
        FRESULT ferr = f_unlink(tmpFilePath);
        if (ferr != FR_OK) {
          DPRINTF("ERROR: Could not delete file (%d)\r\n", ferr);
          if (ferr == FR_DENIED) {
            DPRINTF("ERROR: Not enough permissions to delete file\n");
            status = GEMDOS_EACCDN;
          } else if (ferr == FR_NO_PATH) {
            DPRINTF("ERROR: Folder does not exist\n");
            status = GEMDOS_EPTHNF;
          } else if (ferr == FR_NO_FILE) {
            DPRINTF("ERROR: File does not exist\n");
            // status = GEMDOS_EFILNF;
            status = GEMDOS_EOK;
          } else {
            DPRINTF("ERROR: Internal error\n");
            status = GEMDOS_EINTRN;
          }
        } else {
          DPRINTF("File deleted\n");
          status = GEMDOS_EOK;
        }
      }
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FDELETE_STATUS,
                              status);
      break;
    }
    case GEMDRVEMUL_FSEEK_CALL: {
      uint16_t fd =
          TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // file descriptor (d3)
      int32_t off =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // offset (d4)
      uint16_t mode =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM16(payloadPtr);  // mode (d5)

      FileDescriptors *file = getFileByFD(fdescriptors, fd);
      if (!file) {
        DPRINTF("ERROR: FD %u not found\n", fd);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FSEEK_STATUS,
                                GEMDOS_EIHNDL);
        break;
      }

      // Determine file size for boundary checks
      FSIZE_t size = f_size(&file->fobject);
      int32_t newOff = (int32_t)file->offset;
      bool isValid = true;
      switch (mode) {
        case SEEK_SET:  // SEEK_SET 0 offset specifies the positive number of
                        // bytes from the beginning of the file
          if (off > size) {
            newOff = size;
          } else {
            if (off < 0) {
              newOff = 0;
            } else {
              newOff = off;
            }
          }
          break;
        case SEEK_CUR:  // SEEK_CUR 1 offset specifies offset specifies the
                        // negative or positive number of bytes from the
                        // current file position
          if (off < 0) {
            newOff = (newOff + off < 0) ? 0 : newOff + off;
          } else {
            newOff = (newOff + off > size) ? size : newOff + off;
          }
          break;
        case SEEK_END:  // SEEK_END 2 offset specifies the positive number of
                        // bytes from the end of the file
          if (off <= 0) {
            newOff = (size + off < 0) ? 0 : size + off;
          }
          break;
        default:
          DPRINTF("ERROR: Invalid seek mode %u\n", mode);
          WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FSEEK_STATUS,
                                  GEMDOS_EACCDN);
          isValid = false;
      }
      if (isValid) {
        // Clamp to valid range
        file->offset = (FSIZE_t)newOff;
        DPRINTF("Seek FD %u, offset %x\n", fd, (FSIZE_t)newOff);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FSEEK_STATUS,
                                file->offset);
      }
      break;
    }
    case GEMDRVEMUL_FATTRIB_CALL: {
      uint16_t fattrFlag =
          TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // d3 register
      // Obtain the new attributes, if FATTRIB_SET is set
      uint16_t fattrNew =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM16(payloadPtr);  // d4 register
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);              // Skip d5 register
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);              // Skip d6 register

      // Obtain the fname string and keep it in memory
      // concatenated path and filename
      char tmpFPath[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      getLocalFullPathname(payloadPtr, tmpFPath);
      DPRINTF("Fattrib flag: %x, new attributes: %x\n", fattrFlag, fattrNew);
      DPRINTF("Getting attributes of file: %s\n", tmpFPath);

      // Get the attributes of the file
      FILINFO fno;
      FRESULT fr = f_stat(tmpFPath, &fno);
      uint32_t errorCode = GEMDOS_EOK;
      if (fr != FR_OK) {
        DPRINTF("ERROR: Could not get file attributes (%d)\r\n", fr);
        errorCode = GEMDOS_EFILNF;

      } else {
        uint32_t fattrST = sdcard_attribsFAT2ST(fno.fattrib);
        errorCode = fattrST;
        char fattrSTStr[7] = "";
        sdcard_getAttribsSTStr(fattrSTStr, fattrST);
        if (fattrFlag == FATTRIB_INQUIRE) {
          DPRINTF("File attributes: %s\n", fattrSTStr);
        } else {
          // WE will assume here FATTRIB_SET
          // Set the attributes of the file
          char fattrSTStr[7] = "";
          sdcard_getAttribsSTStr(fattrSTStr, fattrNew);
          DPRINTF("New file attributes: %s\n", fattrSTStr);
          BYTE fattrFatFSNew = (BYTE)sdcard_attribsST2FAT(fattrNew);
          fr = f_chmod(tmpFPath, fattrFatFSNew, AM_RDO | AM_HID | AM_SYS);
          if (fr != FR_OK) {
            DPRINTF("ERROR: Could not set file attributes (%d)\r\n", fr);
            errorCode = GEMDOS_EACCDN;
          }
        }
      }
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FATTRIB_STATUS,
                              errorCode);
      break;
    }
    case GEMDRVEMUL_FRENAME_CALL: {
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d3
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d4
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d5
      // Obtain the src name from the payload
      char *origin = (char *)payloadPtr;
      char frename_fname_src[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      char frename_fname_dst[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      COPY_AND_CHANGE_ENDIANESS_BLOCK16(origin, frename_fname_src,
                                        GEMDRIVE_MAX_FOLDER_LENGTH);
      COPY_AND_CHANGE_ENDIANESS_BLOCK16(origin + GEMDRIVE_MAX_FOLDER_LENGTH,
                                        frename_fname_dst,
                                        GEMDRIVE_MAX_FOLDER_LENGTH);
      // DPRINTF("Renaming file: %s to %s\n", frename_fname_src,
      // frename_fname_dst);
      // getLocalFullPathname(payloadPtr,frename_fname_src);
      //  getLocalFullPathname(payloadPtr,frename_fname_dst);
      // DPRINTF("Renaming file: %s to %s\n", frename_fname_src,
      // frename_fname_dst);

      char drive_src[3] = {0};
      char folders_src[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      char filePattern_src[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      char drive_dst[3] = {0};
      char folders_dst[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      char filePattern_dst[GEMDRIVE_MAX_FOLDER_LENGTH] = {0};
      int statusCode = GEMDOS_EOK;
      sdcard_splitFullpath(frename_fname_src, drive_src, folders_src,
                           filePattern_src);
      DPRINTF("Drive: %s, Folders: %s, FilePattern: %s\n", drive_src,
              folders_src, filePattern_src);
      sdcard_splitFullpath(frename_fname_dst, drive_dst, folders_dst,
                           filePattern_dst);
      DPRINTF("Drive: %s, Folders: %s, FilePattern: %s\n", drive_dst,
              folders_dst, filePattern_dst);

      if (strcasecmp(drive_src, drive_dst) != 0) {
        DPRINTF("ERROR: Different drives\n");

        statusCode = GEMDOS_EPTHNF;
      } else {
        DPRINTF("Renaming file: %s to %s\n", frename_fname_src,
                frename_fname_dst);
        getLocalFullPathname(payloadPtr, frename_fname_src);
        payloadPtr += GEMDRIVE_MAX_FOLDER_LENGTH /
                      2;  // GEMDRIVE_MAX_FOLDER_LENGTH * 2 bytes per uint16_t
        getLocalFullPathname(payloadPtr, frename_fname_dst);
        DPRINTF("Renaming file: %s to %s\n", frename_fname_src,
                frename_fname_dst);
        // Rename the file
        FRESULT fr = f_rename(frename_fname_src, frename_fname_dst);
        if (fr != FR_OK) {
          DPRINTF("ERROR: Could not rename file (%d)\r\n", fr);
          if (fr == FR_DENIED) {
            DPRINTF("ERROR: Not enough premissions to rename file\n");
            statusCode = GEMDOS_EACCDN;
          } else if (fr == FR_NO_PATH) {
            DPRINTF("ERROR: Folder does not exist\n");
            statusCode = GEMDOS_EPTHNF;
          } else if (fr == FR_NO_FILE) {
            DPRINTF("ERROR: File does not exist\n");
            statusCode = GEMDOS_EFILNF;
          } else if (fr == FR_EXIST) {
            DPRINTF("ERROR: File already exists\n");
            statusCode = GEMDOS_EACCDN;
          } else {
            DPRINTF("ERROR: Internal error\n");
            statusCode = GEMDOS_EINTRN;
          }
        } else {
          DPRINTF("File renamed\n");
          statusCode = GEMDOS_EOK;
        }
      }
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FRENAME_STATUS,
                              statusCode);
      break;
    }
    case GEMDRVEMUL_FDATETIME_CALL: {
      uint16_t fdatetimeFlag =
          TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // d3 register
      uint16_t fdatetimeFD =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM16(payloadPtr);  // d4 register
      uint16_t DOSDate =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM16(payloadPtr);  // d5 low register
      uint16_t DOSTime =
          TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);  // d5 high register
      DPRINTF("Fdatetime flag: %x, fd: %x, time: %x, date: %x\n", fdatetimeFlag,
              fdatetimeFD, DOSTime, DOSDate);

      FileDescriptors *fDes = getFileByFD(fdescriptors, fdatetimeFD);
      if (fDes == NULL) {
        DPRINTF("ERROR: File descriptor not found\n");
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FDATETIME_STATUS,
                                GEMDOS_EIHNDL);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FDATETIME_DATE,
                                0);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_FDATETIME_TIME,
                                0);
      } else {
        if (fdatetimeFlag == FDATETIME_INQUIRE) {
          DPRINTF("Inquire file date and time: %s fd: %d\n", fDes->fpath,
                  fdatetimeFD);
          FILINFO fno;
          FRESULT ferr;
          ferr = f_stat(fDes->fpath, &fno);
          if (ferr == FR_OK) {
            // File information is now in fno
#if defined(_DEBUG) && (_DEBUG != 0)
            // Save some memory and cycles if not in debug mode
            // Convert the date and time
            unsigned int year = (fno.fdate >> 9);
            unsigned int month = (fno.fdate >> 5) & 0x0F;
            unsigned int day = fno.fdate & 0x1F;

            unsigned int hour = fno.ftime >> 11;
            unsigned int minute = (fno.ftime >> 5) & 0x3F;
            unsigned int second = (fno.ftime & 0x1F);

            DPRINTF("Get file date and time: %02d:%02d:%02d %02d/%02d/%02d\n",
                    hour, minute, second * 2, day, month, year + 1980);
#endif
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_STATUS, GEMDOS_EOK);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_DATE, fno.fdate);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_TIME, fno.ftime);
          } else {
            DPRINTF(
                "ERROR: Could not get file date and time from file %s "
                "(%d)\r\n",
                fDes->fpath, ferr);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_STATUS, GEMDOS_EFILNF);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_DATE, 0);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_TIME, 0);
          }
        } else {
          DPRINTF("Modify file date and time: %s fd: %d\n", fDes->fpath,
                  fdatetimeFD);
#if defined(_DEBUG) && (_DEBUG != 0)
          // Save some memory and cycles if not in debug mode
          // Convert the date and time
          unsigned int year = (DOSDate >> 9);
          unsigned int month = (DOSDate >> 5) & 0x0F;
          unsigned int day = DOSDate & 0x1F;

          unsigned int hour = DOSTime >> 11;
          unsigned int minute = (DOSTime >> 5) & 0x3F;
          unsigned int second = (DOSTime & 0x1F);

          DPRINTF("Show in hex the values: %02x:%02x:%02x %02x/%02x/%02x\n",
                  hour, minute, second, day, month, year);
          DPRINTF("File date and time: %02d:%02d:%02d %02d/%02d/%02d\n", hour,
                  minute, second * 2, day, month, year + 1980);
#endif
          FILINFO fno;
          fno.fdate = DOSDate;
          fno.ftime = DOSTime;
          FRESULT ferr = f_utime(fDes->fpath, &fno);
          if (ferr == FR_OK) {
            // File exists and date and time set
            // So now we can return the status
            DPRINTF(
                "Set the file date and time: %02d:%02d:%02d "
                "%02d/%02d/%02d\n",
                hour, minute, second * 2, day, month, year + 1980);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_STATUS, GEMDOS_EOK);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_DATE, 0);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_TIME, 0);
          } else {
            DPRINTF(
                "ERROR: Could not set file date and time to file %s "
                "(%d)\r\n",
                fDes->fpath, ferr);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_STATUS, GEMDOS_EFILNF);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_DATE, 0);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                    GEMDRIVE_FDATETIME_TIME, 0);
          }
        }
      }
      break;
    }

    case GEMDRVEMUL_READ_BUFF_CALL: {
      uint16_t fd =
          TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // file descriptor (d3)
      uint32_t totalBytes = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(
          payloadPtr);  // total bytes requested (d4)
      uint32_t pendingBytes = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(
          payloadPtr);  // pending bytes to read (d5)

      DPRINTF("Read buffer FD=%u, total=0x%08x, pending=0x%08x\n", fd,
              totalBytes, pendingBytes);

      // Show open files
#if defined(_DEBUG) && (_DEBUG != 0)
      printFDs(fdescriptors);
#endif
      // Obtain the file descriptor
      FileDescriptors *file = getFileByFD(fdescriptors, fd);
      if (!file) {
        DPRINTF("ERROR: FD %u not found\n", fd);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_READ_BYTES,
                                GEMDOS_EIHNDL);
        break;
      }

      // Seek to current offset before reading
      FSIZE_t offset = file->offset;
      FRESULT res = f_lseek(&file->fobject, offset);
      if (res != FR_OK) {
        DPRINTF("ERROR: f_lseek failed (%d)\n", res);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_READ_BYTES,
                                GEMDOS_EINTRN);
        break;
      }

      // Determine how many bytes to read this call
      UINT toRead = (pendingBytes > DEFAULT_FOPEN_READ_BUFFER_SIZE)
                        ? DEFAULT_FOPEN_READ_BUFFER_SIZE
                        : pendingBytes;
      DPRINTF("Reading 0x%x bytes at offset 0x%x\n", toRead, offset);

      // Zero-fill the shared buffer region
      // memset((void *)(memorySharedAddress + GEMDRIVE_READ_BUFF), 0,
      //        DEFAULT_FOPEN_READ_BUFFER_SIZE);
      UINT bytesRead = 0;
      res = f_read(&file->fobject,
                   (void *)(memorySharedAddress + GEMDRIVE_READ_BUFF), toRead,
                   &bytesRead);
      if (res != FR_OK) {
        DPRINTF("ERROR: f_read failed (%d)\n", res);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_READ_BYTES,
                                GEMDOS_EINTRN);
      } else {
        // Advance internal offset
        file->offset += bytesRead;
        uint32_t newOffset = file->offset;
        DPRINTF("Read %u bytes, new offset=0x%x\n", bytesRead, newOffset);

        // Ensure proper endianness for ST
        CHANGE_ENDIANESS_BLOCK16(memorySharedAddress + GEMDRIVE_READ_BUFF,
                                 toRead + (toRead & 1));

        // Return actual bytes read
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_READ_BYTES,
                                bytesRead);
      }
      break;
    }
    case GEMDRVEMUL_WRITE_BUFF_CALL: {
      uint16_t writebuff_fd =
          TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // d3 register
      uint32_t writebuff_bytes_to_write =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d4
      uint32_t writebuff_pending_bytes_to_write =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d5
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);              // skip d5 register
      DPRINTF(
          "Write buffering file with fd: x%x, bytes_to_write: x%08x, "
          "pending_bytes_to_write: x%08x\n",
          writebuff_fd, writebuff_bytes_to_write,
          writebuff_pending_bytes_to_write);
      // Obtain the file descriptor
      FileDescriptors *file = getFileByFD(fdescriptors, writebuff_fd);
      if (file == NULL) {
        DPRINTF("ERROR: File descriptor not found\n");
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_WRITE_BYTES,
                                GEMDOS_EIHNDL);
      } else {
        uint32_t writebuff_offset = file->offset;
        UINT bytes_write = 0;
        // Reposition the file pointer with FatFs
        FRESULT ferr = f_lseek(&file->fobject, writebuff_offset);
        if (ferr != FR_OK) {
          DPRINTF("ERROR: Could not change write offset of the file (%d)\r\n",
                  ferr);
          WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_WRITE_BYTES,
                                  GEMDOS_EINTRN);
        } else {
          // Only write DEFAULT_FWRITE_BUFFER_SIZE bytes at a time
          uint16_t buff_size =
              writebuff_pending_bytes_to_write > DEFAULT_FWRITE_BUFFER_SIZE
                  ? DEFAULT_FWRITE_BUFFER_SIZE
                  : writebuff_pending_bytes_to_write;
          // Transform buffer's words from little endian to big endian
          // inline
          uint16_t *target = payloadPtr;
          // Change the endianness of the bytes read
          CHANGE_ENDIANESS_BLOCK16(target, (buff_size + 1) & ~1);
          // Write the bytes
          DPRINTF("Write x%x bytes from the file at offset x%x\n", buff_size,
                  writebuff_offset);
          ferr =
              f_write(&file->fobject, (void *)target, buff_size, &bytes_write);
          if (ferr != FR_OK) {
            DPRINTF("ERROR: Could not write file (%d)\r\n", ferr);
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_WRITE_BYTES,
                                    GEMDOS_EINTRN);
          } else {
            // Update the offset of the file
            file->offset += buff_size;
            WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_WRITE_BYTES,
                                    bytes_write);
          }
        }
      }
      break;
    }
    case GEMDRVEMUL_WRITE_BUFF_CHECK: {
      uint16_t writebuff_fd =
          TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // d3 register
      uint32_t writebuff_forward_bytes =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d4 register

      DPRINTF("Write buffering confirm fd: x%x, forward: x%08x\n", writebuff_fd,
              writebuff_forward_bytes);
      // Obtain the file descriptor
      FileDescriptors *file = getFileByFD(fdescriptors, writebuff_fd);
      if (file == NULL) {
        DPRINTF("ERROR: File descriptor not found\n");
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                GEMDRIVE_WRITE_CONFIRM_STATUS, GEMDOS_EIHNDL);
      } else {
        // Update the offset of the file
        file->offset += writebuff_forward_bytes;
        uint32_t current_offset = file->offset;
        DPRINTF("New offset: x%x after writing x%x bytes\n", current_offset,
                writebuff_forward_bytes);
        WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                                GEMDRIVE_WRITE_CONFIRM_STATUS, GEMDOS_EOK);
      }
      break;
    }
    case GEMDRVEMUL_PEXEC_CALL: {
      uint16_t pexec_mode = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      uint32_t pexec_stack_addr =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d4 register
      uint32_t pexec_fname =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d5 register
      DPRINTF("Pexec mode: %x\n", pexec_mode);
      DPRINTF("Pexec stack addr: %x\n", pexec_stack_addr);
      DPRINTF("Pexec fname: %x\n", pexec_fname);
      WRITE_WORD(memorySharedAddress, GEMDRIVE_PEXEC_MODE, pexec_mode);
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_PEXEC_STACK_ADDR,
                              pexec_stack_addr);
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_PEXEC_FNAME,
                              pexec_fname);
      break;
    }
    case GEMDRVEMUL_PEXEC2_CALL: {
      uint32_t pexec_cmdline =
          TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // d6 register
      uint32_t pexec_envstr =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d7 register
      DPRINTF("Pexec cmdline: %x\n", pexec_cmdline);
      DPRINTF("Pexec envstr: %x\n", pexec_envstr);
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_PEXEC_CMDLINE,
                              pexec_cmdline);
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, GEMDRIVE_PEXEC_ENVSTR,
                              pexec_envstr);
      break;
    }
    case GEMDRVEMUL_SAVE_BASEPAGE: {
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d3
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d4
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d5
      // Copy the from the shared memory the basepagea está to pexec_pd
      DPRINTF("Saving basepage\n");
      PD *origin = (PD *)(payloadPtr);
      // Reserve and copy the memory from origin to pexec_pd
      if (pexec_pd == NULL) {
        pexec_pd = (PD *)(memorySharedAddress + GEMDRIVE_EXEC_PD);
      }
      memcpy(pexec_pd, origin, sizeof(PD));
      DPRINTF("pexec_pd->p_lowtpa: %x\n", SWAP_LONGWORD(pexec_pd->p_lowtpa));
      DPRINTF("pexec_pd->p_hitpa: %x\n", SWAP_LONGWORD(pexec_pd->p_hitpa));
      DPRINTF("pexec_pd->p_tbase: %x\n", SWAP_LONGWORD(pexec_pd->p_tbase));
      DPRINTF("pexec_pd->p_tlen: %x\n", SWAP_LONGWORD(pexec_pd->p_tlen));
      DPRINTF("pexec_pd->p_dbase: %x\n", SWAP_LONGWORD(pexec_pd->p_dbase));
      DPRINTF("pexec_pd->p_dlen: %x\n", SWAP_LONGWORD(pexec_pd->p_dlen));
      DPRINTF("pexec_pd->p_bbase: %x\n", SWAP_LONGWORD(pexec_pd->p_bbase));
      DPRINTF("pexec_pd->p_blen: %x\n", SWAP_LONGWORD(pexec_pd->p_blen));
      DPRINTF("pexec_pd->p_xdta: %x\n", SWAP_LONGWORD(pexec_pd->p_xdta));
      DPRINTF("pexec_pd->p_parent: %x\n", SWAP_LONGWORD(pexec_pd->p_parent));
      DPRINTF("pexec_pd->p_hflags: %x\n", SWAP_LONGWORD(pexec_pd->p_hflags));
      DPRINTF("pexec_pd->p_env: %x\n", SWAP_LONGWORD(pexec_pd->p_env));
      DPRINTF("pexec_pd->p_1fill\n");
      DPRINTF("pexec_pd->p_curdrv: %x\n", SWAP_LONGWORD(pexec_pd->p_curdrv));
      DPRINTF("pexec_pd->p_uftsize: %x\n", SWAP_LONGWORD(pexec_pd->p_uftsize));
      DPRINTF("pexec_pd->p_uft: %x\n", SWAP_LONGWORD(pexec_pd->p_uft));
      DPRINTF("pexec_pd->p_cmdlin: %x\n", SWAP_LONGWORD(pexec_pd->p_cmdlin));
      break;
    }
    case GEMDRVEMUL_SAVE_EXEC_HEADER: {
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d3
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d4
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d5
      // Copy the from the shared memory the basepage to pexec_exec_header
      DPRINTF("Saving exec header\n");
      ExecHeader *origin = (ExecHeader *)(payloadPtr);
      // Reserve and copy the memory from origin to pexec_exec_header
      if (pexec_exec_header == NULL) {
        pexec_exec_header =
            (ExecHeader *)(memorySharedAddress + GEMDRIVE_EXEC_HEADER);
      }
      memcpy(pexec_exec_header, origin, sizeof(ExecHeader));
      DPRINTF("pexec_exec->magic: %x\n", pexec_exec_header->magic);
      DPRINTF("pexec_exec->text: %x\n",
              (uint32_t)(pexec_exec_header->text_h << 16 |
                         pexec_exec_header->text_l));
      DPRINTF("pexec_exec->data: %x\n",
              (uint32_t)(pexec_exec_header->data_h << 16 |
                         pexec_exec_header->data_l));
      DPRINTF("pexec_exec->bss: %x\n",
              (uint32_t)(pexec_exec_header->bss_h << 16 |
                         pexec_exec_header->bss_l));
      DPRINTF("pexec_exec->syms: %x\n",
              (uint32_t)(pexec_exec_header->syms_h << 16 |
                         pexec_exec_header->syms_l));
      DPRINTF("pexec_exec->reserved1: %x\n",
              (uint32_t)(pexec_exec_header->reserved1_h << 16 |
                         pexec_exec_header->reserved1_l));
      DPRINTF("pexec_exec->prgflags: %x\n",
              (uint32_t)(pexec_exec_header->prgflags_h << 16 |
                         pexec_exec_header->prgflags_l));
      DPRINTF("pexec_exec->absflag: %x\n", pexec_exec_header->absflag);
      break;
    }
    default: {
      DPRINTF("ERROR: Unknown command: %x\n", lastProtocol->command_id);
      uint32_t d3 = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // d3 register
      DPRINTF("DEBUG: %x\n", d3);
      uint32_t d4 =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d4 register
      DPRINTF("DEBUG: %x\n", d4);
      uint32_t d5 =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d5 register
      DPRINTF("DEBUG: %x\n", d5);
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);  // skip d5 register
      uint8_t *payloadShowBytesPtr = (uint8_t *)payloadPtr;
      printPayload(payloadShowBytesPtr);

      break;
    }
  }
}
