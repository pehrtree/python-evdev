# encoding: utf-8

'''
This module provides the :class:`InputEvent` class, which closely
resembles the ``input_event`` C struct in ``linux/input.h``:

.. code-block:: c

    struct input_event {
        struct timeval time;
        __u16 type;
        __u16 code;
        __s32 value;
    };

This module also defines several abstractions on top of :class:`InputEvent`
that know more about the different event types (key, abs, rel etc). The
:data:`event_factory` dictionary maps event types to these classes.

Assuming you use the provided :func:`evdev.util.categorize()` function to
categorize events according to type, adding or replacing a class for a specific
event type becomes a matter of modifying :data:`event_factory`.

All of the provided classes have reasonable ``str()`` and ``repr()`` methods::

    >>> print(event)
    event at 1337197425.477827, code 04, type 04, val 458792
    >>> print(repr(event))
    InputEvent(1337197425L, 477827L, 4, 4, 458792L)

    >>> print(key_event)
    key event at 1337197425.477835, 28 (KEY_ENTER), up
    >>> print(repr(key_event))
    KeyEvent(InputEvent(1337197425L, 477835L, 1, 28, 0L))
'''

# event type descriptions have been taken mot-a-mot from:
# http://www.kernel.org/doc/Documentation/input/event-codes.txt

from evdev.ecodes import keys, KEY, SYN, REL, ABS, EV_KEY, EV_REL, EV_ABS, EV_SYN,FF_CONSTANT,FF


class InputEvent(object):
    '''
    A generic input event. This closely resembles the ``input_event`` C struct.
    '''

    __slots__ = 'sec', 'usec', 'type', 'code', 'value'

    def __init__(self, sec, usec, type, code, value):
        #: Time in seconds since epoch at which event occurred
        self.sec  = sec

        #: Microsecond portion of the timestamp
        self.usec = usec

        #: Event type - one of ``ecodes.EV_*``
        self.type = type

        #: Event code related to the event type
        self.code = code

        #: Event value related to the event type
        self.value = value

    def timestamp(self):
        ''' Return event timestamp as a python float. '''
        return self.sec + (self.usec / 1000000.0)

    def __str__(s):
        msg = 'event at {:f}, code {:02d}, type {:02d}, val {:02d}'
        return msg.format(s.timestamp(), s.code, s.type, s.value)

    def __repr__(s):
        msg = '{}({!r}, {!r}, {!r}, {!r}, {!r})'
        return msg.format(s.__class__.__name__,
                          s.sec, s.usec, s.type, s.code, s.value)


class KeyEvent(object):
    '''
    Used to describe state changes of keyboards, buttons, or other
    key-like devices.
    '''

    key_up   = 0x0
    key_down = 0x1
    key_hold = 0x2

    __slots__ = 'scancode', 'keycode', 'keystate', 'event'

    def __init__(self, event):
        if event.value == 0:
            self.keystate = KeyEvent.key_up
        elif event.value == 2:
            self.keystate = KeyEvent.key_hold
        elif event.value == 1:
            self.keystate = KeyEvent.key_down

        self.keycode  = keys[event.code]  # :todo:
        self.scancode = event.code

        #: :class:`InputEvent` instance
        self.event = event

    def __str__(self):
        try: ks = ('up', 'down', 'hold')[self.keystate]
        except IndexError: ks = 'unknown'

        msg = 'key event at {:f}, {} ({}), {}'
        return msg.format(self.event.timestamp(),
                          self.scancode, self.keycode, ks)

    def __repr__(s):
        return '{}({!r})'.format(s.__class__.__name__, s.event)


class RelEvent(object):
    '''
    Used to describe relative axis value changes, e.g. moving the
    mouse 5 units to the left.
    '''

    __slots__ = 'event'

    def __init__(self, event):
        #: :class:`InputEvent` instance
        self.event = event

    def __str__(self):
        msg = 'relative axis event at {:f}, {} '
        return msg.format(self.event.timestamp(), REL[self.event.code])

    def __repr__(s):
        return '{}({!r})'.format(s.__class__.__name__, s.event)


class AbsEvent(object):
    '''
    Used to describe absolute axis value changes, e.g. describing the
    coordinates of a touch on a touchscreen.
    '''

    __slots__ = 'event'

    def __init__(self, event):
        #: :class:`InputEvent` instance
        self.event = event

    def __str__(self):
        msg = 'absolute axis event at {:f}, {} '
        return msg.format(self.event.timestamp(), ABS[self.event.code])

    def __repr__(s):
        return '{}({!r})'.format(s.__class__.__name__, s.event)


class SynEvent(object):
    '''
    Used as markers to separate events. Events may be separated in time or
    in space, such as with the multitouch protocol.
    '''

    __slots__ = 'event'

    def __init__(self, event):
        #: :class:`InputEvent` instance
        self.event = event

    def __str__(self):
        msg = 'synchronization event at {:f}, {} '
        return msg.format(self.event.timestamp(), SYN[self.event.code])

    def __repr__(s):
        return '{}({!r})'.format(s.__class__.__name__, s.event)


''' 
    Forcefeedback notes from input.h
  ff_effect 
    struct ff_effect {
	__u16 type;
	__s16 id;
	__u16 direction;
	struct ff_trigger trigger;
	struct ff_replay replay;

	union {
		struct ff_constant_effect constant;  // Linux HID only does FF_CONSTANT right now
	} u;
};	
	 * Direction of the effect is encoded as follows:
 *	0 deg -> 0x0000 (down)
 *	90 deg -> 0x4000 (left)
 *	180 deg -> 0x8000 (up)
 *	270 deg -> 0xC000 (right)
 
 
 All duration values are expressed in ms. Values above 32767 ms (0x7fff)
 * should not be used and have unspecified results.

  * Valid range for the attack and fade levels is 0x0000 - 0x7fff
  # constant.level can be negative?

 '''  

