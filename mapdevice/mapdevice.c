//
// mapdevice.c
// a maxmsp and puredata external encapsulating the functionality of a
// libmapper "device", allowing name and metadata to be set
// http://www.libmapper.org
// Joseph Malloch, IDMIL 2013
//
// This software was written in the Input Devices and Music Interaction
// Laboratory at McGill University in Montreal, and is copyright those
// found in the AUTHORS file.  It is licensed under the GNU Lesser Public
// General License version 2.1 or later.  Please see COPYING for details.
//

/* TODO:
 *  DONE: mapin object
 *  handle other signal props
 *      add most of them afterwards
 *  handle device launched after signals
 *  handle signal removal
 *  handle device removal
 *  handle device relaunch
 *  test multiple copies of same signal
 *      mapin
 *      mapout
 *  attach device to top patcher
 *  test launching conflicting devices
 *  test lanching conflicting signal props
 */

// *********************************************************
// -(Includes)----------------------------------------------

#include "ext.h"            // standard Max include, always required
#include "ext_obex.h"       // required for new style Max object
#include "jpatcher_api.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <lo/lo.h>
#ifndef WIN32
  #include <arpa/inet.h>
#endif

#include <unistd.h>

#define INTERVAL 1
#define MAX_LIST 256

// *********************************************************
// -(object struct)-----------------------------------------
typedef struct _mapdevice
{
    t_object            ob;
    void                *outlet;
    t_hashtab           *ht;
    void                *clock;
    char                *name;
    mapper_admin        admin;
    mapper_device       device;
    mapper_timetag_t    timetag;
    int                 updated;
    int                 ready;
    t_atom              buffer[MAX_LIST];
} t_mapdevice;

typedef struct _mapin_ptrs
{
    t_mapdevice *home;
    int         num_objs;
    t_object    **objs;
} t_mapin_ptrs;

// *********************************************************
// -(function prototypes)-----------------------------------
static void *mapdevice_new(t_symbol *s, int argc, t_atom *argv);
static void mapdevice_free(t_mapdevice *x);

static void mapdevice_notify(t_mapdevice *x, t_symbol *s, t_symbol *msg,
                             void *sender, void *data);

static void mapdevice_detach_obj(t_hashtab_entry *e, void *arg);
static void mapdevice_detach(t_mapdevice *x);
static void mapdevice_attach_obj(t_hashtab_entry *e, void *arg);
static void mapdevice_attach(t_mapdevice *x);

static void mapdevice_poll(t_mapdevice *x);

static void mapdevice_float_handler(mapper_signal sig, mapper_db_signal props,
                                    int instance_id, void *value, int count,
                                    mapper_timetag_t *tt);
static void mapdevice_int_handler(mapper_signal sig, mapper_db_signal props,
                                  int instance_id, void *value, int count,
                                  mapper_timetag_t *tt);
static void mapdevice_instance_event_handler(mapper_signal sig,
                                             mapper_db_signal props,
                                             int instance_id,
                                             msig_instance_event_t event,
                                             mapper_timetag_t *tt);

static void mapdevice_print_properties(t_mapdevice *x);

//static void maybe_start_queue(t_mapdevice *x);
static int atom_strcmp(t_atom *a, const char *string);
static const char *atom_get_string(t_atom *a);
static void atom_set_string(t_atom *a, const char *string);

// *********************************************************
// -(global class pointer variable)-------------------------
static void *mapdevice_class;

// *********************************************************
// -(main)--------------------------------------------------
int main(void)
{
    t_class *c;
    c = class_new("mapdevice", (method)mapdevice_new, (method)mapdevice_free,
                  (long)sizeof(t_mapdevice), 0L, A_GIMME, 0);
    class_addmethod(c, (method)mapdevice_notify, "notify", A_CANT, 0);
    class_register(CLASS_BOX, c); /* CLASS_NOBOX */
    mapdevice_class = c;
    return 0;
}

