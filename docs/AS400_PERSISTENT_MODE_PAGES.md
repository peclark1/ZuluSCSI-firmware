# Superseded: AS/400-only saved MODE-page prototype

The initial SD-card sidecar design on this development branch has been
superseded by the **generic direct-access flash-backed MODE-page implementation**.

See [MODE_PAGE_FLASH_PERSISTENCE.md](MODE_PAGE_FLASH_PERSISTENCE.md) for the
current design, opt-in settings, tests, limitations and non-destructive
hardware validation procedure.

The retired AS/400-only runtime and model test have been removed. The generic
flash regression suite includes the captured B10 MODE SELECT payload and its
readback behavior.