class FFReplay(object):
    '''
    FF replay for scheduling the effect. This closely resembles the ``ff_replay`` C struct.

    All unsigned 16-bits
    '''
    __slots__ = 'length', 'delay'

    def __init__(self,length=500, delay=1):
        #: duration of the effect (ms)
        self.length  = int(length)

        #: delay before effect should start playing
        self.delay = int(delay)


    def __str__(s):
        msg = 'ff_replay length {:d}ms replay after {:d}ms '
        return msg.format(s.length, s.delay)

 
class FFTrigger(object):
    '''
    FF trigger for triggering the effect. This closely resembles the ``ff_trigger`` C struct.

    All unsigned 16-bits
    '''
    __slots__ = 'button', 'interval'

    def __init__(self,button=0, interval=0):
        #: number of the button triggering the effect
        self.button  = int(button)

        #: controls how soon the effect can be re-triggered (ms)
        self.interval = int(interval)


    def __str__(s):
        msg = 'ff_trigger button {:d} interval {:d}ms '
        return msg.format(s.button, s.interval)

 
class FFEnvelope(object):
    '''
    FF fade envelope. This closely resembles the ``ff_envelope`` C struct.
    
    Notes from input.h:
    All duration values are expressed in ms. Values above 32767 ms (0x7fff)
    should not be used and have unspecified results.
    
    The @attack_level and @fade_level are absolute values; when applying
    envelope force-feedback core will convert to positive/negative
    value based on polarity of the default level of the effect.
    
    Valid range for the attack and fade levels is 0x0000 - 0x7fff
 
     All unsigned 16-bits

    '''

    __slots__ = 'attack_length', 'attack_level', 'fade_length', 'fade_level'

    MAX_LENGTH = 0x7FFF
    MAX_LEVEL = 0x7FFF
    def __init__(self,attack_length=150, attack_level=0x3fff, fade_length=1000, fade_level=0):
        #: duration of the attck (ms)
        self.attack_length  = int(attack_length)

        #: level at the beginning of the attack
        self.attack_level = int(attack_level)

        #: duration of the fade (ms)
        self.fade_length = int(fade_length)

        #: level at the end of the fade
        self.fade_level = int(fade_level)

    

    def __str__(s):
        msg = 'ff_envelope attack level 0x{:x} {:d}ms fade to  0x{:x} over {:d} ms'
        return msg.format(int(s.attack_level), int(s.attack_length), int(s.fade_level), int(s.fade_length))

class FFConstantEffect(object):
    ''' 
    Parameters for a constant effect. this closely resembles the ``ff_constant`` C struct.
    level is signed 16-bits
    envelope is FFEnvelope object
    '''
    __slots__ = 'level', 'envelope'

    def __init__(self,level=0x3ff, envelope=FFEnvelope()):
        #: beginning strength of the effect; may be negative
        self.level  = int(level)

        if type(envelope) is not FFEnvelope:
            raise Exception("FFConstantEffect: envelope %s is not FFEnvelope"%(envelope))
            
        #: envelope data
        self.envelope = envelope


    def __str__(s):
        msg = 'ff_constant effect start level 0x{:x} envelope {:s}'
        return msg.format(s.level, s.envelope)

class FFEffect(object):
    ''' 
    Parameters for a force feedback effect. this closely resembles the ``ff_effect`` C struct.
    type is unsigned 16-bits
    id is signed 16 bits
    direction is signed 16 bits
    
    ff_replay is FFReplay object
    ff_trigger is FFTrigger object
    effect is FFConstant Effect. effect is the 'u' union in the real struct.
    
    Direction of the effect is encoded as follows:
    0 deg -> 0x0000 (down)
    90 deg -> 0x4000 (left)
    180 deg -> 0x8000 (up)
    270 deg -> 0xC000 (right)
  
    
    '''
    __slots__ = 'type', 'id','direction', 'trigger','replay','effect'
    
    FX_DOWN = 0
    FX_LEFT = 0x4000
    FX_UP = 0x8000
    FX_RIGHT = 0xC000
    
    directions={0:"DOWN",0x4000:"LEFT",0x8000:"UP", 0xC000:"RIGHT"}
    
    def __init__(self,_type, id,direction,trigger,replay,effect):
        
        #: effect type code - FF_CONSTANT, etc
        self.type  = _type

        #: id of the effect. -1 means create a new one
        self.id  = id

        #: direction of the effect.
        self.direction  = direction


        if type(trigger) is not FFTrigger:
            raise Exception("FFEffect: trigger %s is not FFTrigger"%(trigger))     
        #: how the effect is triggered
        self.trigger = trigger
        
        
        if type(replay) is not FFReplay:
            raise Exception("FFEffect: replay %s is not FFReplay"%(replay))    
        #: how the effect is triggered
        self.replay = replay


        # assign the specific effect
        if type(effect) is not FFConstantEffect:
            raise Exception("FFEffect: effect type %s is not supported"%(type(effect)))    
        self.effect = effect

    def __str__(s):
        msg = 'ff_effect id {:d} type {:s} direction {:s} effect: {:s}'
        return msg.format(s.id, FF[s.type], FFEffect.directions.get(s.direction,s.direction),s.effect)  
                                 
#: Used by :func:`evdev.util.categorize()`
event_factory = {
    EV_KEY: KeyEvent,
    EV_REL: RelEvent,
    EV_ABS: AbsEvent,
    EV_SYN: SynEvent,
}


__all__ = ('InputEvent', 'KeyEvent', 'RelEvent', 'SynEvent',
           'AbsEvent', 'event_factory','FFEffect','FFConstantEffect')
