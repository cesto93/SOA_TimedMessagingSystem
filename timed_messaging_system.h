#ifndef TIMED_MESSAGING_SYSTEM_H
#define TIMED_MESSAGING_SYSTEM_H

#include <linux/ioctl.h>

extern int max_msg_size;
extern int max_storage_size;

#define MODNAME "CHAR DEV"
#define DEVICE_NAME "timed-msg-system" 	/* Device file name in /dev/ - not mandatory  */

#define SET_SEND_TIMEOUT 0
#define SET_RECV_TIMEOUT 1
#define REVOKE_DELAYED_MESSAGES 2

#define SET_SEND_TIMEOUT_NR(MAJOR_NUM) _IOR(MAJOR_NUM, SET_SEND_TIMEOUT, int)
#define SET_RECV_TIMEOUT_NR(MAJOR_NUM) _IOR(MAJOR_NUM, SET_RECV_TIMEOUT, int)
#define REVOKE_DELAYED_MESSAGES_NR(MAJOR_NUM) _IO(MAJOR_NUM, REVOKE_DELAYED_MESSAGES)

#define MINORS 8

#endif
