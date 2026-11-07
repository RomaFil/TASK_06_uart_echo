# TASK_06: UART echo in C

> English summary. The full documentation of this repository is in Ukrainian: [README.md](README.md).

A UART echo application in C (termios, signals, timestamped log) cross-compiled on WSL2 for a Raspberry Pi 5, installed with one "make install" and supervised by a systemd unit. Everything was verified on the real board, and the pitfalls are documented:

- a dynamically linked binary built against a newer glibc fails on the target (needs GLIBC_2.42, the Pi has 2.41) — fixed with a static build, with exact versions in the README;
- on a Pi 5 /dev/serial0 points to the debug UART, not to pins 8/10, so the device is passed explicitly;
- the unit encodes real requirements: Requires/After on the device node, Restart=on-failure with StartLimitBurst against restart loops, SIGTERM for a clean stop, line-buffered stdout;
- proof, not claims: a loopback test with the wire removed to show the test is not lying, a virtual port pair to test the service on the host, and full binutils analysis of native and cross builds.

Stack: C, termios, GNU Make, aarch64 cross toolchain, systemd, Raspberry Pi, WSL2.
