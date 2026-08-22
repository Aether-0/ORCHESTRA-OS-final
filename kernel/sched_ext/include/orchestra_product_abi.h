/* SPDX-License-Identifier: GPL-2.0 */
/* Stable product-level ABI bundle for ORCHESTRA-OS release tooling. */
#ifndef ORCHESTRA_PRODUCT_ABI_H
#define ORCHESTRA_PRODUCT_ABI_H

#include "orchestra_abi.h"
#include "orchestra_kernel_v8.h"
#include "orchestra_control_abi.h"

#define ORCHESTRA_PRODUCT_ABI_MAJOR 1u
#define ORCHESTRA_PRODUCT_ABI_MINOR 0u
#define ORCHESTRA_PRODUCT_ABI_PATCH 0u

/* The bridge ABI remains v2 for compatibility; native coordination and the
 * controller are additive control ABI v10 records. Consumers must validate
 * both values and never reinterpret an older record as a newer one. */
#define ORCHESTRA_PRODUCT_BRIDGE_ABI_VERSION ORCHESTRA_ABI_VERSION
#define ORCHESTRA_PRODUCT_KERNEL_ABI_VERSION ORCHESTRA_KERNEL_ABI_VERSION
#define ORCHESTRA_PRODUCT_CONTROL_ABI_VERSION ORCHESTRA_CONTROL_ABI_VERSION

#define ORCHESTRA_PRODUCT_ACTION_MASK \
    ((1u << ORCHESTRA_ACTION_RUN) | \
     (1u << ORCHESTRA_ACTION_SLEEP) | \
     (1u << ORCHESTRA_ACTION_MIGRATE) | \
     (1u << ORCHESTRA_ACTION_THROTTLE) | \
     (1u << ORCHESTRA_ACTION_YIELD))

#endif /* ORCHESTRA_PRODUCT_ABI_H */
