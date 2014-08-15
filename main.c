/*
  Stand-alone program to flash RFD900 radios. Intended for use on Mesh Extenders
  and other embedded hosts.

  (C) Serval Project Inc. 2014.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#include <stdio.h>
#include "cintelhex.h"

int main(int argc,char **argv)
{
  if (argc!=3) {
    fprintf(stderr,"usage: flash900 <firmware> <serial port>\n");
    exit(-1);
  }
  
  ihex_recordset_t *ihex=ihex_rs_from_file(argv[1]);
  
  return 0;
}
