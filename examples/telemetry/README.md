# Telemetry

The bridge exposes structured status from the canonical v8 and native v10
maps. Use:

```bash
sudo ./scripts/orchestra telemetry
sudo ./scripts/orchestra controller status
```

The output must be interpreted with the ABI and capability fields. Missing
maps, stale generations, invalid records, and unsupported actions are
reported as fallback or unavailable rather than being treated as success.
