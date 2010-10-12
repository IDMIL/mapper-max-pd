/**
	@file
	maxadmin - max object interface with libmapper
    http://www.idmil.org/software/mappingtools
	joseph malloch
*/

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object
#include "ext_dictionary.h"
#include "jpatcher_api.h"
#include "src/mapper_internal.h"
#include "include/mapper/mapper.h"
#include <stdio.h>
#include <math.h>
#include "lo/lo.h"

#include <unistd.h>
#include <arpa/inet.h>

#define INTERVAL 1
#define MAX_PATH_CHARS 2048
#define MAX_FILENAME_CHARS 512

////////////////////////// object struct
typedef struct _maxadmin 
{
	t_object ob;			// the object itself (must be first)
    void *m_outlet;
    void *m_outlet2;
    void *m_outlet3;
    void *m_clock;          // pointer to clock object
	char *basename;
    char *definition;
    mapper_device device;
    mapper_signal sendsig;
    mapper_signal recvsig;
    int ready;
} t_maxadmin;

t_symbol *ps_list;

int port = 9000;

///////////////////////// function prototypes
void *maxadmin_new(t_symbol *s, long argc, t_atom *argv);
void maxadmin_free(t_maxadmin *x);
void maxadmin_assist(t_maxadmin *x, void *b, long m, long a, char *s);
void maxadmin_anything(t_maxadmin *x, t_symbol *s, long argc, t_atom *argv);
void maxadmin_add_signal(t_maxadmin *x, t_symbol *msg, long argc, t_atom *argv);
void maxadmin_remove_signal(t_maxadmin *x, t_symbol *msg, long argc, t_atom *argv);
void poll(t_maxadmin *x);
void float_handler(mapper_signal msig, mapper_signal_value_t *v);
void int_handler(mapper_signal msig, mapper_signal_value_t *v);
//void list_handler(mapper_signal msig, mapper_signal_value_t *v);
void maxadmin_print_properties(t_maxadmin *x);
void maxadmin_read_definition(t_maxadmin *x);

//////////////////////// global class pointer variable
void *maxadmin_class;


int main(void)
{	
	t_class *c;
	
	c = class_new("maxadmin", (method)maxadmin_new, (method)maxadmin_free, (long)sizeof(t_maxadmin), 
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
    class_addmethod(c, (method)maxadmin_assist,			"assist",   A_CANT,     0);
    class_addmethod(c, (method)maxadmin_add_signal,     "add",      A_GIMME,    0);
    class_addmethod(c, (method)maxadmin_remove_signal,  "remove",   A_GIMME,    0);
    class_addmethod(c, (method)maxadmin_anything,       "anything", A_GIMME,    0);
	
	class_register(CLASS_BOX, c); /* CLASS_NOBOX */
	maxadmin_class = c;
	
	ps_list = gensym("list");
	
	return 0;
}

void maxadmin_print_properties(t_maxadmin *x)
{
	if (x->device) {
		//get device properties
		//output them
		//outlet_list(x->m_outlet3, ps_list, 2, myList);
	}
}

void maxadmin_assist(t_maxadmin *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "OSC input");
	} 
	else {	// outlet
        if (a == 0) {
            sprintf(s, "Mapped OSC data");
        }
        else if (a == 1) {
            sprintf(s, "State queries");
        }
        else {
            sprintf(s, "Device information");
        }
	}
}

void maxadmin_free(t_maxadmin *x)
{
    clock_unset(x->m_clock);	// Remove clock routine from the scheduler
	clock_free(x->m_clock);		// Frees memeory used by clock
    
    if (x->device) {
        if (x->device->routers) {
            post("Removing router.. ");
            //fflush(stdout);
            mdev_remove_router(x->device, x->device->routers);
            post("ok\n");
        }
        post("Freeing device.. ");
        //fflush(stdout);
        mdev_free(x->device);
        post("ok\n");
    }
}

