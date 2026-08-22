# ORCHESTRA-OS usage

The `orchestra` command is the normal control plane. Run it from the source
tree as `./scripts/orchestra`, or use `/usr/local/bin/orchestra` after
installation.

## Status and capability tiers

```bash
orchestra version
orchestra check-system
orchestra status
```

`status` reports product/ABI versions, build paths, sched_ext state,
capability checks, and bridge status when authorized. A warning or an
unavailable kernel does not prevent observer-mode use.

## Build and validate

```bash
orchestra build --userspace --bridge
orchestra validate
```

`validate` runs the non-privileged repository checks. It explicitly reports
that privileged attach and ownership are separate gates.

## Enable and disable

```bash
sudo orchestra enable
sudo orchestra status
sudo orchestra disable
```

Enable requires the strict host check, root-safe target-matched artifacts,
and a build-manifest hash match. Disable is idempotent when sched_ext is
already disabled, refuses a foreign sched_ext owner, and verifies the
kernel's disabled state after unloading.

For a scx-style foreground lifecycle, use:

```bash
sudo orchestra run --interval 5
```

The command attaches only when sched_ext is disabled, prints the active ops
name and bridge telemetry, and detaches only the instance it attached when
interrupted. If ORCHESTRA is already the owner, it observes that instance and
does not detach someone else's lifecycle. To observe an already-running
ORCHESTRA instance without changing it:

```bash
sudo orchestra monitor --interval 5
```

An optional systemd unit provides the same foreground ownership model:

```bash
sudo systemctl enable --now orchestra.service
sudo systemctl status orchestra.service --no-pager
sudo systemctl disable --now orchestra.service
```

The installer never enables this unit automatically.

## Policy lifecycle

Inspect a policy without publishing it:

```bash
./scripts/policy_load.py --bridge \
  /var/tmp/orchestra-os-build-"$(id -u)"/orchestra_bridge \
  --dry-run config/examples/adaptive.json
```

Publish a safe policy only while the scheduler is attached:

```bash
sudo orchestra policy load /etc/orchestra-os/safe.json
sudo orchestra policy show
```

The loader writes the inactive bounded bank, then commits its generation.
Invalid action names, state indices, controller states, and oversized entry
sets are rejected before map publication. `EVALUATE` is read-only in the
example workflow and should be inspected with `--dry-run`.

## Actions and task admission

The canonical action identifiers are:

```text
RUN, SLEEP, MIGRATE, THROTTLE, YIELD
```

Normal tasks require the implemented admission/identity path before they can
be owned by the sched_ext prototype. Protected real-time classes are outside
the adaptive path. Use the existing bridge interface for an explicitly
authorized target, then verify exact identity and start-time generation in
telemetry. Never infer ownership from elapsed time alone.

## Telemetry and coordination

```bash
sudo orchestra telemetry
sudo orchestra controller status
```

Look for:

- signal/prediction generation, confidence, freshness, and fallback;
- policy-selected, controller-adjusted, capability-adjusted, and actual
  action fields;
- requested, accepted, dispatched, effective, and fallback counters;
- migration outcome and target CPU;
- S1, S2, S3, S4, Q, window generation, and sample interval;
- deficit class, actuator update, saturation, rollback, and recovery.

Never report Q without all four component scores. Never call a request count
an effective action without execution telemetry.

## Degraded mode and recovery

Stale/invalid signals, a bad controller record, unsupported actions, map
errors, and health failures select observed-state or `RUN` fallback. Inspect
the controller state and generation:

```bash
sudo orchestra controller status
sudo orchestra status
```

If the scheduler is degraded or rollback is active, stop introducing new
policy changes, collect telemetry and kernel logs, and disable cleanly:

```bash
sudo orchestra disable
```

After diagnosis, rebuild for the exact kernel and repeat the capability and
ownership gates. Do not delete all bpffs contents as a recovery shortcut.

## Safe shutdown

Disable ORCHESTRA before stopping a test workload or powering down:

```bash
sudo orchestra disable
```

The conventional Linux scheduler remains the intended safe destination.

## Security operating rules

Treat kernel-mode artifacts and policy files as privileged inputs. Install
target-matched artifacts through the installer, review policy changes, keep
the default observer mode on non-dedicated systems, and preserve the build
manifest with experiment evidence. ORCHESTRA currently validates schema,
generation, freshness, identity, ownership, and artifact integrity; it does
not claim cryptographic authentication of the kernel signal frame or signed
release provenance. See [the security model](../security/SECURITY.md) for the
threat boundary and reporting process.
