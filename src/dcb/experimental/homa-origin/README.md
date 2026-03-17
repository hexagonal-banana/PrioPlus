`src/dcb/model/rocev2-homa.{cc,h}` is the active Homa implementation used by the
current DCB/RoCEv2 simulator and referenced by the build.

The files in this directory are preserved as an experimental/original Homa
prototype:

- `homa-l4-protocol.*`
- `homa-socket.*`
- `homa-socket-factory.*`
- `homa-header.*`
- `HomaL4Protocol-*.cc`

They are not wired into the current build. Keeping them under `src/dcb` makes
their relationship to the active implementation explicit without changing
runtime behavior.
