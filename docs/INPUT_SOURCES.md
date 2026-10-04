# Input sources: adding hand tracking (or anything else)

FrameEyePointer splits input into two kinds of module:

- **Aim sources** say where the laser points. Built in: `eyes`.
- **Button sources** say when to click, right click, scroll, or toggle. Built in:
  `controllers`.

`~/.config/frameeyepointer/frameeyepointer.ini` picks them:

```ini
[inputs]
aim_sources = hands, eyes          ; first one that is pointing wins
button_sources = controllers, hands ; all combined
```

With that, a hand ray takes over while you point with your hand, and the laser
falls back to your eyes when you stop. A pinch clicks, and so do the bumpers.

Any name that isn't built in is an **external source**: a separate program that
sends messages to the service. A hand tracker can be written in any language and
run in any process, and FrameEyePointer doesn't need to change.

## The protocol

One text message per datagram, sent to the abstract unix socket
`@frameeyepointer_input` on the headset:

```
aim <source> head <ox> <oy> <oz> <dx> <dy> <dz>       ray relative to the headset (-Z forward)
aim <source> tracking <ox> <oy> <oz> <dx> <dy> <dz>   ray in SteamVR standing space (metres)
aim <source> none                                     not pointing (let lower sources aim)
btn <source> click|rightclick|toggle <0|1>
scroll <source> <y>                                   -1..1, positive scrolls up
```

- `<source>` is your source's name (letters, digits, `_`, `-`), the one used in `[inputs]`.
- The direction needn't be unit length; it is normalized.
- Send your state every frame (or at least every `external_timeout_ms`, 250 ms by
  default): a source that goes quiet stops aiming and lets go of its buttons.
- A hand ray is usually in tracking space: from the hand (or the shoulder, for
  stability) through the pinch point. Eye-like or head-locked inputs use `head`.
- Set `smooth_external = true` to run your rays through the same One Euro filter as
  the eyes, if your tracker doesn't smooth them itself.

Python example (a pinch tracker would compute `origin`, `direction` and `pinching`):

```python
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
ADDR = b"\0frameeyepointer_input"

def send_hand(origin, direction, pinching):
    if direction is None:
        s.sendto(b"aim hands none", ADDR)
    else:
        s.sendto(("aim hands tracking %f %f %f %f %f %f" % (*origin, *direction)).encode(), ADDR)
    s.sendto(b"btn hands click %d" % (1 if pinching else 0), ADDR)
```

From a shell, `frameeyepointer-send` (in the install folder) sends test messages:

```sh
# with aim_sources = test, eyes in the settings:
./frameeyepointer-send --repeat 5 'aim test head 0 0 0 0.3 0 -1'   # aim 17 degrees left for 5 s
./frameeyepointer-send 'btn test click 1' 'btn test click 0'
```

## Hand tracking on the Frame: where it could come from

- **SteamVR's own hand tracking**, if Valve exposes it to apps. An OpenVR skeletal
  action or `XR_EXT_hand_tracking` would give joint poses; a small OpenVR client
  could turn the index-finger ray and thumb-index distance into the messages above.
- **[Frametop](https://github.com/DeeJanuz/frametop)'s `hands/`** (experimental,
  MIT): runs palm and hand landmark models on the Frame's tracking cameras and
  detects pinches (`hands/track/pinch.cpp`). An adapter that forwards its ray and
  pinch here would be a few dozen lines.

## Adding a built-in source

For a source that belongs inside the service (for example one that reads a SteamVR
action), implement `frame::InputSource` (`frame/service/InputSource.h`):
`providesAim`/`readAim`, `providesButtons`/`readButtons`, and
`collectActionSets` if it uses SteamVR actions. Then create it by name in
`Service::buildSources()` in `frame/service/frameeyepointerd.cpp`.
