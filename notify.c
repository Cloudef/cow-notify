#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <pwd.h>
#include <limits.h>

#include "notify.h"

#define DEBUG(...) if( DEBUGGING == true) fprintf(stderr, __VA_ARGS__)
bool DEBUGGING=0;

notification *messages=NULL;

dbus_uint32_t curNid = 1;
bool notify_Notify(DBusConnection *dbus, DBusMessage *msg);
bool notify_GetCapabilities(DBusConnection *dbus, DBusMessage *msg);
bool notify_GetServerInformation(DBusConnection *dbus, DBusMessage *msg);
bool notify_CloseNotification(DBusConnection *dbus, DBusMessage *msg);
bool notify_NotificationClosed(DBusConnection *dbus, unsigned int nid, unsigned int reason);

// Path to file
static char path[PATH_MAX];

static void get_config_path(void)
{
   const char* config = getenv("XDG_CONFIG_HOME");
   if(config != NULL)
      snprintf( path, PATH_MAX, "%s/%s/%s", config, CFG_DIR, CFG_FIL );
   else
   {
      config = getenv("HOME");
      if (config == NULL)
      {
         struct passwd *pw = getpwuid(getuid());
         config = pw->pw_dir;
      }
      snprintf( path, PATH_MAX, "%s/.config/%s/%s", config, CFG_DIR, CFG_FIL );
   }
}

DBusConnection* notify_init(bool const debug_enabled) {
   DBusError dbus_err;
   int ret;

   dbus_error_init(&dbus_err);
   DBusConnection *dbus = NULL;

   dbus = dbus_bus_get(DBUS_BUS_SESSION, &dbus_err);
   if (NULL == dbus)
      return 0;

   ret = dbus_bus_request_name(dbus, "org.freedesktop.Notifications", DBUS_NAME_FLAG_REPLACE_EXISTING , &dbus_err);
   if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret)
      return NULL;

   DEBUGGING=debug_enabled;

   get_config_path();

   dbus_error_free(&dbus_err);
   return dbus;
}

// returns the first current notification or NULL ( and if n is supplied, number of total messages)
notification *notify_get_message(DBusConnection *dbus, int *n) {
   notification *ptr;
   int temp=0;

   if( n==NULL ) n=&temp;

   *n=0;
   if( messages != NULL ) {
      // check/remove expired messages
      while( messages!=NULL && messages->expires_after!=0 &&
            (messages->started_at + messages->expires_after) < time(NULL) )
      {
         notification *t = messages->next;
         notify_NotificationClosed(dbus, messages->nid, 1 + messages->closed*2);
         free(messages);
         messages = t;
      }

      ptr=messages;
      while( ptr!=NULL ) { ptr=ptr->next; (*n)++; }
   }

   return messages;
}

DBusHandlerResult notify_handle(DBusConnection *dbus, DBusMessage *msg, void *user_data) {
   (void)(user_data);
   if (dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "Notify"))
      notify_Notify(dbus, msg);
   else if (dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "GetCapabilities"))
      notify_GetCapabilities(dbus, msg);
   else if (dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "GetServerInformation"))
      notify_GetServerInformation(dbus, msg);
   else if (dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "CloseNotification"))
      notify_CloseNotification(dbus, msg);
   else
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
   return  DBUS_HANDLER_RESULT_HANDLED;
}


// check the dbus for notifications (1=something happened, 0=nothing)
bool notify_check(DBusConnection *dbus) {
   DBusMessage* msg;

   // non blocking read of the next available message
   dbus_connection_read_write(dbus, 0);
   msg = dbus_connection_pop_message(dbus);

   if (msg != NULL) {
      notify_handle(dbus, msg, NULL);
      dbus_message_unref(msg);
      dbus_connection_flush(dbus);
      return true;
   }
   return false;
}

