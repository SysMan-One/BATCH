<!-- ident: BATCH -->

# BATCH -- submit and manage batch jobs

Executes a shell script as a batch job: the script is placed in a
queue and run later, without a terminal, under a resident job
controller.

Format:

```
BATCH command [parameter] [/qualifiers]
```

A command may be abbreviated to any unambiguous leading portion of its
name, as a qualifier may: SUBM is SUBMIT and DEL is DELETE, while S is
refused, SHOW and SUBMIT both beginning with it.

Submitting a script is not running it. The submission assigns a queue
entry number and returns; the process is created later, if and when
the controller decides. The entry, not the process, is what you hold
on to.

A job that fails stays in the queue with its log, so that it can be
looked at. A job that succeeds leaves the queue. That is what makes
SHOW without qualifiers useful: what remains is what is running and
what went wrong.

The subsystem root directory is taken from the environment variable
BATCH_ROOT, and from the value the software was built with when that
is not set.

# SUBMIT -- queue a script for execution

Queues a shell script for execution as a batch job.

Format:

```
BATCH SUBMIT script-specification [/qualifiers]
```

The script specification is resolved to an absolute path when the
command is entered. The file must exist and be readable both by you
and by the job controller.

The job starts in the working directory that was current when the
command was entered, not in the one current when it comes to be run.

A qualifier is written /NAME or --NAME; a lone hyphen carries no such
meaning, so -NAME is an ordinary value.

Because a file specification and a qualifier both begin with a slash,
an argument is taken for a qualifier only when the text up to the
equals sign carries no slash of its own: /tmp/job.sh is a path and
/NAME=x is a qualifier.

## /AFTER

Specifies the time before which the job is not to start. The job is
held until then and released without anyone's attention.

```
/AFTER=+1-02:00           a day and two hours from now
/AFTER=29-JUL-2026:03:00  an absolute time, by your own clock
/AFTER=TOMORROW           the midnight the next day begins with
```

The delta form is +[days-]hh[:mm[:ss]] and the omitted fields are
zero. The day field is told from the hour field by the hyphen that
follows it, and by nothing else:

```
+7          seven hours
+7-         seven days
+7-12:00    seven days and twelve hours
+:30        thirty minutes
+::45       forty-five seconds
```

Since a mistake here is not apparent until the span has passed, the
submission names the time the job is to start:

```
$ batch submit nightly.sh /AFTER=+7-
%BATCH-S-QUEUEDUNTIL, job nightly queued on SYS$BATCH as entry 4, holding until  8-AUG-2026 20:03:37
```

## /CPU

Binds the job to the processors named, in the notation taskset(1)
uses: a comma-separated series of numbers and of ranges.

```
/CPU=0-3,5,8-11
```

Without the qualifier the job inherits the affinity mask of the job
controller, which the controller states in its log when it starts.
The list is verified when the command is entered.

## /HOLD

Places the job in the Holding state. It is not considered for
execution until it is released by the RELEASE command. Use it when
the time to start is not yet known.

## /KEEP

Keeps the log of the job after it has completed successfully. By
default the log of a successful job is removed along with its queue
entry.

## /LOG_FILE

Names the file the standard output and the standard error of the job
are directed to. The specification is resolved when the command is
entered; the file need not exist, its directory must.

By default the log is created in the directory the job was submitted
from, as it is in OpenVMS, and is named after the job and its entry
number. Where that directory cannot be written in by the submitter,
the log subdirectory of the subsystem root is used instead.

## /NAME

Names the job, up to 63 characters. By default the name of the job is
the name of the script file without the .sh suffix. The name appears
in the displays and forms part of the default log name.

## /PARAMETERS

Passes up to eight values to the job.

```
/PARAMETERS=(full,/srv/data,verbose)
```

Each value reaches the script twice: as the environment variables P1
through P8 and as the positional arguments $1 through $8. The values
are handed to the interpreter as separate elements of the argument
vector and are never joined into a command line, so a value holding
characters the shell would act upon cannot cause anything to be run.

The parentheses are read by the shell that invokes BATCH; quote the
whole qualifier, or leave them out.

## /PRIORITY

Sets the priority of the job within the queue, from 1 to 255,
100 by default. The controller picks the pending job of the highest
priority; jobs of equal priority are picked in the order of their
entry numbers.

This is the priority of the job in the queue. It has nothing to do
with the scheduling priority of the process, and means nothing once
the job is running.