void maxadmin_add_signal(t_maxadmin *x, t_symbol *s, long argc, t_atom *argv)
{
	t_atom myList[2];
    //need to read attribs: type, units, min/minimum, max/maximum
    //char *type;
    char *units = 0;
    float minimum = 0;
    float maximum = 1;
    long i;
    
    if (argc < 4)
		return;
    
    for (i = 0; i < argc; i++) {
        if ((argv + i)->a_type == A_SYM) {
			if(strcmp(atom_getsym(argv+i)->s_name, "@units") == 0) {
				if ((argv + i + 1)->a_type == A_SYM) {
					units = strdup(atom_getsym(argv+i+1)->s_name);
					i++;
				}
			}
			else if((strcmp(atom_getsym(argv+i)->s_name, "@min") == 0) || (strcmp(atom_getsym(argv+i)->s_name, "@minimum") == 0)) {
				if ((argv + i + 1)->a_type == A_FLOAT) {
					minimum = atom_getfloat(argv + i + 1);
					i++;
				}
                else if ((argv + i + 1)->a_type == A_LONG) {
					minimum = (float)atom_getlong(argv + i + 1);
					i++;
				}
			}
            else if((strcmp(atom_getsym(argv+i)->s_name, "@max") == 0) || (strcmp(atom_getsym(argv+i)->s_name, "@maximum") == 0)) {
				if ((argv + i + 1)->a_type == A_FLOAT) {
					maximum = atom_getfloat(argv + i + 1);
					i++;
				}
                else if ((argv + i + 1)->a_type == A_LONG) {
					maximum = (float)atom_getlong(argv + i + 1);
					i++;
				}
			}
        }
    }
	
	if (argv->a_type == A_SYM) {
        if ((argv + 1)->a_type != A_SYM) {
            return;
        }
		if (strcmp(atom_getsym(argv)->s_name, "input") == 0) {
			
			//register all signals as floats for now
			x->recvsig = msig_float(1, atom_getsym(argv + 1)->s_name, units, minimum, maximum, 0, float_handler, x);
			mdev_register_input(x->device, x->recvsig);
			
			//output numInputs
			atom_setsym(myList, gensym("numInputs"));
			atom_setlong(myList + 1, mdev_num_inputs(x->device));
			outlet_list(x->m_outlet3, ps_list, 2, myList);
		} 
		else if (strcmp(atom_getsym(argv)->s_name, "output") == 0) {
			//register all signals as floats for now
			x->sendsig = msig_float(1, atom_getsym(argv + 1)->s_name, units, minimum, maximum, 0, 0, 0);
			mdev_register_output(x->device, x->sendsig);
			
			//output numOutputs
			atom_setsym(myList, gensym("numOutputs"));
			atom_setlong(myList + 1, mdev_num_outputs(x->device));
			outlet_list(x->m_outlet3, ps_list, 2, myList);
		}
	}
}

void maxadmin_remove_signal(t_maxadmin *x, t_symbol *s, long argc, t_atom *argv)
{
	;
}

void maxadmin_anything(t_maxadmin *x, t_symbol *s, long argc, t_atom *argv)
{
	if (argc) {
        //find signal
        mapper_signal msig;
        if (mdev_find_output_by_name(x->device, s->s_name, &msig) == -1)
            return;
        
        //check if message payload is correct type
        if (argv->a_type == A_FLOAT) {  //&& msig->props.type == 'f') {
            //get payload
            float payload = atom_getfloat(argv);
            
            //update signal
            msig_update_scalar(msig, (mval) payload);
        }
    }
}

void int_handler(mapper_signal msig, mapper_signal_value_t *v)
{
    t_maxadmin *x = msig->user_data;
	char *path = strdup(msig->props.name);
	
    t_atom myList[2];
    atom_setsym(myList, gensym(path));
    atom_setlong(myList + 1, (*v).i32);
    outlet_list(x->m_outlet, ps_list, 2, myList);
}

void float_handler(mapper_signal msig, mapper_signal_value_t *v)
{
    t_maxadmin *x = msig->user_data;
	char *path = strdup(msig->props.name);
	
    t_atom myList[2];
    atom_setsym(myList, gensym(path));
    atom_setfloat(myList + 1, (*v).f);
    outlet_list(x->m_outlet, ps_list, 2, myList);
}

//void list_handler(mapper_signal msig, mapper_signal_value_t *v)
//{
//}

