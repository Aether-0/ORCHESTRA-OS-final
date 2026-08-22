# CPU pressure

Run a disposable CPU workload while observing the product control plane:

```bash
./examples/cpu_pressure/run.sh 10
```

The argument is the duration in seconds. The example uses only standard
shell tools and `sha256sum`; it is suitable for observer-mode measurements.
When kernel mode is enabled, prove task ownership from scheduler telemetry
before attributing a result to ORCHESTRA.
