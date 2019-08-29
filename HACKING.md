# Attaching a debugger to Tracker daemons

Tracker daemons are not started directly. Instead they are started by the D-Bus
daemon by request. When using tracker-sandbox or the functional-tests, it's
difficult to start the daemon manually under `gdb`.

Instead, we recommend adding a 10 second timeout at the top of the daemon's
main() function. In Vala code, try this:

    print("Pausing to attach debugger. Run: gdb attach %i\n", Posix.getpid());
    Posix.usleep(10 * 1000 * 1000);
    print("Waking up again\n");

Run the test, using the `meson build --timeout-multiplier=10000`
option to avoid your process being killed by the test runner. When you see
the 'Pausing' message, run the `gdb attach``command in another terminal within
10 seconds.
