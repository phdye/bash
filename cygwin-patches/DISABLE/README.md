# Disabled Patches

This directory contains patches that are **intentionally not applied** to this repository.
They are preserved here for reference and in case they need to be applied in specific scenarios.

## no-loadable-builtins.patch

**Purpose:** Removes loadable builtins from `make install` target.

**Why disabled:** This repository aims to investigate and enable loadable modules support
on Cygwin. The patch would defeat that purpose.

**Original rationale:** FHS (Filesystem Hierarchy Standard) compliance - Fedora considered
the loadable examples to be sample code that shouldn't be installed in /usr/lib/bash.

**Alternative approach:** Debian provides loadable builtins as a separate `bash-builtins`
package, allowing users to opt-in.

**To apply if needed:**
```bash
cd /path/to/bash
patch -p1 < cygwin-patches/DISABLE/no-loadable-builtins.patch
```
