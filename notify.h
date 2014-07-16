#include <stdbool.h>
#include <dbus/dbus.h>

#ifdef __linux__
   #include <limits.h>
#endif

/* default message delay in milliseconds */
#define EXPIRE_DEFAULT  5000

/* multiply requested delay by this
 * (slow it down since only one line)
 */
#define EXPIRE_MULT     2

/* default command */
#define DEFAULT_CMD     "xsetroot -name '[summary] ~ [body]'"

/* config dir */
#define CFG_DIR         "cow-notify"

/* config file */
#define CFG_FIL         "config"

/* notification maximum text lengths
 * might want to increase these, if causes problems.
 *
 * thought won't have problems with LINE_MAX
 */
#ifdef __linux__
   #define APP_LEN         256
   #define SUMMARY_LEN     LINE_MAX
   #define BODY_LEN        LINE_MAX
#else
   #define APP_LEN         256
   #define SUMMARY_LEN     256
   #define BODY_LEN        256
#endif

typedef struct _notification {
   dbus_uint32_t nid;
   time_t started_at;
   time_t expires_after;
   char closed;

   char appname[APP_LEN];
   char summary[SUMMARY_LEN];
   char body[BODY_LEN];
   struct _notification *next;
} notification;

/* initialize notifications */
DBusConnection* notify_init(bool const debug_enabled);

/* returns the first current notification into status (number of total messages in n) */
notification* notify_get_message(DBusConnection *dbus, int *n);

/* calls the respective notify handler based on the message received */
DBusHandlerResult notify_handle(DBusConnection *dbus, DBusMessage *msg, void *user_data);

/* check the dbus for notifications (1=something happened, 0=nothing) */
bool notify_check(DBusConnection *dbus);
