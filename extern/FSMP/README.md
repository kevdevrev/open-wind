# FSMP public interface

`PluginAPI.h` and `WindAPI.h` are unmodified copies of the local
`codex/fsmp-wind-api` host's public headers: plugin interface 2.1 and
wind-controller descriptor 2. They expose a generic controller interface and
do not depend on Open Wind, Open Shaders, or private Bullet layouts.

The implementation is derived from Dynamic Wind Field - HDT Physics. The
headers and integration use GPL-3.0; the license text is included in `COPYING`.