## /QUEUE

Names the queue the job is placed in. It must be one the controller
was told of: a job placed in a queue nobody minds would wait for ever
and would look exactly like a job waiting its turn.

Queues have limits of their own. Jobs of one queue run in the order
their priorities and entry numbers give; jobs of different queues run
beside one another, each queue up to its own limit. The controller is
given the table as `/QUEUE=(SYS$BATCH,slow:1,fast:3)`, the first name
being the queue a job goes to when it names none. By default the queue the
controller was started with, ordinarily SYS$BATCH.

## /RESTART

Specifies that the job is to be run again if the job controller is
stopped while the job is executing. Such a job goes back into the
queue when the controller is started next; without the qualifier it
is kept as orphaned and is not run again.

The process left behind by the earlier instance is neither sought out
nor stopped: it belongs to a session of its own and may well have
finished. What the qualifier promises is that the job will be run,
not that it will be run once, so a restartable job should be written
such that running it twice does no harm.

The job is told of a restart through BATCH_RESTART, which reads TRUE
or FALSE, and BATCH_RESTARTS, which counts them.

## /USER

Runs the job as the user named rather than as the submitter.

```
/USER=sysman
```

Only the privileged user may ask for this. The identity is settled by
the job controller from what the kernel reports about the sender of
the request, not from anything the request says: the utility runs as
whoever invoked it and could claim to be anybody.

Every use of this qualifier is recorded in the log of the job
controller, naming who asked and for whom, and so is every refusal:

```
%BATCH-S-RUNASUSER, job 1 submitted by uid 0 to run as sysman (uid 1000, gid 1000)
%BATCH-E-NOPRIVUSER, uid 1000 may not submit a job to run as root
```

The job starts in the home directory of that user rather than in the
directory it was submitted from, which he may well not be admitted
to; where the account names no directory that exists -- an account
kept for a service often does not -- the root directory serves. The
log goes to the log subdirectory of the subsystem, whether the
submitter could write beside the submission or not: the answer for
one user says nothing about another.

## /RETAIN

Sets the condition under which the queue entry survives the job.

```
/RETAIN=ALWAYS      the entry is always kept
/RETAIN=ERROR       kept only when the job failed; the default
/RETAIN=NEVER       never kept, even when the job failed
```

# SHOW -- display the queue

Displays the entries of the batch queue.

Format:

```
BATCH SHOW [entry-number] [/qualifiers]
```

An entry number displays that entry alone, whatever state it holds --
so SHOW of an entry does not depend upon /ALL as SHOW of the queue
does. An entry that does not exist is refused rather than displayed
as an empty queue. The number may equally be given with /ENTRY.

Without /ALL the jobs displayed are those executing, aborting,
waiting (Pending and Holding) and retained after a failure -- the
queue as it stands to be worked. The entry of a successful job
leaves the queue; one kept by /RETAIN=ALWAYS is displayed only
with /ALL.

The Elapsed column is the time from the start of the job to its end,
or to the present moment while it is still running.

The last column says what became of the job -- exit=n for one that
returned that status, signal=n for one that was signalled -- and for
a job held until a time it says that time instead, a job which has
not run having no status to report. So finding out when a held job
starts does not call for /FULL.

## /ALL

Adds the entries kept after a successful job (/RETAIN=ALWAYS) to
the display.

## /EVERYONE

Displays the jobs of every user. By default only your own.

The privileged user is not restricted to his own and needs no
qualifier for it: he may delete and release any entry, and an entry
he may dispose of but cannot see would be worse than useless. It
matters for /USER above -- a job submitted to run as somebody else
belongs to that somebody at once, submitter or no.

## /FULL

Displays the whole of each entry: the owner, the priority, the
processors, the script, the working directory, the log, the
timestamps, the exit status and the parameters.

## /MANAGER

Displays the state of the job controller rather than the contents of
the queue: the image it runs, its process and how long it has been
up, the root it serves, the job limit and what is executing against
it, the counts by state, the next entry number, and the pool of
processors it was given.

A queue that stands still looks the same whether the controller is
busy, idle or gone, and that is the first thing worth knowing when
nothing is happening.

## /QUEUES

Displays the queues rather than the jobs: each with its limit, what
it has executing, pending and holding, and whether it is paused.

