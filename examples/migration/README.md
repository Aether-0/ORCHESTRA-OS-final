# Migration action

`MIGRATE` is a request that is still subject to task affinity, CPU-online,
capability, and safety checks. A request is not proof of effective movement.

With an attached scheduler and a valid target PID, inspect the available
bridge interface first:

```bash
./scripts/orchestra telemetry
sudo "$ORCHESTRA_BUILD_DIR/orchestra_bridge" --publish \
  --action MIGRATE --target-pid "$PID" --target-cpu "$CPU" \
  --slice-ns 5000000
```

Compare requested, accepted, effective, and fallback counters afterward.
Do not run this example on a production machine.
