#include <stdbool.h>

#include <dbus/dbus.h>

// default message delay in milliseconds
#define EXPIRE_DEFAULT  5000

// multiply requested delay by this
// (slow it down since only one line)
#define EXPIRE_MULT     2

// default command
#define DEFAULT_CMD     "xsetroot -name '[summary] ~ [body]'"

// config dir
#define CFG_DIR         "cow-notify"

// config file
#define CFG_FIL         "config"

typedef struct _notification {
   dbus_uint32_t nid;
   time_t started_at;
   time_t expires_after;
   char closed;

   char appname[20];
   char summary[64];
   char body[256];
   struct _notification *next;
} notification;

// initialize notifications
DBusConnection* notify_init(bool const debug_enabled);

// returns the first current notification into status (number of total messages in n)
notification* notify_get_message(DBusConnection *dbus, int *n);

// calls the respective notify handler based on the message received
DBusHandlerResult notify_handle(DBusConnection *dbus, DBusMessage *msg, void *user_data);

// check the dbus for notifications (1=something happened, 0=nothing)
bool notify_check(DBusConnection *dbus);
