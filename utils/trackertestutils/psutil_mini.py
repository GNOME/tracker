# Copyright (c) 2009, Giampaolo Rodola'. All rights reserved.
#
# Use of this source code is governed by a BSD-style license that can be
# found at https://github.com/giampaolo/psutil/blob/master/LICENSE
#
# Taken from https://github.com/giampaolo/psutil/blob/master/psutil/_psposix.py
# by Sam Thursfield to avoid adding a dependency between the Tracker testsuite
# and the 'psutil' module.


import os
import time


class TimeoutExpired(Exception):
    pass


def pid_exists(pid):
    """Check whether pid exists in the current process table."""
    if pid == 0:
        # According to "man 2 kill" PID 0 has a special meaning:
        # it refers to <<every process in the process group of the
        # calling process>> so we don't want to go any further.
        # If we get here it means this UNIX platform *does* have
        # a process with id 0.
        return True
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        # EPERM clearly means there's a process to deny access to
        return True
    # According to "man 2 kill" possible error values are
    # (EINVAL, EPERM, ESRCH)
    else:
        return True


def wait_pid(pid, timeout=None, proc_name=None):
    """Wait for process with pid 'pid' to terminate and return its
    exit status code as an integer.
    If pid is not a children of os.getpid() (current process) just
    waits until the process disappears and return None.
    If pid does not exist at all return None immediately.
    Raise TimeoutExpired on timeout expired.
    """
    def check_timeout(delay):
        if timeout is not None:
            if timer() >= stop_at:
                raise TimeoutExpired(timeout, pid=pid, name=proc_name)
        time.sleep(delay)
        return min(delay * 2, 0.04)

    timer = getattr(time, 'monotonic', time.time)
    if timeout is not None:
        def waitcall():
            return os.waitpid(pid, os.WNOHANG)
        stop_at = timer() + timeout
    else:
        def waitcall():
            return os.waitpid(pid, 0)

    delay = 0.0001
    while True:
        try:
            retpid, status = waitcall()
        except InterruptedError:
            delay = check_timeout(delay)
        except ChildProcessError:
            # This has two meanings:
            # - pid is not a child of os.getpid() in which case
            #   we keep polling until it's gone
            # - pid never existed in the first place
            # In both cases we'll eventually return None as we
            # can't determine its exit status code.
            while True:
                if pid_exists(pid):
                    delay = check_timeout(delay)
                else:
                    return
        else:
            if retpid == 0:
                # WNOHANG was used, pid is still running
                delay = check_timeout(delay)
                continue
            # process exited due to a signal; return the integer of
            # that signal
            if os.WIFSIGNALED(status):
                return -os.WTERMSIG(status)
            # process exited using exit(2) system call; return the
            # integer exit(2) system call has been called with
            elif os.WIFEXITED(status):
                return os.WEXITSTATUS(status)
            else:
                # should never happen
                raise ValueError("unknown process exit status %r" % status)
