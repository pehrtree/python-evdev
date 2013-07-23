
/*
 * Python bindings to certain linux input subsystem functions.
 *
 * While everything here can be implemented in pure Python with struct and
 * fcntl.ioctl, imho, it is much more straightforward to do so in C.
 *
 */

#include <Python.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/input.h>

#define MAX_NAME_SIZE 256

extern char*  EV_NAME[EV_CNT];
extern int    EV_TYPE_MAX[EV_CNT];
extern char** EV_TYPE_NAME[EV_CNT];
extern char*  BUS_NAME[];


int test_bit(const char* bitmask, int bit) {
    return bitmask[bit/8] & (1 << (bit % 8));
}


// Useful for comparing input events as seen in the extension module
// and as seen in python
// static void
// print_event(struct input_event *ev) {
//     fprintf(stderr, "[so] event: time %ld.%06ld, code %02d, type %02d, val %02d\n",
//             ev->time.tv_sec, ev->time.tv_usec,
//             ev->code, ev->type, ev->value
//     );
// }


// Read input event from a device and return a tuple that mimics input_event
static PyObject *
device_read(PyObject *self, PyObject *args)
{
    int fd;
    struct input_event event;

    // get device file descriptor (O_RDONLY|O_NONBLOCK)
    if (PyArg_ParseTuple(args, "i", &fd) < 0)
        return NULL;

    int n = read(fd, &event, sizeof(event));

    if (n < 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

    PyObject* sec  = PyLong_FromLong(event.time.tv_sec);
    PyObject* usec = PyLong_FromLong(event.time.tv_usec);
    PyObject* val  = PyLong_FromLong(event.value);
    PyObject* py_input_event = NULL;

    py_input_event = Py_BuildValue("OOhhO", sec, usec, event.type, event.code, val);
    Py_DECREF(sec);
    Py_DECREF(usec);
    Py_DECREF(val);

    return py_input_event;
}


// Read multiple input events from a device and return a list of tuples
static PyObject *
device_read_many(PyObject *self, PyObject *args)
{
    int fd, i;

    // get device file descriptor (O_RDONLY|O_NONBLOCK)
    int ret = PyArg_ParseTuple(args, "i", &fd);
    if (!ret) return NULL;

    PyObject* event_list = PyList_New(0);
    PyObject* py_input_event = NULL;
    PyObject* sec  = NULL;
    PyObject* usec = NULL;
    PyObject* val  = NULL;

    struct input_event event[64];

    size_t event_size = sizeof(struct input_event);
    ssize_t nread = read(fd, event, event_size*64);

    if (nread < 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

    // Construct a list of event tuples, which we'll make sense of in Python
    for (i = 0 ; i < nread/event_size ; i++) {
        sec  = PyLong_FromLong(event[i].time.tv_sec);
        usec = PyLong_FromLong(event[i].time.tv_usec);
        val  = PyLong_FromLong(event[i].value);

        py_input_event = Py_BuildValue("OOhhO", sec, usec, event[i].type, event[i].code, val);
        PyList_Append(event_list, py_input_event);

        Py_DECREF(py_input_event);
        Py_DECREF(sec);
        Py_DECREF(usec);
        Py_DECREF(val);
    }

    return event_list;
}


// Unpack a single event (this is essentially a struct.unpack(), without having
// to worry about word size.
static PyObject *
event_unpack(PyObject *self, PyObject *args)
{
    struct input_event event;

    const char *data;
    int len;

    int ret = PyArg_ParseTuple(args, "s#", &data, &len);
    if (!ret) return NULL;

    memcpy(&event, data, sizeof(event));

    Py_RETURN_NONE;
}


// Get the event types and event codes that the input device supports
static PyObject *
ioctl_capabilities(PyObject *self, PyObject *args)
{
    int fd, ev_type, ev_code;
    char ev_bits[EV_MAX/8], code_bits[KEY_MAX/8];
    struct input_absinfo absinfo;

    int ret = PyArg_ParseTuple(args, "i", &fd);
    if (!ret) return NULL;

    // @todo: figure out why fd gets zeroed on an ioctl after the
    // refactoring and get rid of this workaround
    const int _fd = fd;

    // Capabilities is a mapping of supported event types to lists of handled
    // events e.g: {1: [272, 273, 274, 275], 2: [0, 1, 6, 8]}
    PyObject* capabilities = PyDict_New();
    PyObject* eventcodes = NULL;
    PyObject* evlong = NULL;
    PyObject* capability = NULL;
    PyObject* py_absinfo = NULL;
    PyObject* absitem = NULL;

    memset(&ev_bits, 0, sizeof(ev_bits));

    if (ioctl(_fd, EVIOCGBIT(0, EV_MAX), ev_bits) < 0)
        goto on_err;

    // Build a dictionary of the device's capabilities
    for (ev_type=0 ; ev_type<EV_MAX ; ev_type++) {
        if (test_bit(ev_bits, ev_type)) {

            capability = PyLong_FromLong(ev_type);
            eventcodes = PyList_New(0);

            memset(&code_bits, 0, sizeof(code_bits));
            ioctl(_fd, EVIOCGBIT(ev_type, KEY_MAX), code_bits);

            for (ev_code = 0; ev_code < KEY_MAX; ev_code++) {
                if (test_bit(code_bits, ev_code)) {
                    // Get abs{min,max,fuzz,flat} values for ABS_* event codes
                    if (ev_type == EV_ABS) {
                        memset(&absinfo, 0, sizeof(absinfo));
                        ioctl(_fd, EVIOCGABS(ev_code), &absinfo);

                        py_absinfo = Py_BuildValue("(iiiiii)",
                                                   absinfo.value,
                                                   absinfo.minimum,
                                                   absinfo.maximum,
                                                   absinfo.fuzz,
                                                   absinfo.flat,
                                                   absinfo.resolution);

                        evlong = PyLong_FromLong(ev_code);
                        absitem = Py_BuildValue("(OO)", evlong, py_absinfo);

                        // absitem -> tuple(ABS_X, (0, 255, 0, 0))
                        PyList_Append(eventcodes, absitem);

                        Py_DECREF(absitem);
                        Py_DECREF(py_absinfo);
                    }
                    else {
                        evlong = PyLong_FromLong(ev_code);
                        PyList_Append(eventcodes, evlong);
                    }

                    Py_DECREF(evlong);
                }
            }
            // capabilities[EV_KEY] = [KEY_A, KEY_B, KEY_C, ...]
            // capabilities[EV_ABS] = [(ABS_X, (0, 255, 0, 0)), ...]
            PyDict_SetItem(capabilities, capability, eventcodes);

            Py_DECREF(capability);
            Py_DECREF(eventcodes);
        }
    }

    return capabilities;

    on_err:
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
}


// An all-in-one function for describing an input device
static PyObject *
ioctl_devinfo(PyObject *self, PyObject *args)
{
    int fd;

    struct input_id iid;
    char name[MAX_NAME_SIZE];
    char phys[MAX_NAME_SIZE] = {0};

    int ret = PyArg_ParseTuple(args, "i", &fd);
    if (!ret) return NULL;

    memset(&iid,  0, sizeof(iid));

    if (ioctl(fd, EVIOCGID, &iid) < 0)                 goto on_err;
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) goto on_err;

    // Some devices do not have a physical topology associated with them
    ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);

    return Py_BuildValue("hhhhss", iid.bustype, iid.vendor, iid.product, iid.version,
                         name, phys);

    on_err:
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
}


