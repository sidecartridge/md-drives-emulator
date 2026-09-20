#include "sidecart.h"

// The Atari does not write to the device: it reads addresses in the ROM3
// window, and the device watches the bus and rebuilds a word from the address
// of every read. A command is a magic word, the command code, the payload size
// in bytes, the payload, and a checksum; the device answers by writing the
// token it was given into the exchange buffer, and then a new seed.
//
// This is the same protocol as send_sync in the cartridge firmware
// (target/atarist/src/inc/sidecart_functions.s), written in C because a test
// program is not part of that firmware. Only the no-argument form is here: a
// test asks the device to restart and nothing more.

#define ROM3_START 0xFB0000L
#define ROM_EXCHG_BUFFER 0xFA8200L  // ROM4 + $8200

#define CMD_MAGIC_NUMBER 0xABCD
#define CMD_RETRIES_COUNT 5
// Iterations of the wait loop below, not a time: the answer to a command with
// no work behind it comes back in microseconds, so this only has to be long
// enough to outlast an interrupt landing in the middle of one.
#define CMD_TIMEOUT 200000L

// A word is sent as the offset of a read, signed, from the middle of the
// window, so that all 16 bits of it address the 64 KB window.
static volatile unsigned char *const commandWindow =
    (volatile unsigned char *)(ROM3_START + 0x8000L);
static volatile unsigned long *const answerToken =
    (volatile unsigned long *)ROM_EXCHG_BUFFER;
static volatile unsigned long *const answerSeed =
    (volatile unsigned long *)(ROM_EXCHG_BUFFER + 4);

static void send_word(unsigned short value) {
  (void)commandWindow[(short)value];
}

int sidecart_send_command(unsigned short command) {
  for (int retry = 0; retry < CMD_RETRIES_COUNT; retry++) {
    // The seed the device last left is this command's token: the answer is the
    // device writing it back.
    unsigned long token = *answerSeed;
    unsigned short tokenLow = (unsigned short)token;
    unsigned short tokenHigh = (unsigned short)(token >> 16);
    // The payload is the token alone, 4 bytes. The checksum covers everything
    // but the magic word.
    unsigned short payloadSize = 4;
    unsigned short checksum =
        (unsigned short)(command + payloadSize + tokenLow + tokenHigh);

    send_word(CMD_MAGIC_NUMBER);
    send_word(command);
    send_word(payloadSize);
    send_word(tokenLow);
    send_word(tokenHigh);
    send_word(checksum);

    for (long wait = CMD_TIMEOUT; wait > 0; wait--) {
      // The token alone is not an answer: the device writes the token and the
      // new seed as two stores, and the token sent is the seed read. Waiting
      // for the seed to move on makes the pair a commit of both.
      if ((*answerToken == token) && (*answerSeed != token)) {
        return 0;
      }
    }
  }
  return -1;
}

int sidecart_restart_device(void) {
  return sidecart_send_command(SIDECART_CMD_RESTART);
}
