# BATCH — batch execution subsystem for shell scripts

A functional equivalent of the OpenVMS batch job queue (`SUBMIT` / `SHOW QUEUE`) for Linux.
A script is placed in a queue and executed in the background under the control of a resident
job controller, rather than being started directly as `cron` does.

The differences from a periodic scheduler are deliberate:

* a job is identified by a **queue entry number** assigned at submission time, not by a process
  identifier; the entry number exists before any process does;
* the queue has **states** — a job may wait for a free execution slot or for a designated
  start time;
* the number of **simultaneously executing jobs is limited**;
* a job that **terminates abnormally stays in the queue** together with its log file and is
  displayed by `show` with no qualifier at all.

Two manuals, both written in the style of an OpenVMS one:

* **[doc/batch_users_guide.md](doc/batch_users_guide.md)** — the User's Guide: installation and
  configuration of the service, the commands, every message with the action it calls for, and
  troubleshooting. For whoever uses or operates the subsystem.
* **[doc/batch_programmers_guide.md](doc/batch_programmers_guide.md)** — the Programmer's Guide: the
  concept and the decisions behind it, the architecture, the modules and their routines, the
  conventions, and how to make the changes that come up most often.

Both exist in Russian (`_rus`) and in French (`_fra`) beside the English, and each is rendered as a
PDF kept beside its markdown, so that reading a manual asks for no TeX installation.

Both exist in Russian as well, with the suffix `_rus`:
**[doc/batch_users_guide_rus.md](doc/batch_users_guide_rus.md)** and
**[doc/batch_programmers_guide_rus.md](doc/batch_programmers_guide_rus.md)**. The message reference of
the Russian guide keeps the message texts in English, since that is what the program prints.

All four are carried in `doc/` as PDF as well as markdown, and both forms are installed. The reader
of a manual is not expected to hold a TeX installation in order to read it, and there is one set of
sources to hand over rather than a set and an archive of documentation beside it.

Configuring with `-DBATCH_M_PDF=ON` re-renders the PDF from the markdown, which is what to do after
changing a manual; it calls for pandoc and a TeX installation. Without it the PDF already in `doc/`
is used as it stands.

## Layout

| Directory | Contents |
|---|---|
| `src/` | sources and headers (see the table below) |
| `test/` | the functional test `smoke.sh` and its registration with `ctest` |
| `doc/` | the User's and Programmer's Guides |
| `etc/` | templates of the systemd unit and of the System V start script |
| `cmake/` | the removal script |

| Module | Purpose |
|---|---|
| `batch.c` | command line utility: DCL parsing, exchange with the job controller, display |
| `batchd.c` | job controller: queue, scheduler, job creation, journal |
| `util.c` | general-purpose routines: FAO output, time, paths, socket transfer |
| `batchmsg.c` | facility message table |
| `batchmsg.h` | condition values of the BATCH facility |
| `batchdef.h` | job states and job descriptor |
| `batchipc.h` | protocol between the utility and the job controller |
| `batchvers.h.in` | identification header template, filled in by CMake |

Two images are built: **`batch`**, the command line utility, installed into `bin`, and
**`batchd`**, the job controller, installed into `sbin`.

