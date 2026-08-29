#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: packaging/package-smoke.sh PACKAGE FORMAT DISTRO" >&2
    exit 2
fi
PACKAGE=$1
FORMAT=$2
DISTRO=$3
[ -f "$PACKAGE" ] || { echo "package not found: $PACKAGE" >&2; exit 1; }

before_state=unavailable
if [ -r /sys/kernel/sched_ext/state ]; then
    before_state=$(tr -d '\n' < /sys/kernel/sched_ext/state)
fi
operator_file=/etc/orchestra-os/release-smoke-operator.conf
mkdir -p /etc/orchestra-os
printf 'operator-owned=true\n' > "$operator_file"

case "$FORMAT" in
    deb)
        dpkg -i "$PACKAGE" >/dev/null
        command=/usr/bin/orchestra
        "$command" version | grep -q 'ORCHESTRA-OS 1.0.0'
        test -f /usr/lib/orchestra-os/.package-managed
        if "$command" install >/tmp/orchestra-package-install.out 2>/tmp/orchestra-package-install.err; then
            echo "package-managed install unexpectedly succeeded" >&2
            exit 1
        fi
        grep -q package-managed /tmp/orchestra-package-install.err
        dpkg -r orchestra-os >/dev/null
        test -f "$operator_file"
        dpkg -i "$PACKAGE" >/dev/null
        dpkg --purge orchestra-os >/dev/null
        ;;
    rpm)
        rpm -Uvh --replacepkgs --nodeps "$PACKAGE" >/dev/null
        command=/usr/bin/orchestra
        "$command" version | grep -q 'ORCHESTRA-OS 1.0.0'
        test -f /usr/lib/orchestra-os/.package-managed
        if "$command" uninstall >/tmp/orchestra-package-uninstall.out 2>/tmp/orchestra-package-uninstall.err; then
            echo "package-managed uninstall unexpectedly succeeded" >&2
            exit 1
        fi
        grep -q package-managed /tmp/orchestra-package-uninstall.err
        rpm -e orchestra-os >/dev/null
        test -f "$operator_file"
        rpm -Uvh --replacepkgs --nodeps "$PACKAGE" >/dev/null
        rpm -e orchestra-os >/dev/null
        ;;
    apk)
        apk add --allow-untrusted "$PACKAGE" >/dev/null
        command=/usr/bin/orchestra
        "$command" version | grep -q 'ORCHESTRA-OS 1.0.0'
        test -f /usr/lib/orchestra-os/.package-managed
        if "$command" install >/tmp/orchestra-package-install.out 2>/tmp/orchestra-package-install.err; then
            echo "package-managed install unexpectedly succeeded" >&2
            exit 1
        fi
        grep -q package-managed /tmp/orchestra-package-install.err
        apk del orchestra-os >/dev/null
        test -f "$operator_file"
        apk add --allow-untrusted "$PACKAGE" >/dev/null
        apk del orchestra-os >/dev/null
        ;;
    *)
        echo "unsupported format: $FORMAT" >&2
        exit 2
        ;;
esac

after_state=unavailable
if [ -r /sys/kernel/sched_ext/state ]; then
    after_state=$(tr -d '\n' < /sys/kernel/sched_ext/state)
fi
[ "$before_state" = "$after_state" ] || {
    echo "package lifecycle changed sched_ext state: before=$before_state after=$after_state" >&2
    exit 1
}
echo "PACKAGE_SMOKE_PASS format=$FORMAT distro=$DISTRO state=$after_state"
