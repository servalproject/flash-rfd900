

extern int atmode;
extern int detectedspeed;
extern int bootloadermode;
extern int onlinemode;

extern int first_speed;

extern int last_baud;

extern int debug;

int setup_serial_port(int fd, int baud);
int change_radio_to(int fd,int speed);
int detect_speed(int fd);
int switch_to_bootloader(int fd);
int clear_waiting_bytes(int fd);
int switch_to_online_mode(int fd);
int try_bang_B(int fd);
int eeprom_program(int argc,char **argv);
int set_nonblock(int fd);
int write_radio(int fd,unsigned char *bytes,int count);
int get_radio_reply(int fd,char *buffer,int buffer_size,int delay_in_seconds);
int dump_bytes(char *m, unsigned char *b,int count);

// RFD900 boot-loader commands
#define NOP		0x00
#define OK		0x10
#define FAILED		0x11
#define INSYNC		0x12
#define EOC		0x20
#define GET_SYNC	0x21
#define GET_DEVICE	0x22
#define CHIP_ERASE	0x23
#define LOAD_ADDRESS	0x24
#define PROG_FLASH	0x25
#define READ_FLASH	0x26
#define PROG_MULTI	0x27
#define READ_MULTI	0x28
#define PARAM_ERASE	0x29
#define REBOOT		0x30

