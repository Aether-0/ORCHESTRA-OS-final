# Controller and coordination

The native controller consumes finalized coordination windows and keeps
bounded actuator state. Inspect its state after an attached run:

```bash
sudo ./scripts/orchestra controller status
```

Valid coordination output includes S1, S2, S3, S4, the geometric-mean Q,
window generation, deficit class, actuator updates, saturation, rollback,
and recovery counters. A Q value without all four component scores is not a
valid coordination result.
