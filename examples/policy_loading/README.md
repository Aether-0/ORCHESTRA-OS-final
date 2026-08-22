# Policy loading

Validate a policy without touching kernel maps:

```bash
./scripts/policy_load.py \
  --bridge /var/tmp/orchestra-os-build-"$(id -u)"/orchestra_bridge \
  --dry-run config/examples/adaptive.json
```

When the scheduler is attached and the bridge is running as an authorized
administrator, publish the safe example through the user-facing command:

```bash
sudo ./scripts/orchestra policy load config/examples/safe.json
sudo ./scripts/orchestra policy show
```