// to support libnotify events, we must implement:
//
// Methods:
//   org.freedesktop.Notifications.Notify (STRING app_name, UINT32 replaces_id, app_icon (ignored), STRING summary, STRING body, actions (ignored), hints (ignored), INT32 expire_timeout)
//     replaces_id = previous notification to replace
//     expire_timeout==0 for no expiration, -1 for default expiration
//     returns notification id (replaces_id if given)
//
//   org.freedesktop.Notifications.GetCapabilities
//     returns caps[1] = "body" (doesnt support any fancy features)
//
//   org.freedesktop.Notifications.GetServerInformation
//     returns "dwmstatus", "suckless", "0.1"
//
//   org.freedesktop.Notifications.CloseNotification (nid)
//     forcefully hide and remove notification
//     emits NotificationClosed signal when done
//
// Signal:
//   org.freedesktop.Notifications.NotificationClosed -> (nid, reason )
//     whenever notification is closed(reason=3) or expires(reason=1)

bool notify_NotificationClosed(DBusConnection *dbus, unsigned int nid, unsigned int reason)
{
   DBusMessageIter args;
   DBusMessage* notify_close_msg;

   DEBUG("NotificationClosed(%d, %d)\n", nid, reason);

   notify_close_msg = dbus_message_new_signal("/org/freedesktop/Notifications", "org.freedesktop.Notifications", "NotificationClosed");
   if( notify_close_msg == NULL )
      return false;

   dbus_message_iter_init_append(notify_close_msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &nid) ||
         !dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &reason) ||
         !dbus_connection_send(dbus, notify_close_msg, NULL))
   {
      dbus_message_unref(notify_close_msg);
      return false;
   }
   dbus_message_unref(notify_close_msg);

   DEBUG("   Signal emitted\n");
   return true;
}

static void run_file(notification* note)
{
    char* sh = NULL;
    char* summary;
    char* body;
    char expireTime[LINE_MAX];
    char* args[6];

    if(fork() != 0)
        return;

    setsid();
    summary = strdup(note->summary);
    body = strdup(note->body);
    snprintf(expireTime, LINE_MAX, "%d", (int)note->expires_after ? (int)note->expires_after : (int)EXPIRE_DEFAULT);

    if(!(sh = getenv("SHELL"))) sh = "/bin/sh";
    args[0] = sh;
    args[1] = strdup(path);
    args[2] = expireTime;
    args[3] = summary;
    args[4] = body;
    args[5] = NULL;
    execv(sh, args);
    DEBUG("%s \"%s\" \"%s\" \"%s\" \"%s\"\n", sh, args[0], args[1], args[2], args[3]);

    free(args[1]);
    free(summary);
    free(body);
    /* Exit fork */
    _exit(0);
}

// Notify
bool notify_Notify(DBusConnection *dbus, DBusMessage *msg) {
   DBusMessage* reply;
   DBusMessageIter args;
   const char *appname;
   const char *summary;
   const char *body;
   dbus_uint32_t nid=0;
   dbus_int32_t expires=-1;
   notification *ptr = messages;
   notification *note = NULL;

   dbus_message_iter_init(msg, &args);
   dbus_message_iter_get_basic(&args, &appname);
   dbus_message_iter_next( &args );
   dbus_message_iter_get_basic(&args, &nid);
   dbus_message_iter_next( &args );
   dbus_message_iter_next( &args );  // skip icon
   dbus_message_iter_get_basic(&args, &summary);
   dbus_message_iter_next( &args );
   dbus_message_iter_get_basic(&args, &body);
   dbus_message_iter_next( &args );
   dbus_message_iter_next( &args );  // skip actions
   dbus_message_iter_next( &args );  // skip hints
   dbus_message_iter_get_basic(&args, &expires);

   DEBUG("Notify('%s', %u, -, '%s', '%s', -, -, %d)\n",appname, nid, summary, body, expires);

   if( nid!=0 ) { // update existing message
      note = messages;
      if( note!=NULL )
         while( note->nid != nid && note->next!=NULL ) note=note->next;

      if( note==NULL || note->nid!=nid ) { // not found, re-create
         note = calloc(sizeof(notification), 1);
         note->nid=nid;
         nid=0;
      }
   } else {
      note = calloc(sizeof(notification), 1);
      note->nid=curNid++;
      note->started_at = time(NULL);
   }

   note->expires_after = (time_t)(expires<0?EXPIRE_DEFAULT:expires*EXPIRE_MULT);
   note->closed=0;
   strncpy( note->appname, appname, APP_LEN);
   strncpy( note->summary, summary, SUMMARY_LEN);
   strncpy( note->body,    body,    BODY_LEN);

   if( nid==0 ) {
      if( ptr==NULL ) messages=note;
      else {
         while( ptr->next != NULL ) ptr=ptr->next;
         ptr->next=note;
      }
   }

   run_file( note );
   reply = dbus_message_new_method_return(msg);

   dbus_message_iter_init_append(reply, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &(note->nid)) ||
         !dbus_connection_send(dbus, reply, NULL))
   {
      return false;
   }
   dbus_message_unref(reply);

   DEBUG("   Notification %d created.\n", note->nid);
   return true;
}

