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

int debug=0;

int dump_bytes(char *msg,unsigned char *bytes,int length)
{
  if (!debug) return 0;
  
  int i,j;
  fprintf(stderr,"%s:\n",msg);
  for(i=0;i<length;i+=16) {
    int show=0;
    for(j=0;j<16;j++)
      if ((i+j<length)&&(bytes[i+j])) { show=1; break; }
    if (!show) continue;

    
    fprintf(stderr,"%04X: ",i);
    for(j=0;j<16;j++)
      if (i+j<length) fprintf(stderr," %02X",bytes[i+j]);
      else fprintf(stderr,"   ");
    fprintf(stderr,"  ");
    for(j=0;j<16;j++)
      if (i+j<length) {
	if ((bytes[i+j]>=' ')&&(bytes[i+j]<0x7f))
	  fprintf(stderr,"%c",bytes[i+j]);
	else
	  fprintf(stderr,".");
      }
    fprintf(stderr,"\n");
  }
  return 0;
}

int write_radio(int fd,unsigned char *bytes,int count)
{
  int written=write(fd,bytes,count);
  if (written!=count) fprintf(stderr,"WARNING: Short write.\n");
  dump_bytes("Wrote to radio",bytes,written);
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
  while(get_radio_reply(fd,buffer,8192,0)>0) continue;
  return 0;
}

int radio_in_at_command_mode(int fd)
{
  char buffer[8192];
  
  // Erase the partially typed command and be ready to type a new one
  write_radio(fd,(unsigned char *)"\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\r",17);
  get_radio_reply(fd,buffer,8192,1);

  // Send AT and expect OK
  sleep(1);
  clear_waiting_bytes(fd);
  write_radio(fd,(unsigned char *)"AT\r",3);
  int bytes=get_radio_reply(fd,buffer,8192,1);
  if (strstr(buffer,"OK")) {
    if (!silent_mode) printf("Got OK reply to AT, so assuming that we are in command mode.\n");
    return 1;
  } else {
    if (!silent_mode) {
      printf("We don't seem to be in AT command mode.\n");
      debug++;
      dump_bytes("This is what I saw echoed back",
		 (unsigned char *)buffer,bytes);
      debug--;
    }
    return 0;
  }
}

int switch_to_online_mode(int fd)
{
  if (!silent_mode) fprintf(stderr,"Switching to online mode.\n");
  
  if (onlinemode) return 0;
  if (bootloadermode) {
    fprintf(stderr,"I can't reliably switch to online mode from the bootloader.\n");
    exit(-1);
  }
  
  write(fd,"\r\nATO\r\n",7);
  sleep(1);
  clear_waiting_bytes(fd);

  onlinemode=1;
  atmode=0;
  bootloadermode=0;
  
  return 0;
}

int switch_to_at_mode(int fd)
{
  if (!silent_mode) fprintf(stderr,"Attempting to switch to AT command mode.\n");

  
  char buffer[8192];

  sleep(2);
  write_radio(fd,(unsigned char *)"+++",3);
  get_radio_reply(fd,buffer,8192,2);
  if (!strstr(buffer,"OK")) {
    // Try a second time with +++
    sleep(2);
    write_radio(fd,(unsigned char *)"+++",3);
    get_radio_reply(fd,buffer,8192,2);
  }
  if (strstr(buffer,"OK")) {
    if (radio_in_at_command_mode(fd)) {
      if (!silent_mode) fprintf(stderr,"Yes, we are in command mode.\n");
      atmode=1;
      onlinemode=0;
      return 0;
    } else {
      fprintf(stderr,"Okay, that's weird, I got some characters echoed, but we don't seem to be in command mode.\n");
    }    
  }

  return -1;
}

