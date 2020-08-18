
# Contributing to Horse64

If you want to contribute to Horse64, there are multiple tasks you
may want to help out with. Please note that you are expected to
adhere to the [Community Guidelines](./Community%20Guidelines.md)
while participating.


## Developer Certificate of Origin

If you submit a pull request or commit to the project, you do so
under this **Developer Certificate of Origin**:

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.
1 Letterman Drive
Suite D4700
San Francisco, CA, 94129

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.

```
If you make a pull request, therefore please include this line
in your commit(s):

`DCO-1.1-Signed-off-by: Jane Developer Doe <jane@ddoe.example.com>`

Please also add yourself to `AUTHORS.md` if not present (listing
your GitHub profile or email in that list is optional, and up to you).


## Report Bugs

You can help find issues in [horsec](./horsec/horsec.md) and other
tooling by testing interesting corner cases, verifying with this
documentation and other community members that it is likely a bug,
and then **report the issue:**

- [core.horse64.org Bug Tracker (for horsec/horsevm core bugs)](
     https://github.com/horse64/core.horse64.org/issues/
  )
- [multimedia.horse64.org Bug Tracker](
     https://github.com/horse64/multimedia.horse64.org/issues/
  )

For other issues, please locate the bug tracker of the respective
package or project. If it isn't about a specific package,
use the [general bug tracker](https://github.com/horse64/horse64-general).

Don't forget the [guidelines](./Community%20Guidelines.md)
when filing tickets, and to check for duplicate tickets that
were already filed!


## Sources

If you want to contribute to documentation or code, you may
want to dive deeper into the source code. In general, the issue
trackers listed above might also be a good start if you just
need a clue on what needs working on.

Here is where you can find the source code:


### core.horse64.org Package

[https://github.com/horse64/core.horse64.org](
  https://github.com/horse64/core.horse64.org
)

(This repository includes [horsec](./horsec/horsec.md) and horsevm,
the core technology to get anything running based on horse64. It's
largely written in the C programming language. You can build this
repository with `GNU make`.)


### multimedia.horse64.org Package

[https://github.com/horse64/multimedia.horse64.org](
  https://github.com/horse64/multimedia.horse64.org
)

(This repository includes the `multimedia.horse64.org` module that
provides easy graphics and audio access, as well as the default UI.)


---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)*