/*! Creation of a local sender. */
int setup_device(t_maxadmin *x)
{
	if (x->basename) {
		x->device = mdev_new(x->basename, port, 0);
	}
	else {
		x->device = mdev_new("maxadmin", port, 0);
	}

    if (!x->device)
        return 1;
    else
        maxadmin_print_properties(x);
    
    return 0;
}


void *maxadmin_new(t_symbol *s, long argc, t_atom *argv)
{
	t_maxadmin *x;
    long i;
    
    x = object_alloc(maxadmin_class);

    //intin(x,1);
    x->m_outlet3 = outlet_new((t_object *)x,0);
    x->m_outlet2 = outlet_new((t_object *)x,0);
    x->m_outlet = listout((t_object *)x);
        
    for (i = 0; i < argc; i++) {
        if ((argv + i)->a_type == A_SYM) {
			if(strcmp(atom_getsym(argv+i)->s_name, "@alias") == 0) {
				if ((argv + i + 1)->a_type == A_SYM) {
					x->basename = strdup(atom_getsym(argv+i+1)->s_name);
					i++;
				}
			}
			else if ((strcmp(atom_getsym(argv+i)->s_name, "@def") == 0) || (strcmp(atom_getsym(argv+i)->s_name, "@definition") == 0)) {
				if ((argv + i + 1)->a_type == A_SYM) {
					x->definition = strdup(atom_getsym(argv+i+1)->s_name);
					i++;
				}
			}
        }
    }
    
    if (setup_device(x)) {
        post("Error initializing device.\n");
    }
    
    x->ready = 0;
    
    x->m_clock = clock_new(x, (method)poll);	// Create the timing clock
    
    clock_delay(x->m_clock, INTERVAL);  // Set clock to go off after delay
    
    if (x->definition) {
        maxadmin_read_definition(x);
    }
    
	return (x);
}

void maxadmin_read_definition (t_maxadmin *x)
{
    t_dictionary *d = dictionary_new();
    
    //t_max_err dictionary_read (char ∗ filename, short path, t_dictionary ∗∗ d)
    post("got definition: %s", x->definition);
    short path;
    long filetype = 'JSON', outtype;
    
    //add ".json" to end of string if missing (or pick new filetype!)
    
    if (locatefile_extended(x->definition, &path, &outtype, &filetype, 1) == 0) {
        post("located file");
        if (dictionary_read(x->definition, path, &d) == 0) {
            dictionary_dump(d, 1, 0);
        }
        else {
            post("Could not parse file %s", x->definition);
        }
    }
    else {
        post("Could not locate file %s", x->definition);
    }
    
    object_free(d);

}

void poll(t_maxadmin *x)
{
	t_atom myList[2];
	char *message;
    
    mdev_poll(x->device, 0);
    
    if (!x->ready) {
        if (mdev_ready(x->device)) {
            mapper_db_dump();
			
			//output name
			message = strdup(mapper_admin_name(x->device->admin));
			atom_setsym(myList, gensym("name"));
			atom_setsym(myList + 1, gensym(message));
			outlet_list(x->m_outlet3, ps_list, 2, myList);
			
			//output IP
			message = strdup(inet_ntoa(x->device->admin->interface_ip));
			atom_setsym(myList, gensym("IP"));
			atom_setsym(myList + 1, gensym(message));
			outlet_list(x->m_outlet3, ps_list, 2, myList);
			
			//output port
			atom_setsym(myList, gensym("port"));
			atom_setlong(myList + 1, x->device->admin->port.value);
			outlet_list(x->m_outlet3, ps_list, 2, myList);
			
			//output numInputs
			atom_setsym(myList, gensym("numInputs"));
			atom_setlong(myList + 1, mdev_num_inputs(x->device));
			outlet_list(x->m_outlet3, ps_list, 2, myList);
			
			//output numOutputs
			atom_setsym(myList, gensym("numOutputs"));
			atom_setlong(myList + 1, mdev_num_outputs(x->device));
			outlet_list(x->m_outlet3, ps_list, 2, myList);
			
            x->ready = 1;
        }
    }
	clock_delay(x->m_clock, INTERVAL);  // Set clock to go off after delay
}