int switch_to_bootloader(int fd)
{
  if (!silent_mode) fprintf(stderr,"Attempting to switch to bootloader mode.\n");

  try_bang_B(fd);
  
  if (bootloadermode) return 0;

  if (!atmode) {
    if (switch_to_at_mode(fd)) {
      fprintf(stderr,"Failed to switch radio to AT mode in preparation for switching to bootloader mode.\n");
      return -1;
    }
  }
  
  // try AT&UPDATE or ATS1=115\rAT&W\rATZ if the modem isn't already on 115200bps
  if (!silent_mode) printf("Switching to boot loader...\n");
  clear_waiting_bytes(fd);
  char *cmd="AT&UPDATE\r\n";
  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  char buffer[8192];
  int reply_bytes;
  reply_bytes=get_radio_reply(fd,buffer,8192,1);
  if (reply_bytes>strlen(cmd)) {
    fprintf(stderr,"Saw extraneous bytes after sending AT&UPDATE... Probably not a good sign.\n");
  }

  if (!strcmp(cmd,buffer)) {
    if (!silent_mode) fprintf(stderr,"Looks like we have switched to the bootloader from AT command mode.\n");
    setup_serial_port(fd,115200);

    atmode=0;
    bootloadermode=1;
    onlinemode=0;
  
    return 0;
  } else {
    fprintf(stderr,"Saw '%s' instead of '%s' when trying to switch to bootloader... Probably not a good sign.\n",buffer,cmd);

    // ... but it might have worked, anyway.
    
    // Start at 115200
    setup_serial_port(fd,115200);
    clear_waiting_bytes(fd);
    
    // Clear any pending bootloader command
    unsigned char cmd[260]; bzero(&cmd[0],260);
    write_radio(fd,(unsigned char *)cmd,260);
    
    write_radio(fd,(unsigned char *)" \" ",3);
    reply_bytes=get_radio_reply(fd,buffer,8192,1);
    if ((reply_bytes==4)&&(buffer[2]==INSYNC)&&(buffer[3]==OK)) {
      // Got a valid bootloader string.
      detectedspeed=115200;
      bootloadermode=1;
      atmode=0;
      onlinemode=0;
      if (!silent_mode) fprintf(stderr,"Radio is already in boot loader @ 115200\n");
      return 0;
    }    
    
    return -1;
  }
  
}

