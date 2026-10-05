# Contributing

Changes are developed on branches and submitted through pull requests. Keep
the observer/userspace path safe by default and preserve the five canonical
actions: RUN, SLEEP, MIGRATE, THROTTLE, and YIELD.

Every change must include tests or a documented reason that a test is not
applicable. Do not claim kernel ownership, performance improvement, real-time
guarantees, cryptographic kernel authentication, or deployment readiness without
the evidence described in the [claim–evidence guide](docs/paper/EVIDENCE.md).

Run the local gate before opening a pull request:

    make clean
    make
    make check
    make test

Do not include machine logs, credentials, generated BPF objects, private
artifacts, or local absolute paths in a pull request.
