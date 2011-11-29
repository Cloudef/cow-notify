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
dbus_uint32_t serial = 0xDEADBEEF;
DBusConnection* dbus_conn;

bool notify_Notify(DBusMessage *msg);
bool notify_GetCapabilities(DBusMessage *msg);
bool notify_GetServerInformation(DBusMessage *msg);
bool notify_CloseNotification(DBusMessage *msg);
bool notify_NotificationClosed(unsigned int nid, unsigned int reason);

// Command from file
static char command[LINE_MAX];

static void read_command(void)
{
   const char* config = getenv("XDG_CONFIG_HOME");
   if(!config)
   {
      struct passwd *pw = getpwuid(getuid());
      config = pw->pw_dir;
   }

   char filename[PATH_MAX];
   snprintf( filename, PATH_MAX, "%s/%s/%s", config, CFG_DIR, CFG_FIL );
   sprintf( command, DEFAULT_CMD );

   FILE* fp;
   fp = fopen(filename, "r");
   if(fp)
   {
      fgets( command, LINE_MAX, fp );
      fclose(fp);
   }
}

bool notify_init(bool const debug_enabled) {
   DBusError dbus_err;
   int ret;

   dbus_error_init(&dbus_err);
   dbus_conn=NULL;

   dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &dbus_err);
   if (NULL == dbus_conn)
      return 0;

   ret = dbus_bus_request_name(dbus_conn, "org.freedesktop.Notifications", DBUS_NAME_FLAG_REPLACE_EXISTING , &dbus_err);
   if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret)
      return 0;

   DEBUGGING=debug_enabled;

   read_command();

   dbus_error_free(&dbus_err);
   return 1;
}

// returns the first current notification or NULL ( and if n is supplied, number of total messages)
notification *notify_get_message(int *n) {
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
         notify_NotificationClosed(messages->nid, 1 + messages->closed*2);
         free(messages);
         messages = t;
      }

      ptr=messages;
      while( ptr!=NULL ) { ptr=ptr->next; (*n)++; }
   }

   return messages;
}

// check the dbus for notifications (1=something happened, 0=nothing)
bool notify_check() {
   DBusMessage* msg;

   // non blocking read of the next available message
   dbus_connection_read_write(dbus_conn, 0);
   msg = dbus_connection_pop_message(dbus_conn);

   if (msg != NULL) {
      if (dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "Notify"))
         notify_Notify(msg);
      if (dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "GetCapabilities"))
         notify_GetCapabilities(msg);
      if (dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "GetServerInformation"))
         notify_GetServerInformation(msg);
      if (dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "CloseNotification"))
         notify_CloseNotification(msg);

      dbus_message_unref(msg);
      dbus_connection_flush(dbus_conn);
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

bool notify_NotificationClosed(unsigned int nid, unsigned int reason)
{
   DBusMessageIter args;
   DBusMessage* notify_close_msg;
   serial++;

   DEBUG("NotificationClosed(%d, %d)\n", nid, reason);

   notify_close_msg = dbus_message_new_signal("/org/freedesktop/Notifications", "org.freedesktop.Notifications", "NotificationClosed");
   if( notify_close_msg == NULL )
      return false;

   dbus_message_iter_init_append(notify_close_msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &nid) ||
         !dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &reason) ||
         !dbus_connection_send(dbus_conn, notify_close_msg, &serial))
   {
      dbus_message_unref(notify_close_msg);
      return false;
   }
   dbus_message_unref(notify_close_msg);

   DEBUG("   Signal emitted\n");
   return true;
}

// since most libnotify clients dont respect my capabilities, this
// simple helper will strip html tags and endlines from notifications
// modifies string in place
// strips &amp; - stuff instead of replacing with proper chars
void _strip_body(char *text) {
   int i=0, j=0;
   bool in_tag = false, in_amp = false;

   while( text[j] ) {
      if( text[j]=='\n' ) text[i++]=' ';
      else if( text[j]=='\t' ) text[i++]=' ';
      else if( text[j]=='<' ) in_tag = true;
      else if( text[j]=='>' && in_tag == true) in_tag = false;
      else if( text[j]=='&' ) in_amp = true;
      else if( text[j]==';' && in_amp == true) { in_amp = false; text[i++]=' '; }
      else if( in_tag == false && in_amp == false ) text[i++]=text[j];
      j++;
   }
   text[i]=0;
}

/* precondition: s!=0, old!=0, new!=0 */
static char *str_replace(const char *s, const char *old, const char *new)
{
  size_t slen = strlen(s)+1;
  char *cout=0, *p=0, *tmp=NULL; cout=malloc(slen); p=cout;
  if( !p )
    return 0;
  while( *s )
    if( !strncmp(s, old, strlen(old)) )
    {
      p  -= (intptr_t)cout;
      cout= realloc(cout, slen += strlen(new)-strlen(old) );
      tmp = strcpy(p=cout+(intptr_t)p, new);
      p  += strlen(tmp);
      s  += strlen(old);
    }
    else
     *p++=*s++;

  *p=0;
  return cout;
}