static PyObject *
ioctl_EVIOCGREP(PyObject *self, PyObject *args)
{
    int fd, ret;
    unsigned int rep[2] = {0};
    ret = PyArg_ParseTuple(args, "i", &fd);
    if (!ret) return NULL;

    ioctl(fd, EVIOCGREP, &rep);
    return Py_BuildValue("(ii)", rep[0], rep[1]);
}


static PyObject *
ioctl_EVIOCSREP(PyObject *self, PyObject *args)
{
    int fd, ret;
    unsigned int rep[2] = {0};

    ret = PyArg_ParseTuple(args, "iii", &fd, &rep[0], &rep[1]);
    if (!ret) return NULL;

    ret = ioctl(fd, EVIOCSREP, &rep);
    return Py_BuildValue("i", ret);
}


static PyObject *
ioctl_EVIOCGVERSION(PyObject *self, PyObject *args)
{
    int fd, ret, res;
    ret = PyArg_ParseTuple(args, "i", &fd);
    if (!ret) return NULL;

    ret = ioctl(fd, EVIOCGVERSION, &res);
    return Py_BuildValue("i", res);
}


static PyObject *
ioctl_EVIOCGRAB(PyObject *self, PyObject *args)
{
    int fd, ret, flag;
    ret = PyArg_ParseTuple(args, "ii", &fd, &flag);
    if (!ret) return NULL;

    ret = ioctl(fd, EVIOCGRAB, (intptr_t)flag);
    if (ret != 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

// Adding extra functionality
static PyObject *
ioctl_EVIOCGEFFECTS(PyObject *self, PyObject *args)
{
    int fd, ret, n_effects;
    ret = PyArg_ParseTuple(args, "i", &fd);
    if (!ret) return NULL;

    ret = ioctl(fd, EVIOCGEFFECTS, &n_effects);
    if (ret != 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

    return Py_BuildValue("i", n_effects);
}

static PyObject *
ioctl_EVIOCSFF(PyObject *self, PyObject *args)
{
    int fd, ret;

    // we give just some pertinent fx parameters
    int fxtype, direction,replay_length,replay_delay;
    int attack_level, constant_level, fade_level;
    int attack_length, fade_length;
    int fxid; 
    
    ret = PyArg_ParseTuple(args, "iiiiiiiiiii", &fd, &fxtype, &direction, &replay_length, &replay_delay,
                                &constant_level,
                                &attack_level, &attack_length,
                                &fade_level, &fade_length,
                                &fxid);
    if (!ret) return NULL;
    
    struct ff_effect effect; // need to build this effect from args
    memset(&effect,0,sizeof(effect));
    
    effect.id = fxid; //-1 means save a new effect
	effect.type=fxtype; // TODO check for valid values of this
	effect.trigger.button=0;
	effect.trigger.interval=0;
	effect.replay.length=replay_length;
	effect.replay.delay=replay_delay;
	effect.direction=direction;
	effect.u.constant.level=constant_level;
	effect.u.constant.envelope.attack_length=attack_length;
	effect.u.constant.envelope.attack_level=attack_level;
	effect.u.constant.envelope.fade_length=fade_length;
	effect.u.constant.envelope.fade_level=fade_level;
	
    ret = ioctl(fd, EVIOCSFF, &effect);
    
    if (ret != 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }
    
    int effect_id = effect.id; //driver sets an effect ID after saving
    
    return Py_BuildValue("i", effect_id);

}

static PyObject *
ioctl_EVIOCRMFF(PyObject *self, PyObject *args)
{
    int fd, ret,effect_id;

    ret = PyArg_ParseTuple(args, "ii", &fd, &effect_id);
    if (!ret) return NULL;
    //effect_id is id previously returned from ioctl_EVIOCSFF
    ret = ioctl(fd, EVIOCRMFF, &effect_id);
    
    if (ret != 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
set_FF_AUTOCENTER(PyObject *self, PyObject *args)
{
    int fd, ret, autocenter_force; //0 to 0xFFFFUL;

    ret = PyArg_ParseTuple(args, "ii", &fd, &autocenter_force);
    if (!ret) return NULL;

    struct input_event ff_center;
    ff_center.type = EV_FF;
    ff_center.code = FF_AUTOCENTER; //make it want to return to center, ie 'sticky'
    ff_center.value = autocenter_force;

    ret = write(fd, &ff_center, sizeof(ff_center));

    if (ret == -1) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }
    
    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject *
set_FF_GAIN(PyObject *self, PyObject *args)
{
    int fd, ret, gain; //0 to 0xFFFFUL;

    ret = PyArg_ParseTuple(args, "ii", &fd, &gain);
    if (!ret) return NULL;

    struct input_event ff_gain;
    ff_gain.type = EV_FF;
    ff_gain.code = FF_GAIN; //make it want to return to center, ie 'sticky'
    ff_gain.value = gain;

    ret = write(fd, &ff_gain, sizeof(ff_gain));

    if (ret == -1) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
play_FF_EFFECT(PyObject *self, PyObject *args)
{
    int fd, ret, fxid, ntimes; //ntimes==0 means stop it

    ret = PyArg_ParseTuple(args, "iii", &fd, &fxid, &ntimes);
    if (!ret) return NULL;

    struct input_event ff_play;
    ff_play.type = EV_FF;
    ff_play.code = fxid;
    ff_play.value = ntimes;

    ret = write(fd, &ff_play, sizeof(ff_play));

    if (ret == -1) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }
    
    Py_INCREF(Py_None);
    return Py_None;
}





// todo: this function needs a better name
static PyObject *
get_sw_led_snd(PyObject *self, PyObject *args)
{
    int i, max, fd, evtype, ret;
    PyObject* res = PyList_New(0);

    ret = PyArg_ParseTuple(args, "ii", &fd, &evtype);
    if (!ret) return NULL;

    if (evtype == EV_LED)
        max = LED_MAX;
    else if (evtype == EV_SW)
        max = SW_MAX;
    else if (evtype == EV_SND)
        max = SND_MAX;
    else
        return NULL;

    char bytes[(max+7)/8];
    memset(bytes, 0, sizeof bytes);

    if (evtype == EV_LED)
        ret = ioctl(fd, EVIOCGLED(sizeof(bytes)), &bytes);
    else if (evtype == EV_SW)
        ret = ioctl(fd, EVIOCGSW(sizeof(bytes)), &bytes);
    else if (evtype == EV_SND)
        ret = ioctl(fd, EVIOCGSND(sizeof(bytes)), &bytes);

    for (i=0 ; i<max ; i++) {
        if (test_bit(bytes, i)) {
            PyList_Append(res, Py_BuildValue("i", i));
        }
    }

    return res;
}


static PyMethodDef MethodTable[] = {
    { "unpack",               event_unpack,         METH_VARARGS, "unpack a single input event" },
    { "ioctl_devinfo",        ioctl_devinfo,        METH_VARARGS, "fetch input device info" },
    { "ioctl_capabilities",   ioctl_capabilities,   METH_VARARGS, "fetch input device capabilities" },
    { "ioctl_EVIOCGREP",      ioctl_EVIOCGREP,      METH_VARARGS},
    { "ioctl_EVIOCSREP",      ioctl_EVIOCSREP,      METH_VARARGS},
    { "ioctl_EVIOCGVERSION",  ioctl_EVIOCGVERSION,  METH_VARARGS},
    { "ioctl_EVIOCGRAB",      ioctl_EVIOCGRAB,      METH_VARARGS},
    { "ioctl_EVIOCGEFFECTS",  ioctl_EVIOCGEFFECTS,  METH_VARARGS, "Query number of simultaneous forcefeedback effects"},
    { "ioctl_EVIOCSFF",       ioctl_EVIOCSFF,       METH_VARARGS, "Upload/save force feedback effect"},
    { "ioctl_EVIOCRMFF",      ioctl_EVIOCRMFF,      METH_VARARGS, "Remove a previously-uplaoded force feedback effect"},
    { "set_FF_AUTOCENTER",    set_FF_AUTOCENTER,    METH_VARARGS, "Set forcefeedback autocenter 0 - 0xFFFF"},
    { "set_FF_GAIN",          set_FF_GAIN,          METH_VARARGS, "Set forcefeedback gain 0 - 0xFFFF"},
    { "play_FF_EFFECT",       play_FF_EFFECT,       METH_VARARGS, "Play a previously-uploaded forcefeedback effect"},
    
    { "get_sw_led_snd",       get_sw_led_snd,       METH_VARARGS},
    { "device_read",          device_read,          METH_VARARGS, "read an input event from a device" },
//   { "device_write_effect",  device_write_effect,  METH_VARARGS, "Write a Force Feedback effect to device"},         
    { "device_read_many",     device_read_many,     METH_VARARGS, "read all available input events from a device" },

    { NULL, NULL, 0, NULL}
};


#define MODULE_NAME "_input"
#define MODULE_HELP "Python bindings to certain linux input subsystem functions"

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    MODULE_NAME,
    MODULE_HELP,
    -1,          /* m_size */
    MethodTable, /* m_methods */
    NULL,        /* m_reload */
    NULL,        /* m_traverse */
    NULL,        /* m_clear */
    NULL,        /* m_free */
};

static PyObject *
moduleinit(void)
{
    PyObject* m = PyModule_Create(&moduledef);
    if (m == NULL) return NULL;
    return m;
}

PyMODINIT_FUNC
PyInit__input(void)
{
    return moduleinit();
}

#else
static PyObject *
moduleinit(void)
{
    PyObject* m = Py_InitModule3(MODULE_NAME, MethodTable, MODULE_HELP);
    if (m == NULL) return NULL;
    return m;
}

PyMODINIT_FUNC
init_input(void)
{
    moduleinit();
}
#endif
