#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "flash900.h"

int atmode=0;
int detectedspeed=0;
int bootloadermode=0;
int onlinemode=0;

int first_speed=-1;

int dump_bytes(char *msg,unsigned char *bytes,int length)
{
  fprintf(stderr,"%s:\n",msg);
  for(int i=0;i<length;i+=16) {
    fprintf(stderr,"%04X: ",i);
    for(int j=0;j<16;j++) if (i+j<length) fprintf(stderr," %02X",bytes[i+j]);
    fprintf(stderr,"\n");
  }
  return 0;
}

int get_radio_reply(int fd,char *buffer,int buffer_size,int delay_in_seconds)
{
  sleep(delay_in_seconds);  
  int r=read(fd,buffer,buffer_size); buffer[buffer_size-1]=0;
  if (r>0) dump_bytes("Bytes from radio",(unsigned char *)buffer,r);
  return r;
}

int clear_waiting_bytes(int fd)
{
  char buffer[8192];
  get_radio_reply(fd,buffer,8192,0);
  return 0;
}

int radio_in_at_command_mode(int fd)
{
  int reply_bytes;
  char buffer[8192];
  
  // Erase the partially typed command and be ready to type a new one
  write(fd,"\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\r",17);
  reply_bytes=get_radio_reply(fd,buffer,8192,1);

  // Send AT and expect OK
  sleep(1);
  clear_waiting_bytes(fd);
  write(fd,"AT\r",3);
  reply_bytes=get_radio_reply(fd,buffer,8192,1);
  if (!strstr(buffer,"OK")) {
    printf("Got OK reply to AT, so assuming that we are in command mode.\n");
    return 1;
  } else {
    printf("We don't seem to be in AT command mode.\n");
    return 0;
  }
}

int switch_to_at_mode(int fd)
{
  int reply_bytes;
  char buffer[8192];

  sleep(2);
  write(fd,"+++",3);
  reply_bytes=get_radio_reply(fd,buffer,8192,2);
  if (strstr(buffer,"OK")) {
    if (radio_in_at_command_mode(fd)) {
      fprintf(stderr,"Yes, we are in command mode at 115200bps.\n");
      return 0;
    } else {
      fprintf(stderr,"Okay, that's weird, I got some characters echoed, but we don't seem to be in command mode.\n");
    }    
  }

  return -1;
}

int switch_to_bootloader(int fd)
{
  if (bootloadermode) return 0;

  if (!atmode) {
    if (switch_to_at_mode(fd)) {
      fprintf(stderr,"Failed to switch radio to AT mode in preparation for switching to bootloader mode.\n");
      return -1;
    }
  }
  
  // try AT&UPDATE or ATS1=115\rAT&W\rATZ if the modem isn't already on 115200bps
  printf("Switching to boot loader...\n");
  clear_waiting_bytes(fd);
  char *cmd="AT&UPDATE\r\n";
  write(fd,cmd,strlen(cmd));
  char buffer[8192];
  int reply_bytes;
  reply_bytes=get_radio_reply(fd,buffer,8192,1);
  if (reply_bytes>strlen(cmd)) {
    fprintf(stderr,"Saw extraneous bytes after sending AT&UPDATE... Probably not a good sign.\n");
  }
  setup_serial_port(fd,115200);
  return 0;
  
}

