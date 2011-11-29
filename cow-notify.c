/*
 * cow-notify - 0.1 ~ fork from dwmstatus
 * Executes command on notification.
 * Edit file XDG_CONFIG_HOME/cow-notify/setting
*/

#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>

#include "notify.h"

static bool DEBUGGING = false;

int main(int argc, char **argv)
{
   /* do not wait for forks */
   signal(SIGCHLD, SIG_IGN);

   if( argc==2 && argv[1][0]=='-' && argv[1][1]=='v' ) {
      DEBUGGING = true;
      fprintf(stderr, "debugging enabled.\n");
   }

   while ( !notify_init(DEBUGGING) ) {
      fprintf(stderr, "cannot bind notifications\n");
      sleep(1);
   }

   while ( true )
   {
      if( !notify_check() )
      {
         sleep(1);
      }
   }

   return 0;
}
