#ifndef SIDECART_H
#define SIDECART_H

// Commands sent straight to the device, without going through GEMDOS.
//
// The command code is an app in the high byte and a command in the low byte,
// the same numbering the firmware uses (rp/src/include/gemdrive.h).
#define SIDECART_APP_GEMDRIVE 0x04
#define SIDECART_CMD_RESTART ((SIDECART_APP_GEMDRIVE << 8) | 0x8C)

// Send a command that carries no arguments and wait for the device to answer.
// Returns 0 when it answered, -1 on timeout.
int sidecart_send_command(unsigned short command);

// Ask the device to restart. It comes back in the setup menu, where the card
// shows up over USB, so a test run that nobody is watching ends with its log
// where a computer can read it.
int sidecart_restart_device(void);

#endif  // SIDECART_H
