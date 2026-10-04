# Third-party notices

## OpenVR SDK

`third_party/openvr` contains unmodified files from Valve's OpenVR SDK 2.15.6
(https://github.com/ValveSoftware/openvr), BSD-3-Clause; see
`third_party/openvr/LICENSE`.

## Frametop

`frame/driver/driver_frameeyepointer.cpp`, its resource files, and the laser-mode
overlay and global-priority button handling in `frame/service` are adapted from
Frametop (https://github.com/DeeJanuz/frametop), `pointer/driver` and
`pointer/helper`. The facts about the Steam Frame (Steam's controller handling, the
eye tracking action in games, the eye-server.mmap fields) also come from its
documentation.

```
MIT License

Copyright (c) 2026 DeeJanuz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## frameeyeosc

The eye-server.mmap layout versions 4 and 5 (`core/src/EyeServer.cpp`) come from
https://github.com/konsti219/frameeyeosc (MIT License).
