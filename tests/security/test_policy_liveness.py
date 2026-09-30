"""Bounded offline regression tests for policy input and publication failures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import policy_load as policy

class Liveness(unittest.TestCase):
    def test_fifo_is_rejected_without_waiting_for_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            fifo = Path(directory) / 'policy'
            os.mkfifo(fifo)
            result = subprocess.run(
                [sys.executable, policy.__file__, '--bridge', '/unused', '--dry-run', str(fifo)],
                capture_output=True, timeout=3)
            self.assertEqual(result.returncode, 2)
            self.assertIn(b'regular file', result.stderr)

    def test_expired_deadline_never_executes(self):
        with patch.object(policy.subprocess, 'run') as execute:
            self.assertEqual(policy.run_bridge(['/bridge'], timeout=0), 124)
            self.assertEqual(policy.run_bridge(['/bridge'], timeout=-1), 124)
            execute.assert_not_called()

    def test_execution_error_is_reported(self):
        with patch.object(policy.subprocess, 'run', side_effect=OSError('exec failed')):
            self.assertEqual(policy.run_bridge(['/bridge']), 126)

    def test_failed_entry_aborts_without_commit(self):
        document = {'policy': {'entries': [{'state_index': 0, 'action': 'RUN'}]}}
        with patch.object(sys, 'argv', ['policy', '--bridge', '/bridge', '/policy']), \
             patch.object(policy.os, 'geteuid', return_value=0), \
             patch.object(policy, '_safe_bridge_path'), \
             patch.object(policy, 'load_policy_document', return_value=document), \
             patch.object(policy, 'run_bridge', side_effect=[126, 0]) as run:
            self.assertEqual(policy.main(), 126)
            self.assertEqual(run.call_count, 2)
            self.assertEqual(run.call_args.args[0], ['/bridge', '--policy-abort'])
            self.assertEqual(run.call_args.kwargs['timeout'], 10)

    def test_expiry_before_commit_aborts_with_recovery_budget(self):
        with patch.object(sys, 'argv', ['policy', '--bridge', '/bridge', '/policy']), \
             patch.object(policy.os, 'geteuid', return_value=0), \
             patch.object(policy, '_safe_bridge_path'), \
             patch.object(policy, 'load_policy_document', return_value={'policy': {'entries': []}}), \
             patch.object(policy.time, 'monotonic', side_effect=[0, 301]), \
             patch.object(policy.subprocess, 'run') as execute:
            execute.return_value.returncode = 0
            self.assertEqual(policy.main(), 124)
            execute.assert_called_once_with(['/bridge', '--policy-abort'], check=False, timeout=10)

if __name__ == '__main__':
    unittest.main()
