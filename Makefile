CC ?= gcc
ORCHESTRA_BUILD_DIR ?= /var/tmp/orchestra-os-build-$(shell id -u)
ORCHESTRA_INCLUDE_DIR := kernel/sched_ext/include
ORCHESTRA_BRIDGE_DIR := kernel/sched_ext/bridge
LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf 2>/dev/null)
LIBBPF_LIBS := $(shell pkg-config --libs libbpf 2>/dev/null || echo '-lbpf -lelf -lz')

.PHONY: all userspace bridge product kernel-bpf check test test-unit \
	test-integration security-test clean clean-product

all: userspace

userspace:
	$(MAKE) -C orchestra_paper_cpu_demo

bridge:
	@case "$(ORCHESTRA_BUILD_DIR)" in /*) ;; *) echo "ORCHESTRA_BUILD_DIR must be absolute" >&2; exit 2 ;; esac
	@case "$(ORCHESTRA_BUILD_DIR)" in $(CURDIR)|$(CURDIR)/*) echo "refusing build output inside repository" >&2; exit 2 ;; esac
	@bash -c '. "$(CURDIR)/scripts/path_safety.sh" && orchestra_ensure_private_dir "$$1"' -- "$(ORCHESTRA_BUILD_DIR)"
	@[ ! -L "$(ORCHESTRA_BUILD_DIR)/orchestra_bridge" ] && [ ! -L "$(ORCHESTRA_BUILD_DIR)/orchestra_loader" ]
	$(CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
		-Wformat=2 -Werror -I"$(ORCHESTRA_INCLUDE_DIR)" \
		"$(ORCHESTRA_BRIDGE_DIR)/orchestra_bridge.c" \
		-o "$(ORCHESTRA_BUILD_DIR)/orchestra_bridge"
	@if printf '#include <bpf/libbpf.h>\n' | $(CC) $(LIBBPF_CFLAGS) -E - >/dev/null 2>&1; then \
		$(CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
			-Wformat=2 -Werror $(LIBBPF_CFLAGS) -I"$(ORCHESTRA_INCLUDE_DIR)" \
			"$(ORCHESTRA_BRIDGE_DIR)/orchestra_loader.c" \
			-o "$(ORCHESTRA_BUILD_DIR)/orchestra_loader" $(LIBBPF_LIBS); \
	else \
		echo "BLOCKED_MISSING_LIBBPF_HEADERS: bridge built; loader deferred"; \
	fi
	@chmod 0755 "$(ORCHESTRA_BUILD_DIR)/orchestra_bridge"
	@if [ -e "$(ORCHESTRA_BUILD_DIR)/orchestra_loader" ]; then chmod 0755 "$(ORCHESTRA_BUILD_DIR)/orchestra_loader"; fi

product: userspace bridge

kernel-bpf:
	ORCHESTRA_BUILD_DIR="$(ORCHESTRA_BUILD_DIR)" \
		bash kernel/sched_ext/scripts/build_stage7_out_of_tree.sh

check:
	$(MAKE) -C orchestra_paper_cpu_demo check
	@if command -v clang >/dev/null 2>&1; then \
		clang -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
			-Wformat=2 -Werror -fsyntax-only \
			orchestra_paper_cpu_demo/orchestra_paper_cpu.c; \
		for publication_mode in 0 1; do \
			clang -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
				-Wformat=2 -Werror -pthread \
				-DORCHESTRA_SIGNAL_PUBLICATION_LEGACY=$$publication_mode \
				-fsyntax-only tools/benchmark/signal_publication_microbenchmark.c; \
		done; \
		clang -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
			-Wformat=2 -Werror -fsyntax-only \
			tests/integration/test_signal_publication_integration.c; \
	fi
	@for publication_mode in 0 1; do \
		$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
			-Wformat=2 -Werror -pthread \
			-DORCHESTRA_SIGNAL_PUBLICATION_LEGACY=$$publication_mode \
			-fsyntax-only tools/benchmark/signal_publication_microbenchmark.c; \
	done
	@$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
		-Wformat=2 -Werror -fsyntax-only \
		tests/integration/test_signal_publication_integration.c
	@$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
		-Wformat=2 -Werror -Ikernel/sched_ext/include -fsyntax-only \
		kernel/sched_ext/bridge/orchestra_bridge.c
	@$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
		-Wformat=2 -Werror -fsyntax-only \
		kernel/sched_ext/scripts/fixed_work.c
	@if printf '#include <bpf/libbpf.h>\n' | $(CC) -E - >/dev/null 2>&1; then \
		$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
			-Wformat=2 -Werror -Ikernel/sched_ext/include -fsyntax-only \
			kernel/sched_ext/bridge/orchestra_loader.c; \
	fi
	@$(CC) -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
		-Wformat=2 -Werror -fsyntax-only kernel/sched_ext/orchestra_scx.c
	@PYTHONPYCACHEPREFIX=/tmp/orchestra-os-check-pyc \
		python3 -m py_compile \
		tools/benchmark/run_paper_cpu_benchmark.py \
		tools/benchmark/run_signal_publication_microbenchmark.py \
		tools/plotting/plot_paper_cpu_smoke.py \
		tools/plotting/plot_release_readiness.py \
		tools/testing/capture_test_run.py \
		tools/testing/index_test_artifacts.py \
		tests/integration/validate_paper_cpu_csv.py \
		tests/integration/test_validate_paper_cpu_csv.py \
		tests/unit/test_benchmark_validator.py \
		tests/unit/test_signal_publication_microbenchmark_runner.py
	@python3 -m json.tool experiments/manifests/paper_cpu_smoke_v1.json >/dev/null
	@python3 -m json.tool experiments/schemas/paper_cpu_metrics_v2.json >/dev/null
	@python3 -m json.tool experiments/schemas/paper_cpu_metrics_v3.json >/dev/null
	@python3 -m json.tool experiments/schemas/paper_cpu_metrics_v4.json >/dev/null
	@python3 -m json.tool experiments/schemas/paper_cpu_metrics_v7.json >/dev/null
	@python3 -m json.tool experiments/manifests/paper_cpu_exploratory_v3.json >/dev/null
	@python3 -m json.tool experiments/manifests/paper_cpu_exploratory_v4.json >/dev/null
	@bash -n tests/unit/run.sh tests/integration/run.sh \
		tests/security/run.sh tests/security/test_install_paths.sh \
		benchmarks/real-machine/benchmark_suite.sh \
		benchmarks/real-machine/full_compare.sh \
		benchmarks/real-machine/stress_suite.sh \
		benchmarks/stage9/benchmark_compare.sh \
		kernel/sched_ext/scripts/build_stage7_out_of_tree.sh \
		kernel/sched_ext/scripts/p0_ownership_retest.sh \
		kernel/sched_ext/scripts/reproduce_stage7_runtime.sh \
		kernel/sched_ext/scripts/stage8_validate.sh
	@bash -n scripts/*.sh examples/*/*.sh
	@PYTHONPYCACHEPREFIX=/tmp/orchestra-os-check-pyc \
		python3 -m py_compile scripts/*.py
	@for config_file in config/examples/*.json; do \
		python3 -m json.tool "$$config_file" >/dev/null; \
	done
	@python3 scripts/check-doc-links.py
	@scripts/security-scan.sh
	@if command -v shellcheck >/dev/null 2>&1; then \
		shellcheck tests/unit/run.sh tests/integration/run.sh; \
	fi

test-unit:
	./tests/unit/run.sh

test-integration:
	./tests/integration/run.sh

security-test:
	./tests/security/run.sh

test: check test-unit test-integration security-test

clean:
	$(MAKE) -C orchestra_paper_cpu_demo clean

clean-product:
	@case "$(ORCHESTRA_BUILD_DIR)" in \
		/var/tmp/orchestra-os-build-*|/tmp/orchestra-os-build-*) ;; \
		*) echo "refusing to remove non-product build path: $(ORCHESTRA_BUILD_DIR)" >&2; exit 2 ;; \
	esac
	@if [ -e "$(ORCHESTRA_BUILD_DIR)" ] || [ -L "$(ORCHESTRA_BUILD_DIR)" ]; then \
		bash -c '. "$(CURDIR)/scripts/path_safety.sh" && orchestra_safe_existing_dir "$$1"' -- "$(ORCHESTRA_BUILD_DIR)"; \
		rm -rf -- "$(ORCHESTRA_BUILD_DIR)"; \
	fi