// *********************************************************
// -(new)---------------------------------------------------
static void *mapdevice_new(t_symbol *s, int argc, t_atom *argv)
{
    t_mapdevice *x = NULL;
    long i;
    const char *alias = NULL;
    const char *iface = NULL;

    if ((x = object_alloc(mapdevice_class))) {
        x->outlet = listout((t_object *)x);
        x->name = strdup("maxmsp");

        for (i = 0; i < argc; i++) {
            if ((argv+i)->a_type == A_SYM) {
                if (atom_strcmp(argv+i, "@alias") == 0) {
                    if ((argv+i+1)->a_type == A_SYM) {
                        alias = atom_get_string(argv+i+1);
                        i++;
                    }
                }
                else if (atom_strcmp(argv+i, "@interface") == 0) {
                    if ((argv+i+1)->a_type == A_SYM) {
                        iface = atom_get_string(argv+i+1);
                        i++;
                    }
                }
            }
        }
        if (alias) {
            free(x->name);
            x->name = *alias == '/' ? strdup(alias+1) : strdup(alias);
        }
        post("mapdevice: using name %s", x->name);

        if (iface)
            post("mapdevice: trying network interface %s", iface);
        else
            post("mapdevice: using default network interface.");

        x->admin = mapper_admin_new(iface, 0, 0);
        if (!x->admin) {
            post("mapdevice: error initializing libmapper admin.");
            return 0;
        }
        x->device = mdev_new(x->name, 0, x->admin);
        if (!x->device) {
            post("mapdevice: error initializing libmapper device.");
            return 0;
        }

        // add other declared properties
        for (i = 0; i < argc; i++) {
            if (i > argc - 2) // need 2 arguments for key and value
                break;
            if ((atom_strcmp(argv+i, "@alias") == 0) ||
                (atom_strcmp(argv+i, "@interface") == 0)){
                i++;
                continue;
            }
            else if (atom_get_string(argv+i)[0] == '@') {
                switch ((argv+i+1)->a_type) {
                    case A_SYM: {
                        const char *value = atom_get_string(argv+i+1);
                        mdev_set_property(x->device, atom_get_string(argv+i)+1,
                                          's', (lo_arg *)value);
                        i++;
                        break;
                    }
                    case A_FLOAT:
                    {
                        float value = atom_getfloat(argv+i+1);
                        mdev_set_property(x->device, atom_get_string(argv+i)+1,
                                          'f', (lo_arg *)&value);
                        i++;
                        break;
                    }
                    case A_LONG:
                    {
                        int value = atom_getlong(argv+i+1);
                        mdev_set_property(x->device, atom_get_string(argv+i)+1,
                                          'i', (lo_arg *)&value);
                        i++;
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        mapdevice_print_properties(x);
        x->ready = 0;
        x->updated = 0;

        // Create the timing clock
        x->clock = clock_new(x, (method)mapdevice_poll);
        clock_delay(x->clock, INTERVAL);  // Set clock to go off after delay

        mapdevice_attach(x);
    }
    return (x);
}

// *********************************************************
// -(free)--------------------------------------------------
static void mapdevice_free(t_mapdevice *x)
{
   // mapdevice_detach(x);

    clock_unset(x->clock);      // Remove clock routine from the scheduler
    clock_free(x->clock);       // Frees memeory used by clock

    if (x->device) {
        mdev_free(x->device);
    }
    if (x->admin) {
        mapper_admin_free(x->admin);
    }
    if (x->name) {
        free(x->name);
    }
}

void mapdevice_notify(t_mapdevice *x, t_symbol *s, t_symbol *msg, void *sender, void *data)
{
    if (msg == gensym("update")) {
        // update appropriate signal
    }
	else if (msg == gensym("hashtab_entry_new")) { // something arrived in the hashtab
		t_symbol *key = (t_symbol *)data;
		t_object *obj = NULL;
        mapper_signal sig = NULL;
		hashtab_lookup(sender, key, &obj);
		if (obj) {
			object_attach_byptr(x, obj); // attach to object
            t_symbol *temp = object_attr_getsym(obj, gensym("sig_name"));
            const char *name = temp->s_name;
            char type = object_attr_getchar(obj, gensym("sig_type"));
            long length = object_attr_getlong(obj, gensym("sig_length"));
            
            if (object_classname(obj) == gensym("mapout")) {
                sig = mdev_get_output_by_name(x->device, name, 0);
                if (!sig)
                    sig = mdev_add_output(x->device, name, length, type, 0, 0, 0);
                atom_setobj(x->buffer, (void *)sig);
                object_attr_setvalueof(obj, gensym("sig_ptr"), 1, x->buffer);

                //output numOutputs
                atom_setlong(x->buffer, mdev_num_outputs(x->device));
                outlet_anything(x->outlet, gensym("numOutputs"), 1, x->buffer);
            }
            else if (object_classname(obj) == gensym("mapin")) {
                sig = mdev_get_input_by_name(x->device, name, 0);
                if (sig) {
                    // get user_data
                    mapper_db_signal props = msig_properties(sig);
                    t_mapin_ptrs *ptrs = (t_mapin_ptrs *)props->user_data;
                    ptrs->objs = realloc(ptrs->objs, (ptrs->num_objs+1) * sizeof(t_object *));
                    ptrs->objs[ptrs->num_objs] = obj;
                    ptrs->num_objs++;
                }
                else {
                    t_mapin_ptrs *ptrs = (t_mapin_ptrs *)malloc(sizeof(struct _mapin_ptrs));
                    ptrs->home = x;
                    ptrs->objs = (t_object **)malloc(sizeof(t_object *));
                    ptrs->num_objs = 1;
                    ptrs->objs[0] = obj;
                    sig = mdev_add_input(x->device, name, length, type, 0, 0, 0, type == 'i' ?
                                         mapdevice_int_handler : mapdevice_float_handler, ptrs);
                }
                atom_setobj(x->buffer, (void *)sig);
                object_attr_setvalueof(obj, gensym("sig_ptr"), 1, x->buffer);

                //output numInputs
                atom_setlong(x->buffer, mdev_num_inputs(x->device));
                outlet_anything(x->outlet, gensym("numInputs"), 1, x->buffer);
            }
        }
		object_post((t_object *)x, "Attached to %ld signals.", hashtab_getsize(sender));
	}
    else if (msg == gensym("hashtab_entry_free")) { // something left the hashtab
		t_symbol *key = (t_symbol *)data;
		t_object *obj = NULL;
		
		hashtab_lookup(sender, key, &obj);
		if (obj)
			object_detach_byptr(x, obj); // detach from it
		// we receive the notification before the entry is removed from the hashtable
        object_post((t_object *)x, "Attached to %ld signals.", hashtab_getsize(sender) - 1);
	}
}

void mapdevice_detach_obj(t_hashtab_entry *e, void *arg)
{
	t_mapdevice *x = (t_mapdevice *)arg;
	if (x) {
		// detach from the object, it's going away...
		object_detach_byptr(x, e->value);
	}
}

void mapdevice_detach(t_mapdevice *x)
{
	if (x->ht) {
		hashtab_funall(x->ht, (method)mapdevice_detach_obj, x);
		object_detach_byptr(x, x->ht); // detach from the hashtable
	}
}

void mapdevice_attach_obj(t_hashtab_entry *e, void *arg)
{
	t_mapdevice *x = (t_mapdevice *)arg;
	if (x) {
		// attach to the object to receive its notifications
		object_attach_byptr(x, e->value);
	}
}

void mapdevice_attach(t_mapdevice *x)
{
	t_object *jp;

	object_obex_lookup(x, gensym("#P"), &jp); // get the object's patcher
	if (jp) {
		t_hashtab *ht;

		// look in the jpatcher's obex for an object called "mapperhash"
		object_obex_lookup(jp, gensym("mapperhash"), (t_object **)&ht);
		if (!ht) {
			// it's not there? create it.
			ht = hashtab_new(0);
			// objects stored in the obex will be freed when the obex's owner is freed
			// in this case, when the patcher object is freed. so we don't need to
			// manage the memory associated with the "mapperhash".
			object_obex_store(jp, gensym("mapperhash"), (t_object *)ht);
		}
		x->ht = ht;
		// attach to the hashtab, registering it if necessary
		// this way, we can receive notifications from the hashtab as things are added and removed
		object_attach_byptr_register(x, x->ht, CLASS_NOBOX);
		// call a method on every object in the hash table
		hashtab_funall(x->ht, (method)mapdevice_attach_obj, x);

        object_post((t_object *)x, "Attached to %ld signals.", hashtab_getsize(x->ht));
	}
}

// *********************************************************
// -(print properties)--------------------------------------
static void mapdevice_print_properties(t_mapdevice *x)
{
    if (x->ready) {
        //output name
        atom_set_string(x->buffer, mdev_name(x->device));
        outlet_anything(x->outlet, gensym("name"), 1, x->buffer);

        //output interface
        atom_set_string(x->buffer, mdev_interface(x->device));
        outlet_anything(x->outlet, gensym("interface"), 1, x->buffer);

        //output IP
        const struct in_addr *ip = mdev_ip4(x->device);
        atom_set_string(x->buffer, inet_ntoa(*ip));
        outlet_anything(x->outlet, gensym("IP"), 1, x->buffer);

        //output port
        atom_setlong(x->buffer, mdev_port(x->device));
        outlet_anything(x->outlet, gensym("port"), 1, x->buffer);

        //output ordinal
        atom_setlong(x->buffer, mdev_ordinal(x->device));
        outlet_anything(x->outlet, gensym("ordinal"), 1, x->buffer);

        //output numInputs
        atom_setlong(x->buffer, mdev_num_inputs(x->device));
        outlet_anything(x->outlet, gensym("numInputs"), 1, x->buffer);

        //output numOutputs
        atom_setlong(x->buffer, mdev_num_outputs(x->device));
        outlet_anything(x->outlet, gensym("numOutputs"), 1, x->buffer);
    }
}

// *********************************************************
// -(int handler)-------------------------------------------
static void mapdevice_int_handler(mapper_signal msig, mapper_db_signal props,
                                  int instance_id, void *value, int count,
                                  mapper_timetag_t *tt)
{
    t_mapin_ptrs *ptrs = props->user_data;
    t_mapdevice *x = ptrs->home;

    int i, poly = 0;
    if (props->num_instances > 1) {
        atom_setlong(x->buffer, instance_id);
        poly = 1;
    }
    if (value) {
        int length = props->length;
        int *v = value;

        if (length > (MAX_LIST-1)) {
            post("Maximum list length is %i!", MAX_LIST-1);
            length = MAX_LIST-1;
        }

        for (i = 0; i < length; i++)
            atom_setlong(x->buffer + i + poly, v[i]);
        for (i=0; i<ptrs->num_objs; i++)
            outlet_list(ptrs->objs[i]->o_outlet, NULL, length + poly, x->buffer);
    }
    else if (poly) {
        atom_set_string(x->buffer+1, "release");
        atom_set_string(x->buffer+2, "local");
        for (i=0; i<ptrs->num_objs; i++)
            outlet_list(ptrs->objs[i]->o_outlet, NULL, 3, x->buffer);
    }
}

// *********************************************************
// -(float handler)-----------------------------------------
static void mapdevice_float_handler(mapper_signal msig, mapper_db_signal props,
                                 int instance_id, void *value, int count,
                                 mapper_timetag_t *time)
{
    t_mapin_ptrs *ptrs = props->user_data;
    t_mapdevice *x = ptrs->home;

    int i, poly = 0;
    if (props->num_instances > 1) {
        atom_setlong(x->buffer, instance_id);
        poly = 1;
    }
    if (value) {
        int length = props->length;
        float *v = value;

        if (length > (MAX_LIST-1)) {
            post("Maximum list length is %i!", MAX_LIST-1);
            length = MAX_LIST-1;
        }

        for (i = 0; i < length; i++)
            atom_setfloat(x->buffer + i + poly, v[i]);
        for (i=0; i<ptrs->num_objs; i++)
            outlet_list(ptrs->objs[i]->o_outlet, NULL, length + poly, x->buffer);
    }
    else if (poly) {
        atom_set_string(x->buffer+1, "release");
        atom_set_string(x->buffer+2, "local");
        for (i=0; i<ptrs->num_objs; i++)
            outlet_list(ptrs->objs[i]->o_outlet, NULL, 3, x->buffer);
    }
}

// *********************************************************
// -(instance management handler)----------------------
static void mapdevice_instance_event_handler(mapper_signal sig,
                                             mapper_db_signal props,
                                             int instance_id,
                                             msig_instance_event_t event,
                                             mapper_timetag_t *tt)
{
    t_mapin_ptrs *ptrs = props->user_data;
    t_mapdevice *x = ptrs->home;

    int i, id, mode;
    atom_setlong(x->buffer, instance_id);
    switch (event) {
        case IN_UPSTREAM_RELEASE:
            atom_set_string(x->buffer+1, "release");
            atom_set_string(x->buffer+2, "upstream");
            for (i=0; i<ptrs->num_objs; i++)
                outlet_list(ptrs->objs[i]->o_outlet, NULL, 3, x->buffer);
            break;
        case IN_DOWNSTREAM_RELEASE:
            atom_set_string(x->buffer+1, "release");
            atom_set_string(x->buffer+2, "downstream");
            for (i=0; i<ptrs->num_objs; i++)
                outlet_list(ptrs->objs[i]->o_outlet, NULL, 3, x->buffer);
            break;
        case IN_OVERFLOW:
            mode = msig_get_instance_allocation_mode(sig);
            switch (mode) {
                case IN_STEAL_OLDEST:
                    if (msig_get_oldest_active_instance(sig, &id))
                        return;
                    msig_release_instance(sig, id, *tt);
                    break;
                case IN_STEAL_NEWEST:
                    if (msig_get_newest_active_instance(sig, &id))
                        return;
                    msig_release_instance(sig, id, *tt);
                    break;
                case 0:
                    atom_set_string(x->buffer+1, "overflow");
                    for (i=0; i<ptrs->num_objs; i++)
                        outlet_list(ptrs->objs[i]->o_outlet, NULL, 2, x->buffer);
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

// *********************************************************
// -(poll libmapper)----------------------------------------
static void mapdevice_poll(t_mapdevice *x)
{
    int count = 10;
    while(count-- && mdev_poll(x->device, 0)) {};
    if (!x->ready) {
        if (mdev_ready(x->device)) {
            //mapdevice_db_dump(db);
            x->ready = 1;
            mapdevice_print_properties(x);
        }
    }
    else if (x->updated) {
        mdev_send_queue(x->device, x->timetag);
        x->updated = 0;
    }
    clock_delay(x->clock, INTERVAL);  // Set clock to go off after delay
}


// *********************************************************
// some helper functions

static int atom_strcmp(t_atom *a, const char *string)
{
    if (a->a_type != A_SYM || !string)
        return 1;
    return strcmp(atom_getsym(a)->s_name, string);
}

static const char *atom_get_string(t_atom *a)
{
    return atom_getsym(a)->s_name;
}

static void atom_set_string(t_atom *a, const char *string)
{
    atom_setsym(a, gensym((char *)string));
}

