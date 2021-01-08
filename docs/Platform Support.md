

# Platform Support

The core Horse64 tooling currently is maintained according with the
following platform support in mind:

|Platform                       | Support tier (1=best)|
|-------------------------------|----------------------|
|Linux x64                      |  Tier 1              |
|Linux ARM64 (including phones) |  Tier 1              |
|Windows x64   (gcc or clang)   |  Tier 2              |
|Free/Open/NetBSD x64/ARM64     |  Tier 3              |
|Windows ARM64 (gcc or clang)   |  Tier 3              |
|Windows x64/ARM64 via MSVC     |  Tier 4              |
|macOS x64/ARM64                |  Tier 4              |
|Android ARM64                  |  Tier 4              |
|iOS ARM64                      |  Tier 4              |


## Tiers

### Platform tiers with official releases:

**Tier 1:** this platform is tested in Continuous Integration, gets official
binary releases for easy use, and we aim to run CI testing and other more
involved regression monitoring. All features should be ported with full
scalability support.

**Tier 2:** this platform is tested in Continuous Integration, gets official
binary releases for easy use, and we aim to run CI testing and other more
involved regression monitoring. Some features may have less scalable
implementations, but all should generally be available in a usable form.

### Platform tiers without official releases:

**Tier 3:** this platform is not tested in Continuous Integration, and
doesn't currently get any official binary releases, and may not currently
work in a manual bulid either. However, we are very interested in patches
to get all features to work, even if this means larger code changes.
These platforms are of high interest to be promoted to Tier 2, once the
CI work is done.

**Tier 4:** this platform is not tested or supported, and due to a mix of
tooling issues and platform owner policies we do not aim to provide
extensive support for this platform any time soon. Patches to help with a
manual build might be rejected unless a simple enough approach to
reasonably support these targets is figured out.


---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)*
