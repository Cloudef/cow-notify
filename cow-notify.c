/*
 * cow-notify - 0.1 ~ fork from dwmstatus
 * Executes command on notification.
 * Edit file XDG_CONFIG_HOME/cow-notify/setting
*/

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

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

   DBusConnection *dbus = NULL;
   while ((dbus = notify_init(DEBUGGING)) == NULL) {
      fprintf(stderr, "cannot bind notifications\n");
      sleep(1);
   }

   dbus_connection_add_filter(dbus, notify_handle, NULL, NULL);
   while(dbus_connection_read_write_dispatch(dbus, -1));

   return 0;
}