int try_bang_B(int fd)
{
    // Start at 115200
  int old_speed=last_baud;
    setup_serial_port(fd,115200);
    clear_waiting_bytes(fd);
    
    // Clear any pending bootloader command
    unsigned char cmd[260]; bzero(&cmd[0],260);
    write_radio(fd,(unsigned char *)cmd,260);

    // Then switch to 230400 and send !Cup!B
    setup_serial_port(fd,230400);
    write_radio(fd,(unsigned char *)"ATO\r\n",5);
    clear_waiting_bytes(fd);
    // No idea why we have to type this command sequence slowly.
    // Perhaps !C can take more than one character read time to execute?
    {
      char *s="!Cup!B";
      for(;*s;s++) {
	write(fd,s,1);
	usleep(50000);
      }
    }
    clear_waiting_bytes(fd);

    // Switch to bootloader speed
    setup_serial_port(fd,115200);
    
    // Clear any pending bootloader command
    write_radio(fd,(unsigned char *)cmd,260);
    
    clear_waiting_bytes(fd);

    // Try to get OK & INSYNC
    write_radio(fd,(unsigned char *)" \" ",3);
    char buffer[8192];
    int reply_bytes=get_radio_reply(fd,buffer,8192,1);
    if ((reply_bytes==4)&&(buffer[2]==INSYNC)&&(buffer[3]==OK)) {
      // Got a valid bootloader string.
      detectedspeed=115200;
      bootloadermode=1;
      atmode=0;
      onlinemode=0;
      if (!silent_mode) fprintf(stderr,"Radio is in boot loader @ 115200 (via !B)\n");
      return 0;
    }

    setup_serial_port(fd,old_speed);
    return 1;
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
  setup_serial_port(fd,115200);
  clear_waiting_bytes(fd);

  // Clear any pending bootloader command
  unsigned char cmd[260]; bzero(&cmd[0],260);
  write_radio(fd,(unsigned char *)cmd,260);
  
  write_radio(fd,(unsigned char *)" \" ",3);
  reply_bytes=get_radio_reply(fd,buffer,8192,1);
  if (reply_bytes>0) dump_bytes("Bootloader sync probe result",
				(unsigned char *)buffer,reply_bytes);
  else fprintf(stderr,"No response to bootloader sync probe.\n");
  if ((reply_bytes==4)&&(buffer[2]==INSYNC)&&(buffer[3]==OK)) {
    // Got a valid bootloader string.
    detectedspeed=115200;
    bootloadermode=1;
    atmode=0;
    onlinemode=0;
    if (!silent_mode) fprintf(stderr,"Radio is already in boot loader @ 115200\n");
    return 0;
  }
  if ((reply_bytes==3)&&(!strcmp(buffer," \" "))) {
    // Got our characters echoed out to us, so assume that we are in command mode
    // at this speed.
    if (!silent_mode)
      fprintf(stderr,"I suspect that we are in command mode. Verifying ...\n");

    if (radio_in_at_command_mode(fd)) {
      if (!silent_mode) fprintf(stderr,"Yes, we are in command mode at 115200bps.\n");
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
    if (!silent_mode) fprintf(stderr,"Yes, we are in command mode at 115200bps.\n");
    detectedspeed=115200;
    bootloadermode=0;
    atmode=1;
    onlinemode=0;
    return 0;
  }

  // We now know that we are not at 115,200bps, so try other likely speeds.
  // For these other speeds, we need only check for on-line and AT mode.
  int speed;
  int speeds[]={230400,57600,-1};
  for(speed=0;speeds[speed]>0;speed++) {    
    setup_serial_port(fd,speeds[speed]);

    // Check if we are in the bootloader already
    
    
    if (radio_in_at_command_mode(fd)) {
      if (!silent_mode) fprintf(stderr,"Yes, we are in command mode at %dbps.\n",speeds[speed]);
      detectedspeed=speeds[speed];
      bootloadermode=0;
      atmode=1;
      onlinemode=0;
      return 0;
    }

    if (!switch_to_at_mode(fd)) {
      if (!silent_mode) fprintf(stderr,"Yes, we are in command mode at %dbps.\n",speeds[speed]);
      detectedspeed=speeds[speed];
      bootloadermode=0;
      atmode=1;
      onlinemode=0;
      return 0;
    }

    // We have seen a bug where the radio is in AT command mode, but is unable
    // to writing anything out from there, except echo commands back.
    // It does, however, respond to AT&UPDATE in that mode, so we can issue
    // AT&UPDATE, and then check if we are in the bootloader.
    if (!atmode) {
      atmode=1;
      if (switch_to_bootloader(fd)) {
	atmode=0;
      } else {
	// success
	return 0;
      }
    }
    
  }

  return -1;
}

int change_radio_to(int fd,int speed)
{
  char reply[8192+1];
  int r=0;

  if (speed==first_speed) return 0;
  
  if (!silent_mode) printf("Changing modem to %dbps (original speed was %d)\n",
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
  
  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  sleep(1);
  r=read(fd,reply,8192); reply[8192]=0; if (r>0&&r<8192) reply[r]=0;
  if (!silent_mode) printf("%s reply is '%s'\n",cmd,reply);

  cmd="AT&W\r\n";
  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  sleep(1);
  r=read(fd,reply,8192); reply[8192]=0; if (r>0&&r<8192) reply[r]=0;
  if (!silent_mode) printf("%s reply is '%s'\n",cmd,reply);

  cmd="ATZ\r\n";	  
  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  sleep(3);  // Allow time for the radio to restart
  r=read(fd,reply,8192); reply[8192]=0;
  if (r>0&&r<8192) reply[r]=0;
  if (!silent_mode) printf("ATZ reply is '%s'\n",reply);

  // Go back to looking for modem at 115200
  if (!silent_mode) printf("Changed modem to %dbps (original speed was %d)\n",
			   speed,first_speed);

  return 0;
}
