/*
 * Linux HA management library
 *
 * Author: Huang Zhen <zhenhltc@cn.ibm.com>
 * Copyright (c) 2005 International Business Machines
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#if HAVE_SECURITY_PAM_APPL_H
#  include <security/pam_appl.h>
#else
#  if HAVE_PAM_PAM_APPL_H
#    include <pam/pam_appl.h>
#  endif
#endif
#include <glib.h>

#if HAVE_HB_CONFIG_H
#include <hb_config.h>
#endif

#if HAVE_GLUE_CONFIG_H
#include <glue_config.h>
#endif

#include <clplumbing/GSource.h>
#include <clplumbing/cl_log.h>
#include <clplumbing/cl_syslog.h>
#include <clplumbing/cl_signal.h>
#include <clplumbing/lsb_exitcodes.h>
#include <clplumbing/coredumps.h>
#include <clplumbing/cl_pidfile.h>

#include <crm/crm.h>

#ifdef SUPPORT_AIS
#undef SUPPORT_AIS
#endif

#ifdef SUPPORT_HEARTBEAT
#undef SUPPORT_HEARTBEAT
#endif

#include <pygui_internal.h>

#if HAVE_PACEMAKER_CRM_COMMON_CLUSTER_H
#  include <crm/common/cluster.h>
#endif
#if HAVE_PACEMAKER_CRM_CLUSTER_H
#  include <crm/cluster.h>
#endif

#include <mgmt/mgmt.h>
#include "mgmt_internal.h"


/* common daemon and debug functions */

/* the initial func for modules */
extern int init_general(void);
extern void final_general(void);
#if SUPPORT_HEARTBEAT
extern int init_heartbeat(void);
#endif
extern void final_heartbeat(void);
extern int init_lrm(void);
extern void final_lrm(void);

static GHashTable* msg_map = NULL;		
static GHashTable* event_map = NULL;		
const char* client_name = NULL;
static int components = 0;		
int
init_mgmt_lib(const char* client, int enable_components)
{
	/* create the internal data structures */
	msg_map = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);
	event_map = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);
	client_name = client?client:"unknown";
	components = enable_components;
	mgmt_set_mem_funcs(malloc, realloc, free);
	
	/* init modules */
#if SUPPORT_HEARTBEAT
	if(is_heartbeat_cluster()) {
		if (components & ENABLE_HB) {
			if (init_heartbeat() != 0) {
				return -1;
			}
		}
	}
#endif
	if (components & ENABLE_LRM) {
		if (init_lrm() != 0) {
			return -1;
		}
	}
	return 0;
}

int
final_mgmt_lib()
{
	if (components & ENABLE_LRM) {
		final_lrm();
	}
#if SUPPORT_HEARTBEAT
	if(is_heartbeat_cluster()) {
		if (components & ENABLE_HB) {
			final_heartbeat();
		}
	}
#endif
	g_hash_table_destroy(msg_map);
	g_hash_table_destroy(event_map);
	return 0;
}

int
reg_msg(const char* type, msg_handler fun)
{
	if (g_hash_table_lookup(msg_map, type) != NULL) {
		return -1;
	}
	g_hash_table_insert(msg_map, strdup(type),(gpointer)fun);
	return 0;
}

int
fire_event(const char* event)
{
	event_handler func = NULL;
	
	char** args = mgmt_msg_args(event, NULL);
	if (args == NULL) {
		return -1;
	}
	
	func = (event_handler)g_hash_table_lookup(event_map, args[0]);
	if (func != NULL) {
		func(event);
	}
	mgmt_del_args(args);
	return 0;
}

char*
process_msg(const char* msg)
{
	msg_handler handler;
	char* ret;
	int num;
	char** args = mgmt_msg_args(msg, &num);
	if (args == NULL) {
		return NULL;
	}
	handler = (msg_handler)g_hash_table_lookup(msg_map, args[0]);
	if ( handler == NULL) {
		mgmt_del_args(args);
		return NULL;
	}
	ret = (*handler)(args, num);
	mgmt_del_args(args);
	return ret;
}
int
reg_event(const char* type, event_handler func)
{
	g_hash_table_replace(event_map, strdup(type), (gpointer)func);
	return 0;
}
