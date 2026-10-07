# __UTIL$PUTMSG_FAO / __UTIL$PUTMSGD_FAO

**Status.** The pair was taken up by one revision of the library and is absent from the one vendored
here, along with the `!%L` directive it relied upon. The demonstration therefore does not build
against the current `3rdparty/utility_routines`. What is kept here is the proposal itself, which the
text below states in full; nothing in BATCH depends on it, the subsystem using the printf-based
`$PUTMSG` that the library has always carried.

The counterpart of `__UTIL$PUTMSG` and `__UTIL$PUTMSGD` in which the text of a message record is
handed to the FAO engine instead of to `vsnprintf`.

The pair lives in `utility_routines.c` beside the existing one, which is left untouched; the new
routines are told apart by the `_fao` suffix. The registry, the log descriptor and the rendering of
an unknown condition are shared with the existing pair, so the addition is confined to formatting.

The change against the vendored library is kept as `utility_routines-fao-putmsg.patch`, ready to be
offered upstream.

## What it buys

A message text becomes an FAO control string, so the whole directive set of the engine is available
to a message — none of it reachable through the C run-time library:

| Directive | Effect | Nearest printf equivalent |
|---|---|---|
| `!n<...!>` | fixed-width field, blank-filled, truncated | `%-20.20s`, and only for strings |
| `!nZL` | zero-filled decimal | `%02d` |
| `!AC` | counted string, length in the first byte | none — `ASC` needs `%.*s` and two arguments |
| `!n*c` | character repeated n times | none |
| `!%S` | `s` when the last number converted is not 1 | none |
| `!%D`, `!%T` | date and time from a binary value | none |
| `!n(...)`, `!n-`, `!n+` | repetition of a group, reuse and skipping of parameters | none |

Taken from the demonstration, `demo.c`:

```
%DEMO-S-ELAPSED, 1 job completed in 1 second
%DEMO-S-ELAPSED, 4 jobs completed in 42 seconds        <- one text, !%S twice
%DEMO-S-QUEUE, queue SYS$BATCH holds 3 entries         <- !AC over an ASC container
  1804  a-job-with-a-very-lo Retained   01:02:03  exit=7    <- !6UL !20<!AZ!> !2ZL
```

The tabular line above is a single message record. With `vsnprintf` the same table has to be
assembled at the point of the call, because a width has to be written into the format string.

## What is in the directory

| File | Contents |
|---|---|
| `utility_routines-fao-putmsg.patch` | the change against the vendored library, `starlet.c` included |
| `demo.c` | a message table whose texts are FAO control strings, and its output |
| `CMakeLists.txt` | build of the demonstration, independent of the BATCH build |

```sh
cmake -S contrib/starlet_putmsg -B build-putmsg
cmake --build build-putmsg && ./build-putmsg/starletmsg_demo
```

## How it was placed

The registry of message records lives in `utility_routines.c` and the FAO engine in `starlet.c`, and
the pair needs both. Of the three placements available it took the second:

1. A module of its own within the library. Nothing existing is touched, but the arrangement gains a
   file for two routines.
2. **Into `utility_routines.c`, beside the existing pair.** The registry is already there and the
   private descriptor `g_logoutput` is reachable, so no way of establishing an output descriptor has
   to be invented. `utility_routines.h` gains one line, `#include "starlet.h"`, and there is no
   cycle: `starlet.h` draws in `<stddef.h>`, `<stdint.h>` and `<time.h>` and nothing else.
3. Into `starlet.c`. This would oblige the registry to move as well, and `starlet.c` would forfeit
   the self-containment it keeps down to its own fallback definitions of `STS$K_*`. Not done.

## Points for the author to settle

**Naming.** The `_fao` suffix keeps the two pairs apart without disturbing the existing names, and
`$PUTMSG_FAO` leaves `$PUTMSG` as it was. Were the FAO form to become the ordinary one, the older
macro could simply be redefined and the suffix retired.

**The parameters come as an array**, not as a variable list: a variable list cannot be forwarded to
`UTIL$FAOL`, and the engine accepts an array anyway. The macro hides this — it assembles a compound
literal and derives the count at translation time — but every parameter has to be cast to
`fao_prm_t` by the caller, since a pointer does not convert to an integer type implicitly. That is
the requirement `UTIL$FAO` already makes of its callers.

**The prefix is laid down by one directive.** The time directives of the engine were rearranged for
it:

| Directive | Rendering | Parameter |
|---|---|---|
| `!%T` | `hh:mm:ss.cc` | `0` or `time_t *` — unchanged |
| `!%D` | `dd-mm-yyyy hh:mm:ss` | `0` or `time_t *` — the month is now numeric and the fraction is gone |
| `!%L` | `dd-mm-yyyy hh:mm:ss.mmm` | `0` or `struct timespec *` — new |

A `time_t` carries no fraction of a second, which is why the stamp had to take a `timespec`; the
`.cc` that `!%D` used to emit was a literal, not a value.

`UTIL$ASCTIM` keeps the OpenVMS rendering `dd-MMM-yyyy hh:mm:ss.cc`. It rested on the same routine
as `!%D`, and a fourth mode was introduced so that it should go on doing so: it is the analogue of
`$ASCTIM` and owes its output format to that service rather than to the taste of the caller. The
queue display of a facility built on it is therefore untouched.

```
!%D    -> 29-07-2026 09:06:40
!%L    -> 29-07-2026 09:06:40.171
!%T    ->             09:06:40.00      unchanged
asctim -> 29-JUL-2026 09:06:40.00      unchanged
```

**The fraction is computed as the existing pair computes it**, by a shift of twenty rather than a
division by a million. The two prefixes therefore agree to the character; verified over five pairs
of calls, the fraction included:

```
29-07-2026 09:32:02.249    556 [CMPTEST\probe:100] %CMP-E-SAME, read()->-1, errno: 5   __util$putmsgd
29-07-2026 09:32:02.249    556 [CMPTEST\probe:100] %CMP-E-SAME, read()->-1, errno: 5   __util$putmsgd_fao
```

The prefix of `__UTIL$PUTMSGD_FAO` is thereby a single directive where the equivalent by parts
needed seven parameters and a call to `localtime_r`.

**Line separator.** The `!/` directive yields CR+LF, which a Unix stream does not expect, so the
separator is written as a literal here. A message text intended for both systems has the same
question to answer.

**Conversion is all or nothing.** The two forms of text are not compatible: `%` is an ordinary
character to FAO and `!` is the escape. A facility moving to the FAO form converts its whole table
at once. The message table of BATCH, 68 entries, is a fair specimen of the work involved.

The change is not only in the directives. Under `vsnprintf` the body of a message has to be written
`%%BATCH-E-NOSUCHENT`, since `%B`, `%C` and the rest are conversions in their own right — an
omitted doubling silently consumes a parameter and shifts the remainder. Under FAO the same body is
written as it reads, `%BATCH-E-NOSUCHENT`.

## What is deliberately absent

The registry is not duplicated: `__UTIL$INIMSG` establishes the table and `__UTIL$GETMSG` locates a
record, exactly as before. A facility that already carries a message table needs no change beyond
the text of its messages.

The log descriptor is not duplicated either: both pairs write to `g_logoutput`, so `__UTIL$DEFLOG`
governs them alike.

A record that cannot be found still names its condition, in the manner of the existing pair:

```
%NONAME-W-NOMSG, condition 08020318, facility 2050, message 99
```
