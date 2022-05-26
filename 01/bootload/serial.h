#ifndef _SERIAL_H_INCLUDE_
#define _SERIAL_H_INCLUDE_

int serial_init(int index);
int serial_is_send_enable(int index);
int serial_send_byte(int index, unsigned char b);

#endif
