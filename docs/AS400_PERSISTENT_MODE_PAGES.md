# Superseded: AS/400-only saved MODE-page prototype

The initial SD-card sidecar design on this development branch has been
superseded by the **generic direct-access flash-backed MODE-page implementation**.

See [MODE_PAGE_FLASH_PERSISTENCE.md](MODE_PAGE_FLASH_PERSISTENCE.md) for the
current design, opt-in settings, tests, limitations and non-destructive
hardware validation procedure.

The legacy AS/400 model test is retained for regression coverage of the
captured B10 request. Its model-specific SD-sidecar runtime is not used by
the new MODE SELECT/MODE SENSE dispatch.