// CloseNotification
bool notify_CloseNotification(DBusConnection *dbus, DBusMessage *msg) {
   DBusMessage* reply;
   DBusMessageIter args;
   dbus_uint32_t nid=0;
   notification *ptr = messages;

   dbus_message_iter_init(msg, &args);
   dbus_message_iter_get_basic(&args, &nid);

   DEBUG("CloseNotification(%d)\n", nid);

   if( ptr!=NULL && ptr->nid==nid ) {
      ptr->expires_after=(time(NULL) - ptr->started_at)*EXPIRE_MULT;
      ptr->closed=1;
   } else if( ptr!=NULL ) {
      while( ptr->next != NULL && ptr->next->nid != nid ) {
         ptr=ptr->next;
      }

      if( ptr->next != NULL && ptr->next->nid==nid ) {
         ptr = ptr->next;
         ptr->expires_after=(time(NULL) - ptr->started_at)*EXPIRE_MULT;
         ptr->closed=1;
      }
   }

   reply = dbus_message_new_method_return(msg);
   if( !dbus_connection_send(dbus, reply, NULL)) return false;
   dbus_message_unref(reply);

   DEBUG("   Close Notification Queued.\n");
   return true;
}

// GetCapabilites
bool notify_GetCapabilities(DBusConnection *dbus, DBusMessage *msg) {
   DBusMessage* reply;
   DBusMessageIter args;
   DBusMessageIter subargs;

   char *caps[] = {"body"};

   DEBUG("GetCapabilities called!\n");

   reply = dbus_message_new_method_return(msg);
   if(!reply)
   {
      return false;
   }

   dbus_message_iter_init_append(reply, &args);
   if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &subargs ))
      return 1;

   for (int i = 0; i < sizeof(caps)/sizeof(caps[0]); ++i)
      if (!dbus_message_iter_append_basic(&subargs, DBUS_TYPE_STRING, caps + i))
         return 1;

   if (!dbus_message_iter_close_container(&args, &subargs) ||
         !dbus_connection_send(dbus, reply, NULL))
   {
      return false;
   }

   dbus_message_unref(reply);

   return true;
}

// GetServerInformation
bool notify_GetServerInformation(DBusConnection *dbus, DBusMessage *msg) {
   DBusMessage* reply;
   DBusMessageIter args;

   char* info[4] = {"cow-notify", "cow-notify", "0.1", "1.0"};

   DEBUG("GetServerInfo called!\n");

   reply = dbus_message_new_method_return(msg);

   dbus_message_iter_init_append(reply, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info[0]) ||
         !dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info[1]) ||
         !dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info[2]) ||
         !dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info[3]) ||
         !dbus_connection_send(dbus, reply, NULL))
   {
      return false;
   }

   dbus_message_unref(reply);
   return true;
}