```
 Queue                Limit  Exec  Pend  Hold  State
 -------------------- ----- ----- ----- ----- --------
 SYS$BATCH               16     0     0     0  open
 slow                     1     1     1     0  paused
```

## /PAGE

Holds the display at the foot of each screen and waits for a key,
for a queue longer than the terminal.

The qualifier is a courtesy to a reader at a terminal and a nuisance
to a program reading a pipe, so it is granted only where there is a
terminal to hold at: redirected, it is quietly disregarded and the
output is exactly what it would have been without it.

## /QUEUE

Restricts the display to the queue named.

# DELETE -- remove an entry from the queue

Removes a queue entry, terminating the job if it is running.

Format:

```
BATCH DELETE entry-list
```

The list is written as DCL writes one: numbers and ranges separated
by commas, the parentheses optional -- `1,3,4` and `(1,3-5)` alike.
Every element is applied whatever became of those before it, and the
command exits with a failure status if any of them failed.

If the job is executing, the signal is sent to its whole process
group and the job passes into the Aborting state; the entry is
disposed of when the process terminates and the log is kept. In every
other state the entry is removed at once and its log with it, unless
/KEEP was given at submission.

Only your own jobs may be deleted; the privileged user may delete
any.

The entry number may also be given with the /ENTRY qualifier, which
takes precedence over the positional one.

# RELEASE -- release a held job

Releases a job from the Holding state.

Format:

```
BATCH RELEASE entry-list
```

A list is taken here as it is by DELETE.

The job passes into the Pending state and becomes ready to run. Any
/AFTER time it carried is cleared, so a released job is not withheld
a second time.

The command is refused if the job is not in the Holding state.

# SET -- alter the attributes of a queue entry

Alters the attributes of a job that is in the queue, in place. The
entry keeps its number and its place in the order; nothing is deleted
and nothing is submitted anew.

Format:

```
BATCH SET ENTRY entry-list [/qualifiers]
```

## ENTRY

Alters the attributes of the entries named. A list is taken as it is
by DELETE. Only the attributes a qualifier names are touched; the rest
stay as they were.

```
/PRIORITY=n         the priority within the queue, 1 to 255
/AFTER=time         do not start before the time; /NOAFTER clears it
/HOLD               withhold the job; /NOHOLD lets a held one go
/NAME=name          the name of the job
/RETAIN=keyword     ALWAYS, ERROR or NEVER, as for SUBMIT
/KEEP, /NOKEEP      whether the log of a successful job is kept
/RESTART, /NORESTART  whether the job is run again after a restart
/CPU=list           the processors the job is bound to
```

What may be altered depends on the state of the job. A job that waits,
Pending or Holding, may have any of them altered. A job that executes
may have only /RETAIN, /KEEP and /RESTART altered: the rest describe a
start that has already happened. A retained job may have nothing
altered, since nothing about it is still to be decided. A request that
names an attribute the state forbids is refused whole.

/HOLD and /AFTER work together as they do at submission. A held job that
carries a time is let go when the time comes, or when /NOAFTER clears
it; a job held by /HOLD alone waits for RELEASE or /NOHOLD. /HOLD and
/NOAFTER together therefore mean: held, without a time.

```
$ batch set entry 17 /PRIORITY=200
%BATCH-S-ENTMODIFIED, entry 17 modified: priority=200
$ batch set entry 17 /AFTER=TOMORROW /NAME=nightly2
%BATCH-S-ENTMODIFIED, entry 17 modified: after= 9-SEP-2026 00:00:00, name=nightly2
```

The name given to a job changes what the displays show and nothing
else: the log file was named when the job was submitted and keeps its
name.

# ANALYZE -- examine the queue journal

Examines the queue journal and reports what is wrong with it.

Format:

```
BATCH ANALYZE [journal-specification] [/DUMP]
```

Without a specification the journal of this subsystem is examined.

The file is read directly rather than through the job controller, so
the command serves where the controller will not start -- which is
when it is wanted most -- and serves equally on a copy of a journal
taken from another machine.

Nothing is written. A journal found to be damaged is left as it
stands: what to do about it is a decision for whoever ran the
command, and a repair made without being asked for would be the
worse surprise.

A tag the record carries and this revision does not assign is named,
with its value in hexadecimal:

```
  entry 1: tag 4000, type 0, 6 octets not known to this revision: DE AD BE EF CA FE
```

A record is shown thus, every label followed by a colon:

