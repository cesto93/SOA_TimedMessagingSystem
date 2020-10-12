#ifndef TIMED_MESSAGING_SYSTEM_H
#define TIMED_MESSAGING_SYSTEM_H

extern int max_msg_size;
extern int max_storage_size;

#define MODNAME "CHAR DEV"
#define DEVICE_NAME "timed-msg-system" 	/* Device file name in /dev/ - not mandatory  */

#define SET_SEND_TIMEOUT 0
#define SET_RECV_TIMEOUT 1
#define REVOKE_DELAYED_MESSAGES 2

#define MINORS 8

#endif
