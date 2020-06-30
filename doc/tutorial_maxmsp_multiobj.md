# Getting started with libmapper and Max

Note: this tutorial will introduce the revised bindings for MaxMSP (July 2013).
You can access the tutorial for the original bindings using the `[mapper]`
object [here](./maxmsp_central.html).

To start using the _libmapper_ with MaxMSP you will need to:

* Download the external objects from our [downloads page](../downloads.html).
Alternatively, you can build the object from
[source](http://github.com/malloch/mapper-max-pd) instead.
* Download a [graphical user interface](../downloads.html#GUIs) for creating and
editing mapping connections.

## Devices

### Creating a device

To create a _libmapper_ device, create an object called `[mpr.device]` with an
optional argument specifying the device's name.  There is a brief initialization
period after a device is created where a unique ordinal is chosen to append to
the device name.  This allows multiple devices with the same name to exist on
the network.  If no name is given libmapper will choose a name for your device
starting with the string "maxmsp".

Your device will start with no inputs or outputs, so it will not yet show up in
most GUIs.

You can also provide arbitrary properties to your device using jitter-style
properties.  By default, libmapper will try to guess which network interface to
use for mapping, defaulting to the local loopback interface if ethernet or wifi
is not available.  You can force the object to use a particular interface by
using the `@interface` property.

An example of creating a device:

<img style="padding:0px;box-shadow:0 4px 8px 0" src="./images/maxmsp_multiobj1.png" alt="Creating a device."/>

Once the object has initialized, it will output its metadata from its outlet:

* The complete device name, including an appended ordinal for distinguishing
between multiple active devices with the same name.
* The IP address and port in use by the object.
* The number of input and output signals associated with the object (none to
start).
* The network interface in use by the object.

If and when this information changes, the object will output the updated
property.

### Multiple devices

Once you have created a `[mpr.device]` object in a patcher, it considers itself
to be the "parent" device for that patcher and all of its subpatchers.  This
means that it you try to create a second copy of the device in the same patcher,
or in one of its child subpatchers, the object instantiation will fail.  When
the object is created, it checks whether there is a pre-existing device that
would cause a conflict.

You can, however, create multiple `[mpr.device]` objects in the same patch as
long as they are both contained in different subpatchers.

## Signals

Now that we have created a device, we only need to know how to add signals in
order to give our program some input/output functionality.  While libmapper
enables arbitrary connections between _any_ declared signals, we still find it
helpful to distinguish between two type of signals: `inputs` and `outputs`.

- `outputs` signals are _sources_ of data, updated locally by their parent
device
- `inputs` signals are _consumers_ of data and are **not** generally
updated locally by their parent device.

This can become a bit confusing, since the "reverb" parameter of a sound
synthesizer might be updated locally through user interaction with a GUI,
however the normal use of this signal is as a _destination_ for control data
streams so it should be defined as an `input` signal.  Note that this
distinction is to help with GUI organization and user-understanding –
_libmapper_ enables connections from input signals and to output signals if
desired.

### Creating a signal

Creating input and output signals is accomplished with the `[mpr.in]` and
`[mpr.out]` objects, which requires two pieces of information:

* a name for the signal
* the signal's data type expressed as a character 'i' for `integer`, 'f' for
`float`

A third optional integer argument sets the signal's vector length, if it is
omitted the signal is assumed to have length 1.  Additional signal properties
can also (optionally) be added:

* the signal's unit, e.g. `@unit Hz`
* the signal's minimum value, e.g. `@min 0`
* the signal's maximum value, e.g. `@max 100`

examples:

<img style="padding:0px;box-shadow:0 4px 8px 0" src="./images/maxmsp_multiobj2.png" alt="Adding signals to a device."/>

The only _required_ parameters here are the signal name and data type, but the
more information you provide, the more the mapper can do some things
automatically.  For example, if `minimum` and `maximum` are provided, it will be
possible to create linear-scaled connections very quickly.  If `unit` is
provided, the mapper will be able to similarly figure out a linear scaling based
on unit conversion. (Centimeters to inches for example.) Currently automatic
unit-based scaling is not a supported feature, but will be added in the future.
You can take advantage of this future development by simply providing unit
information whenever it is available.  It is also helpful documentation for
users.

An example of a `float` signal where some more information is provided:

<img style="padding:0px;box-shadow:0 4px 8px 0" src="./images/maxmsp_multiobj3.png" alt="Adding a floating-point output signal with some optional properties."/>

So far we know how to create a device and to specify an output signal for it.


### Updating signals

We can imagine the above program getting sensor information in a loop.  It could
be running on a computer and reading data from an Arduino over a USB serial
port, or it could just be a mouse-controlled GUI slider.  However it's getting
the data, it must provide it to _libmapper_ so that it will be sent to other
devices if that signal is mapped.

This is accomplished by passing ints or floats to the `[mpr.out]` object.  In
the case of vector signals, a list with the same number of elements should be
used.

So in the "sensor 1 voltage" example, assuming that we have some code which
reads sensor 1's value into a float variable in `[p read_sensor]`, the patch
becomes:

<img style="padding:0px;box-shadow:0 4px 8px 0" src="./images/maxmsp_multiobj4.png" alt="Updating a signal."/>

This is about all that is needed to expose sensor 1's voltage to the network as
a mappable parameter.  The _libmapper_ GUI can now be used to create a mapping
between this value and a receiver, where it could control a synthesizer
parameter or change the brightness of an LED, or whatever else you want to do.

### Signal conditioning

Most synthesizers of course will not know what to do with "voltage"--it is an
electrical property that has nothing to do with sound or music.  This is where
_libmapper_ really becomes useful.

Scaling or other signal conditioning can be taken care of _before_ exposing the
signal, or it can be performed as part of the mapping.  Since the end user can
demand any mathematical operation be performed on the signal, he can perform
whatever mappings between signals as he wishes.

As a developer, it is therefore your job to provide information that will be
useful to the end user.

For example, if sensor 1 is a position sensor, instead of publishing "voltage",
you could convert it to centimeters or meters based on the known dimensions of
the sensor, and publish a "/sensor1/position" signal instead, providing the unit
information as well.

We call such signals "semantic", because they provide information with more
meaning than a relatively uninformative value based on the electrical properties
of the sensing technique.  Some sensors can benefit from low-pass filtering or
other measures to reduce noise.  Some sensor data may need to be combined in
order to derive physical meaning.  What you choose to expose as outputs of your
device is entirely application-dependent.

You can even publish both "sensor1/position" and "sensor1/voltage" if desired,
in order to expose both processed and raw data.  Keep in mind that these will
not take up significant processing time, and _zero_ network bandwidth,
if they are not mapped.

### Receiving signals

Receiving signals is even easier: create a `[mpr.in]` object with a name and
type, and updates for this signal will be routed to its outlet.  The arguments
for the `[mpr.in]` object are identical to `[mpr.out]`.

Let's try making two devices in the same patch for testing.

![Sending and receiving signal updates](./images/maxmsp_multiobj5.png)

If you use your mapping GUI to create a link between the two devices _sender_
and _receiver_ and a connection between your two signals _/sendsig_ and
_/recvsig_, any change made to the float value on the left will cause a
corresponding output on the right.

Congratulations - you have created your first mapping connection! This probably
seems quite simplistic, since you could have made a patch-cord between the two
float objects and accomplished the same thing, but your "mapping" represents
something more:

* It can be edited remotely from another machine on the network.
* It can connect signals on different computers.
* It can connect different signals implemented in different programming
languages such as C, C++, Python, Java, and SuperCollider.
* It can be edited to provide signal processing, including automatic linear
scaling, calibration, muting, clipping, or an arbitrary expression - even FIR
and IIR filters.


## Publishing metadata

Things like device names, signal units, and ranges, are examples of metadata —
information about the data you are exposing on the network.

_libmapper_ also provides the ability to specify arbitrary extra metadata in the
form of name-value pairs.  These are not interpreted by _libmapper_ in any way,
but can be retrieved over the network.  This can be used for instance to label a
device with its location, or to perhaps give a signal some property like
"reliability", or some category like "light", "motor", "shaker", etc.

Some GUI could then use this information to display information about the
network in an intelligent manner.

Any time there may be extra knowledge about a signal or device, it is a good
idea to represent it by adding such properties, which can be of any
OSC-compatible type.  (So, numbers and strings, etc.)

The MaxMSP bindings for libmapper do not currently allow dynamically changing
the properties of a device or signal, however they can be declared when the
entity is created by using jitter-style property arguments

For example, to store a `float` indicating the X position of a device `dev`, you
could instantiate your object like this:

<img style="padding:0px;box-shadow:0 4px 8px 0" src="./images/maxmsp_multiobj6.png" alt="Adding extra properties to a device."/>

To specify a string property of a signal:

<img style="padding:0px;box-shadow:0 4px 8px 0" src="./images/maxmsp_multiobj7.png" alt="Adding extra properties to a signal."/>

### Reserved keys

You can use any property name not already reserved by libmapper.

#### Reserved keys for devices

`description`, `host`, `id`, `is_local`, `libversion`, `name`,
`num_incoming_maps`, `num_outgoing_maps`, `num_inputs`, `num_outputs`, `port`,
`synced`, `value`, `version`, `user_data`

#### Reserved keys for signals

`description`, `direction`, `id`, `is_local`, `length`, `max`, `maximum`, `min`,
`minimum`, `name`, `num_incoming_maps`, `num_instances`, `num_outgoing_maps`,
`rate`, `type`, `unit`, `user_data`

#### Reserved keys for links

`description`, `id`, `is_local`, `num_maps`

#### Reserved keys for maps

`description`, `expression`, `id`, `is_local`, `mode`, `muted`,
`num_destinations`, `num_sources`, `process_location`, `ready`, `status`

#### Reserved keys for map slots

`bound_max`, `bound_min`, `calibrating`, `causes_update`, `direction`, `length`,
`maximum`, `minimum`, `num_instances`, `use_as_instance`, `type`
