# UE mock compile check

`check.sh` runs `clang++ -fsyntax-only -Wall -Wextra -Wshadow -Werror` over every
`Client/Source/TacticalFPS/Private/*.cpp` using `UeMock.h`, a hand-written mock
of the small Unreal Engine API subset the client uses.

**What it proves:** the Unreal glue code has no syntax errors and no type
errors in its use of our own classes, NetCore and the standard library.
There are no shadowing or unused-variable issues, given the API signatures
the mock assumes.

**What it does not prove:** that the code compiles against the real Unreal
Engine. The mock's signatures are written from knowledge of UE 5.4 and can be
wrong. UHT is not run (no reflection-specifier validation). Linking and
runtime behaviour are not checked. A real UE build
(`Scripts/build_client.sh` / `.ps1`) is required for that and is the first
item of the next milestone on a machine with Unreal Engine installed.

```bash
Tools/UeMockCheck/check.sh
```