```
  Entry: 1, job: weekly, 364 octets
    State: Retained, owner: [0:0], priority: 100
    Queued: 21-AUG-2026 16:42:45, queue: SYS$BATCH
    Script: /root/Works/weekly.sh
    Ended: 21-AUG-2026 16:42:51, exit: 7
```

That is not counted a fault -- passing over what one does not know is
what lets a journal cross a revision of the software -- but it is
worth saying, since a journal written by a later revision and one
quietly corrupted look alike until the tags are named.

The header is reported first, then a line for each fault found: a
record that will not decode, an entry number absent, repeated or
above the counter that issues them, a state that names no state, a
script or a working directory that is not an absolute path. The
command exits with a failure status if anything was found.

## /DUMP

Displays each record as well: its entry, name, size, state, owner,
priority, the queue, the time it was queued and the script it runs.
A job that has ended is shown with how it ended, and one that is
held with the time it waits for.

# SYNC -- wait for a job to end

Waits for a job to end and reports how it ended.

Format:

```
BATCH SYNC entry-number [/TIMEOUT=seconds]
```

It is meant for a script that has work to do after a job and no
other way of knowing when the job is done. The exit status carries
the answer, so the caller need not read the display at all:

```
0    the job succeeded
1    the job failed, or the wait ran out, or there is no such entry
```

```sh
batch submit nightly.sh /NAME=nightly
batch sync 17 /TIMEOUT=3600 || echo "the nightly job did not go well"
```

An entry that has left the queue is taken to have succeeded, that
being what makes an entry leave: a job that fails is retained. An
entry number never issued is told from one that has been and gone by
the counter that issues them, and is refused.

## /TIMEOUT

Gives up after the number of seconds named, rather than waiting for
as long as it takes. Without it the wait has no limit.

# QUEUE -- pause or resume a queue

Withholds the work of a queue, or lets it go on again.

Format:

```
BATCH QUEUE queue-name /PAUSE
BATCH QUEUE queue-name /RESUME
```

A paused queue starts nothing. Its entries are not disturbed and its
executing jobs are left to finish: pausing is what one does to a
queue whose jobs must wait without their entries being touched.

Only the privileged user may ask for it: pausing withholds the work
of everybody who uses the queue, and ownership of a job says nothing
about the queue it waits in.

# PURGE -- remove the entries of finished jobs

Removes the entries left behind by jobs that have finished.

Format:

```
BATCH PURGE [/QUEUE=queue] [/CONFIRM]
```

A queue that keeps its failures needs tidying: an entry retained
after a job failed stays until somebody has looked at it -- and the
looking is done, but the entries remain.

Only a job that has finished is taken. A job still executing is not
touched: PURGE tidies what is over, it does not stop what is not.

Your own entries, unless you are the privileged user, who may
dispose of any.

## /QUEUE

Restricts the purge to the queue named.

## /CONFIRM

Asks before it goes ahead. Anything but a word beginning with Y is a
refusal: what PURGE does cannot be undone, and silence is not
consent. Redirected input is taken as confirmed -- a question put
into a file would wait for a key nobody is at hand to press --
and /NOCONFIRM says the same explicitly.

# HELP -- display this information

Displays information about a command of this subsystem.

Format:

```
BATCH HELP [topic [subtopic ...]]
```

Without a topic the whole of this text is displayed. A subtopic is
named after the topic it belongs to:

```
$ batch help submit
$ batch help submit /after
$ batch help show /full
```

# STATES -- the states a job passes through

A job occupies exactly one of the following states.

```
Pending     may be run and is waiting for an execution slot
Holding     withheld by /HOLD, or by an /AFTER time not yet reached
Executing   a process has been created for it and is running
Aborting    a termination signal has been sent and it has not ended
Retained    it has ended and its entry was kept for inspection
```

A job that ends with a zero exit status and without /RETAIN=ALWAYS
leaves the queue; anything else is retained.

# FILES -- what the subsystem keeps and where

```
<root>/batch.sock     the socket the controller accepts requests on
<root>/journal.dat    the queue journal
<root>/batchd.log     the log of the controller itself
<root>/log/           the logs of jobs
```

The root is /var/spool/batch unless the software was built otherwise;
the utility takes it from the environment variable BATCH_ROOT.

The queue journal is written whole whenever the queue changes and is
renamed over the previous copy, so it is never seen half written. It
holds the binary image of every entry: a journal written by another
revision of the software is refused and the queue starts empty.
