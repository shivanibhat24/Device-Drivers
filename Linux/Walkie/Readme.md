# walkie

A Linux kernel character device that lets exactly two processes talk to each
other over a shared in-kernel ring buffer — like a walkie-talkie: one speaks,
the other listens, and vice versa.

---

## How it works

When the module is loaded, it creates `/dev/walkie`.  The **first** process to
open the device becomes **side 0**; the **second** becomes **side 1**.  Any
further `open()` calls are rejected with `EBUSY`.

There are two independent ring buffers, each 4 KB:

```
side-0  ──write──▶  pipe[0]  ──read──▶  side-1
side-1  ──write──▶  pipe[1]  ──read──▶  side-0
```

Each side reads only what the other side has written, so there is no
echo and no self-read.  Reads block until data arrives (or the peer closes);
writes block when the target buffer is full.  Both can be made non-blocking
with `O_NONBLOCK`.  `poll()`/`select()` are fully supported.

---

## Requirements

| Requirement | Notes |
|---|---|
| Linux kernel headers | Must match your running kernel |
| GCC + GNU Make | Standard build tools |
| Root / `sudo` | Required to load/unload the module |

Install headers on Debian/Ubuntu:

```bash
sudo apt install linux-headers-$(uname -r) build-essential
```

---

## Building

```bash
# Build the kernel module and the test binary
make
make test_walkie
```

The build produces `walkie.ko` and the `test_walkie` binary.

---

## Loading and unloading

```bash
# Load
sudo insmod walkie.ko

# Confirm it appeared
ls -l /dev/walkie
dmesg | tail -3

# Unload
sudo rmmod walkie
```

Alternatively, the Makefile provides shortcuts:

```bash
make load      # insmod + show device
make unload    # rmmod
make reload    # unload then load
```

---

## Quick demo

Open two terminals after loading the module.

**Terminal A (side 0):**
```bash
cat /dev/walkie
```

**Terminal B (side 1):**
```bash
echo "hello from side 1" > /dev/walkie
```

Terminal A prints the message.  Reverse the roles to send the other way.

---

## Running the test suite

```bash
# Build module + test binary, load module if needed, run all tests
make test

# Or manually
sudo insmod walkie.ko
sudo ./test_walkie
```

Expected output:

```
=== walkie device test suite ===

  [PASS] T1 basic write/read
  [PASS] T2 cross-side isolation
  [PASS] T3 third open rejected
  [PASS] T4 non-blocking read empty
  [PASS] T5 non-blocking write full
  [PASS] T6 EOF after peer close
  [PASS] T7 poll EPOLLIN
  [PASS] T8 large message
  [PASS] T9 bidirectional
  [PASS] T10 reopen after close

=== Results: 10 passed, 0 failed ===
```

---

## Test descriptions

| # | Name | What it checks |
|---|------|----------------|
| T1 | basic write/read | Data written by side 0 is received intact by side 1 |
| T2 | cross-side isolation | A side cannot read its own writes |
| T3 | third open rejected | `open()` by a third process returns `EBUSY` |
| T4 | non-blocking read empty | `O_NONBLOCK` read on empty buffer returns `EAGAIN` |
| T5 | non-blocking write full | `O_NONBLOCK` write on a full buffer returns `EAGAIN` |
| T6 | EOF after peer close | Read returns 0 after the peer closes |
| T7 | poll EPOLLIN | `poll()` reports readable when data is present |
| T8 | large message | A 2 KB payload survives the ring buffer unchanged |
| T9 | bidirectional | Fork-based ping/pong in both directions |
| T10 | reopen after close | Device works correctly after a full close/reopen cycle |

---

## Manual testing ideas

Beyond the automated suite, these are useful to try by hand:

**Blocking behaviour** — open both sides with `cat` and `echo`, then remove
one side with Ctrl-C and observe the other side getting EOF.

**Non-blocking mode** — use `python3` or a small C snippet to open with
`O_NONBLOCK` and verify `EAGAIN` returns immediately on an empty pipe.

**`dmesg` logging** — every open, close, and module load/unload writes an
`info`-level kernel log entry, visible with `dmesg -w`.

**`strace` tracing** — run `strace -e read,write,poll ./test_walkie` to
observe every syscall the test suite makes.

---

## Device semantics reference

| Scenario | Behaviour |
|---|---|
| Read with data available | Returns immediately with up to `BUF_SIZE` bytes |
| Read with no data, peer open | Blocks (or `EAGAIN` with `O_NONBLOCK`) |
| Read with no data, peer closed | Returns 0 (EOF) |
| Write with buffer not full | Copies data and wakes the peer's reader |
| Write with buffer full | Blocks (or `EAGAIN` with `O_NONBLOCK`) |
| Third `open()` | Returns `-EBUSY` |
| `poll(POLLIN)` | Ready when peer's pipe has data |
| `poll(POLLOUT)` | Ready when own pipe has space |
| `poll(POLLHUP)` | Set when peer has closed |

---

## Kernel log messages

```
walkie: loaded – /dev/walkie is major <N>
walkie: process <PID> opened as side <0|1>
walkie: side <0|1> (pid <PID>) closed
walkie: device already opened by two processes   ← on a third open attempt
walkie: unloaded
```

Monitor in real time with `dmesg -w` or `journalctl -kf`.

---

## Cleaning up

```bash
make clean     # remove build artefacts and test binary
make unload    # remove the module from the running kernel
```