int detect_speed(int fd)
{
  /* Try to work out the current speed of the radio.
     The radio can be in one of three states:

     1. On-line mode
     2. Command mode
     3. Bootloader

     The bootloader is always at 115200, and will respond with the board ID if you 
     send ($22, $20), and will respond with something like $43 $91 $12 $10, where the
     first two bytes are the board ID.

     If we are already in the bootloader, then all is fine -- we are in the right mode,
     and can return the speed we detected the bootloader at.

     If we are in command mode, the modem will echo characters back at us, so if we
     have sent $22 $20, we will see that come back.

     Failing that, if we are in online mode, we will see nothing, OR we will see the
     periodic GPIO report packets unannounced.  The latter tells us that we are at
     the correct speed.

     If we don't see any GPIO report packets, but see other characters, when we are
     probably in online mode, but not at the correct speed.

     If we are in online mode, we can try sending +++ and see if we get an OK.

     If all the above fails, then we try other speeds.

  */

  int reply_bytes=0;
  char buffer[8192];
  
  // Start at 115200
  setup_serial_port(fd,B115200);
  clear_waiting_bytes(fd);

  // Clear any pending bootloader command
  unsigned char cmd[260]; bzero(&cmd[0],260);
  write(fd,cmd,260);
  
  write(fd," \" ",3);
  reply_bytes=get_radio_reply(fd,buffer,8192,1);
  if ((reply_bytes==4)&&(buffer[2]==INSYNC)&&(buffer[3]==OK)) {
    // Got a valid bootloader string.
    detectedspeed=115200;
    bootloadermode=1;
    atmode=0;
    onlinemode=0;
    fprintf(stderr,"Radio is already in boot loader @ 115200\n");
    return 0;
  }
  if ((reply_bytes==3)&&(!strcmp(buffer," \" "))) {
    // Got our characters echoed out to us, so assume that we are in command mode
    // at this speed.
    fprintf(stderr,"I suspect that we are in command mode. Verifying ...\n");

    if (radio_in_at_command_mode(fd)) {
      fprintf(stderr,"Yes, we are in command mode at 115200bps.\n");
      detectedspeed=115200;
      bootloadermode=0;
      atmode=1;
      onlinemode=0;
      return 0;
    } else {
      fprintf(stderr,"Okay, that's weird, I got some characters echoed, but we don't seem to be in command mode.\n");
    }
    
  }

  // At this point, we are either in on-line mode, or talking to the modem at the wrong
  // speed.
  if (!switch_to_at_mode(fd)) {
    fprintf(stderr,"Yes, we are in command mode at 115200bps.\n");
    detectedspeed=115200;
    bootloadermode=0;
    atmode=1;
    onlinemode=0;
    return 0;
  }

  // We now know that we are not at 115,200bps, so try other likely speeds.
  // For these other speeds, we need only check for on-line and AT mode.
  int speed;
  int speeds[]={230400,57600,38400,19200,9600,2400,1200,-1};
  for(speed=0;speeds[speed]>0;speed++) {    
    setup_serial_port(fd,speeds[speed]);

    if (radio_in_at_command_mode(fd)) {
      fprintf(stderr,"Yes, we are in command mode at %dbps.\n",speeds[speed]);
      detectedspeed=speeds[speed];
      bootloadermode=0;
      atmode=1;
      onlinemode=0;
      return 0;
    }

    if (!switch_to_at_mode(fd)) {
      fprintf(stderr,"Yes, we are in command mode at %dbps.\n",speeds[speed]);
      detectedspeed=speeds[speed];
      bootloadermode=0;
      atmode=1;
      onlinemode=0;
      return 0;
    }    
  }

  return -1;
}

int change_radio_to(int fd,int speed)
{
  char reply[8192+1];
  int r=0;

  printf("Changing modem to %dbps (original speed was %d)\n",
	 speed,first_speed);
  
  if (!atmode) {
    if (switch_to_at_mode(fd)) {
      fprintf(stderr,"Failed to switch radio to AT mode in preparation for setting radio speed.\n");
      return -1;
    }
  }
  
  char *cmd=NULL;
  switch (speed) {
  case 57600: cmd="ATS1=57\r\n"; break;
  case 115200: cmd="ATS1=115\r\n"; break;
  case 230400: cmd="ATS1=230\r\n"; break;
  default:
    printf("Illegal speed: %dpbs (bust be 57600,115200 or 230400)\n",speed);
  }
  
  write(fd,cmd,strlen(cmd));
  sleep(1);
  r=read(fd,reply,8192); reply[8192]=0; if (r>0&&r<8192) reply[r]=0;
  printf("%s reply is '%s'\n",cmd,reply);

  cmd="AT&W\r\n";
  write(fd,cmd,strlen(cmd));
  sleep(1);
  r=read(fd,reply,8192); reply[8192]=0; if (r>0&&r<8192) reply[r]=0;
  printf("%s reply is '%s'\n",cmd,reply);

  cmd="ATZ\r\n";	  
  write(fd,cmd,strlen(cmd));
  sleep(3);  // Allow time for the radio to restart
  r=read(fd,reply,8192); reply[8192]=0;
  if (r>0&&r<8192) reply[r]=0;
  printf("ATZ reply is '%s'\n",reply);

  // Go back to looking for modem at 115200
  printf("Changed modem to %dbps (original speed was %d)\n",
	 speed,first_speed);

  return 0;
}