## Building

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
sudo cmake --install build
sudo systemctl restart batchd                 # after an upgrade; without systemd: service batchd restart
```

The installation puts `batch` in `bin`, `batchd` in `sbin`, this file and the guide in
`share/doc/BATCH`, the systemd unit in `/lib/systemd/system` — or, on a system without systemd, the
System V start script in `/etc/init.d` — and the subsystem root in `/var/spool/batch`.

Removal reads the manifest written at installation time:

```sh
sudo cmake --build build --target uninstall
```

Installing enables and starts the job controller — through `systemctl`, or through `update-rc.d` and
the start script where there is no systemd — and removing stops and disables it before any file
goes. A controller already running is not replaced: restart it after an upgrade. A staged
installation — one made with `DESTDIR` set — leaves both to whoever installs the package.
`BATCH_M_AUTOSTART=OFF` turns the starting off.

The distribution kit is assembled by a target of its own and is named for the facility and the
revision it carries — `BATCH-X-01-08.tar.gz` — in the manner of the kits of the neighbouring products, so
that one lies beside another and is read the same way:

```sh
cmake --build build --target dist
```

The subsystem root and the log directory survive it while they hold anything: a removal of the
software is not a reason to discard the queue journal or the logs of jobs.

CMake 3.16 or later, a compiler supporting gnu11, and Linux are required: the subsystem uses
UNIX domain sockets, the `SO_PEERCRED` socket option, `daemon(3)` and `poll(2)`.

### Configuration variables

| Variable | Default | Purpose |
|---|---|---|
| `BATCH_K_SPOOLROOT` | `/var/spool/batch` | subsystem root directory |
| `BATCH_K_SHELL` | `/bin/sh` | command interpreter created for a job |
| `BATCH_K_FACILITY` | `BATCH` | facility name appearing in messages |
| `BATCH_K_IDENT` | `X-02-02` | product revision identification |
| `BATCH_M_STRICT` | `OFF` | treat compiler diagnostics as errors |
| `BATCH_M_TESTS` | `ON` | build and register the functional tests |
| `BATCH_M_SYSTEMD` | `ON` | install the systemd unit of the job controller |
| `BATCH_M_SYSVINIT` | `ON` without systemd | install the System V start script of the job controller |
| `BATCH_M_PDF` | `OFF` | render the manuals as PDF and install them as well |
| `BATCH_M_STATICLIB` | `ON` | bind UTILITY$ROUTINES into the images rather than share it |

UTILITY$ROUTINES is bound into the images. The package offers a shared library and it would be
preferable in the ordinary way, but the layouts of its structures are declared in its headers and
this subsystem embeds them; a revision that changes one must raise its SOVERSION, or the images read
the new library by the old layout and nothing says so. That discipline is not yet established — the
SOVERSION has stood at 1 while the layouts moved — and until it is, binding the library in is the
honest choice: a mismatch is then caught by the linker. `BATCH_M_STATICLIB=OFF` asks for the shared
one.

The build requires two packages installed in the system: **StarLet** (UTILITY$ROUTINES) and
**HELP**. Neither is carried in the tree, and neither is optional — without the first nothing
compiles and without the second the `HELP` command does not.

The `HELP` command is built when the HELP facility is found. Its library exists in three languages:
English in `share/help`, Russian in `share/help/ru`, French in `share/help/fr`, translated out of
`doc/batch_help.md`, `doc/batch_help_rus.md` and `doc/batch_help_fra.md`. The HELP facility picks
the one the locale asks for. Its library is translated out of
`doc/batch_help.md` by VHELPTRAN, kept beside the sources and installed into `share/help`; the
command answers through the reading API of that facility rather than by spawning its image.

```sh
$ batch help submit /after
```

Installing the library also rebuilds the catalogue the HELP utility shares between its users, so
`help submit` asked of the general utility finds the topic too. A staged installation leaves that
to the package; `vhelp /SYSCACHE` builds it.

| `BATCH_K_SYSTEMDDIR` | `/lib/systemd/system` | where that unit is installed |
| `BATCH_K_INITDIR` | `/etc/init.d` | where the System V start script is installed |

```sh
cmake -S . -B build -DBATCH\$K_SPOOLROOT=/opt/batch -DBATCH\$M_STRICT=ON
```

The backslash is needed only to keep the shell from expanding the dollar sign; CMake itself
permits a dollar sign in a variable name.

## Quick start

```sh
$ batchd /ROOT=/var/spool/batch /JOB_LIMIT=8

$ batch submit /opt/jobs/nightly.sh /NAME=nightly '/PARAMETERS=(full,/srv/data)'
Job nightly (queue SYS$BATCH, entry 17) queued

$ batch show

 Entry  Job name             State      Submitted            Elapsed   Status
 -----  -------------------- ---------- -------------------- --------- ----------
    17  nightly              Executing  28-JUL-2026 10:01:05 00:04:12  -
    18  reindex              Retained   28-JUL-2026 10:01:05 00:00:07  exit=7

Total entries: 2

