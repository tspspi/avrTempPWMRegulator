# PWM Temperature regulator

This is a small PID temperature regulator that's controlled by an AVR. Basically
it's an PWM regulator that gets temperature feedback. For each control channel
there are up to two measured temperature values. Each channel can have a different
target temperature value and a different slope limit.

The controller ensures that:

* The temperature difference between both channels (if both are enabled) stays
  below a certain limit
* The temperature increase and decrease has a limited gradient (since this
  controller is used for vacuum control). Thus the target temperature is also
  only slowly increased and decreased - this is implemented by simply having
  a target value and a current target. Whenever the actual reaches the current
  target the current target is moved into the direction of the target value

## PWM frequency

Since the control is done primary side on a pretty slow (thermal) system the PWM
timeslice has been decided to be around 4 seconds (0.25 Hz) with a controlable
duty cycle from 0-100 percent.

The PWM module works synchronously - on every iteration it simply checks if the
next PWM clock cycle has ellapsed (every 40 ms - i.e. every 800000 clock cycle
thus I won't use a timer; introduces some jitter but for this application it's
neglectable)

## Serial protocol

All serial messages start with at least a synchronization pattern ```0xAA, 0x55```.
After this comes a 1 byte length and 1 byte opcode field. The end of the packet
is always formed by a checksum - all bytes including the synchronization pattern
exclusively xor'ed should result in ```0x00```. The absolute minimum packet length
thus is 5 bytes.

| Offset | Length | Content                                                                       |
| ------ | ------ | ----------------------------------------------------------------------------- |
| 0      | 2      | ```0xAA, 0x55```                                                              |
| 2      | 1      | Length including length and opcode field as well as checksum and sync pattern |
| 3      | 1      | Operation code field                                                          |
| 4      | n-5    | Payload                                                                       |
| n-1    | 1      | Checksum                                                                      |

There is no terminator field since the next message starts with a synchronization
pattern again.

### OpCodes assigned

| Code | Operation |
| ---- | --------- |
| 0x01 | Identify  |

### Response codes assigned

| Code | Response type                            |
| ---- | ---------------------------------------- |
| 0x01 | Error packet including 1 byte error code |
| 0x02 | Identify response                        |

### Identification command (Operation code 0x01, response code 0x01)

| Offset | Length | Content           |
| ------ | ------ | ----------------- |
| 0      | 2      | ```0xAA, 0x55```  |
| 2      | 1      | Length (5)        |
| 3      | 1      | OpCode ```0x01``` |
| 4      | 1      | Checksum          |