static char* escape_single_quote( char *string )
{
   char *tmp;
   if(!string)
      return string;

   tmp = str_replace( string, "'", "'\\''" );
   if(tmp)
   {
      free(string);
      string = tmp;
   }

   return string;
}

static void run_command( notification *note )
{
   char *sh = NULL;
   char *summary = escape_single_quote( strdup(note->summary) );
   char *body    = escape_single_quote( strdup(note->body) );

   char *tmp = NULL, *parsed = NULL;
   tmp = str_replace( command, "[summary]", summary );
   if(!tmp)
      parsed = str_replace( command, "[body]", body );
   else
   {
      parsed = str_replace( tmp, "[body]", body );
      if(parsed)
         free(tmp);
      else
         parsed = tmp;
   }
   free(summary);
   free(body);

   if(parsed)
   {
      char expireTime[LINE_MAX];
      sprintf(expireTime, "%d", (int)note->expires_after ? (int)note->expires_after : (int)EXPIRE_DEFAULT);
      tmp = str_replace( parsed, "[expire]", expireTime );
      if(tmp)
      {
         free(parsed);
         parsed = tmp;
      }
   }

   if(!parsed)
      parsed = strdup(command);

   if(!(sh = getenv("SHELL"))) sh = "/bin/sh";

   DEBUG(parsed);
   if(fork() == 0)
   {
      setsid();
      execlp(sh, sh, "-c", parsed, (char*)NULL);
      free(parsed);
      _exit(0);
   }
}

// Notify
bool notify_Notify(DBusMessage *msg) {
   DBusMessage* reply;
   DBusMessageIter args;
   const char *appname;
   const char *summary;
   const char *body;
   dbus_uint32_t nid=0;
   dbus_int32_t expires=-1;
   notification *ptr = messages;
   notification *note = NULL;

   serial++;

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
   strncpy( note->appname, appname, 20);
   strncpy( note->summary, summary, 64);
   strncpy( note->body,    body, 256);
   //_strip_body(note->body);
   DEBUG("   body stripped to: '%s'\n", note->body);

   if( nid==0 ) {
      if( ptr==NULL ) messages=note;
      else {
         while( ptr->next != NULL ) ptr=ptr->next;
         ptr->next=note;
      }
   }

   run_command( note );
   reply = dbus_message_new_method_return(msg);

   dbus_message_iter_init_append(reply, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &(note->nid)) ||
         !dbus_connection_send(dbus_conn, reply, &serial))
   {
      return false;
   }
   dbus_message_unref(reply);

   DEBUG("   Notification %d created.\n", note->nid);
   return true;
}

// CloseNotification
bool notify_CloseNotification(DBusMessage *msg) {
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
   if( !dbus_connection_send(dbus_conn, reply, &serial)) return false;
   dbus_message_unref(reply);

   DEBUG("   Close Notification Queued.\n");
   return true;
}

// GetCapabilites
bool notify_GetCapabilities(DBusMessage *msg) {
   DBusMessage* reply;
   DBusMessageIter args;
   DBusMessageIter subargs;
   int ncaps = 1;

   char *caps[1] = {"body"}, **ptr = caps;  // workaround (see specs)
   serial++;

   printf("GetCapabilities called!\n");

   reply = dbus_message_new_method_return(msg);
   if(!reply)
   {
      return false;
   }

   dbus_message_iter_init_append(reply, &args);
   if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, NULL, &subargs ) ||
         !dbus_message_iter_append_fixed_array(&subargs, DBUS_TYPE_STRING, &ptr, ncaps) ||
         !dbus_message_iter_close_container(&args, &subargs) ||
         !dbus_connection_send(dbus_conn, reply, &serial))
   {
      return false;
   }

   dbus_message_unref(reply);

   return true;
}

// GetServerInformation
bool notify_GetServerInformation(DBusMessage *msg) {
   DBusMessage* reply;
   DBusMessageIter args;

   char* info[4] = {"cow-notify", "cow-notify", "0.1", "1.0"};
   serial++;

   printf("GetServerInfo called!\n");

   reply = dbus_message_new_method_return(msg);

   dbus_message_iter_init_append(reply, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info[0]) ||
         !dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info[1]) ||
         !dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info[2]) ||
         !dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &info[3]) ||
         !dbus_connection_send(dbus_conn, reply, &serial))
   {
      return false;
   }

   dbus_message_unref(reply);
   return true;
}