$ batch show /all          # including the successful jobs kept by /RETAIN=ALWAYS
$ batch show /full         # a full description of each job
$ batch delete 17          # abort a job or delete a queue entry
$ batch release 19         # release a job from the holding state
$ batch set entry 19 /PRIORITY=200 /AFTER=TOMORROW   # alter a queued job without resubmitting it
```

A job receives its parameters both as the environment variables `P1` through `P8` and as the
positional arguments `$1` through `$8`, together with `BATCH_ENTRY`, `BATCH_NAME` and
`BATCH_QUEUE`. It starts in the working directory that was current when it was submitted.

## Design notes

**Time is kept in UTC.** Every timestamp displayed, journalled or accepted by `/AFTER` is UTC, so
the queue is unaffected by a change of local time or by a daylight-saving transition. Conversion is
performed by the STARLET services `UTIL$BINTIM`, `UTIL$ASCTIM`, `UTIL$GETTIM` and `UTIL$ADD_TIMES`
from the StarLet package.

**Connections are served without blocking.** The listening socket, the self-pipe that carries
signals and every open connection are awaited together, and a connection is advanced one step at a
time as its descriptor becomes ready. A client that connects and then says nothing costs a
descriptor and nothing else; it is dropped once it has made no progress for thirty seconds. A reply
is assembled in memory before any of it is transmitted, so the job queue is never held open across a
transfer to a slow reader.

**Sender identification** is obtained from the kernel through `SO_PEERCRED`, not from the contents
of the request. The owner fields, the entry number and the timestamps are filled in by the job
controller itself, so a client cannot forge them.

**The queue journal** is written to a temporary file and moved into place by `rename(2)`, which
makes the write atomic and lets the queue survive a restart of the daemon. Jobs that were
executing when the daemon stopped are marked as orphaned: their processes are not children of the
new instance.

**Scheduling order** is by the highest priority and, among equal priorities, by the lowest entry
number, so that jobs of equal priority are served in the order they were submitted.

**Command line parsing.** Matching, abbreviation, ambiguity and validation of a value against its
declared type are performed by the CLI services of the StarLet package. Two things they do not
do are done here. First, in DCL a qualifier begins with a slash, but so does an absolute Unix path,
and the services make no such distinction: an argument is taken for a qualifier only if the text up
to the equals sign carries no slash and names one of the qualifiers. Second, those services take a
declared parameter from the head of the vector unconditionally, whereas a parameter and a qualifier
may appear here in any order, so the vector is rebuilt with the parameter ahead of the qualifiers
before it is handed over.

**Condition values.** Every routine returns a condition value in the OpenVMS format — facility
number, message number, severity and control bits — rather than an indication of success. The
layout, the severity codes and the composition macros come from the installed `utility_routines.h`;
the set of conditions is in `batchmsg.h` and the mnemonics and default texts in `batchmsg.c`. A condition produced by the job controller crosses the process
boundary in the `mh_sts` field of the reply and is issued by the utility as its own:

```
28-JUL-2026 10:58:15 %BATCH-E-NOSCRIPT, script /nonexistent.sh not found - No such file or directory
28-JUL-2026 10:58:15 %BATCH-E-NOSUCHENT, no such queue entry
```

## Coding conventions

The project is written in the OpenVMS style:

* Whitesmiths formatting, tabs only, tab width 8, text width up to 132;
* every module header carries `MODULE`, `IDENT`, `REV`, `FACILITY`, `ABSTRACT`, `AUTHOR`,
  `CREATION DATE` and a modification history;
* prefixes `g_`, `l_`, `s_` and `a_` for global, local, static and routine parameters;
* constants and macros carry the dollar separator: `BATCH_K_MAXJOB`, `STS$K_ERROR`, `$PUTMSG_FAO`,
  `$IS_SUCCESS`;
* structure declaration: the tag carries the suffix `_t` and the type name the dollar separator,
  for instance `typedef struct batch_job_t { ... } BATCH$JOB`;
* variable declarations at the head of a routine, without indentation, grouped by type;
* a routine returns a condition value `BATCH$_*` and delivers computed values through output
  parameters; the exceptions — `util$log`, the signal handler and `main` — are noted in comments;
* all diagnostics are issued through `$PUTMSG_FAO` with an explicit condition value;
* all formatting goes through the FAO engine of the `starlet` module of that package — `util$fao` and
  `util$faol` — and not through the formatted output of the run-time library. Neither image calls
  `printf` in any of its forms, and neither includes `<stdio.h>`.

The one departure concerns the build: `@NAME@` substitution in `configure_file` does not recognize
the dollar separator within a CMake variable name, so the template `batchvers.h.in` is fed by the
auxiliary variables `BATCH_CFG_*`. The macros generated from them retain the separator.

## Limitations of this revision

* one queue; the job descriptor carries a queue name field, but the limit is common to the whole
  subsystem. The queue itself is that of the StarLet package, so an entry costs what it needs
  and `BATCH_K_MAXJOB` is only a ceiling against a runaway submitter;
* the log of an individual job is not rewound while the job runs; it is removed with the queue entry
  it belongs to, and what is left behind is swept from the log directory after `/LOG_RETAIN` days.
  The log of the controller itself is rewound once it exceeds a megabyte;
* the nice value, the resource limits and the control group membership are inherited from the job
  controller and cannot be set per job. Processor affinity can: `/CPU=0-3,5` binds a job, and the
  mask the controller itself runs under is stated in its log when it starts.

The command line of the job controller is parsed by the CLI services of the StarLet package:
a qualifier may be introduced by a slash or a hyphen, may be abbreviated to any unambiguous leading
portion, and takes its value through an equals sign — `/JOB_LIMIT=8` or `-j=8`.
