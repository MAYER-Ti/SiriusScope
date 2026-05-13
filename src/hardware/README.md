# Hardware adapter layer

Hardware adapter layer scaffold for SiriusScope.

Responsibilities:
- UDP/TCP clients and protocol parsers;
- BCO control adapter for reception configuration;
- hardware and simulator adapters behind application interfaces;
- protocol-version isolation and diagnostics.

SiriusScope must not implement a direct RPU adapter. Receiver settings that affect the RPU are sent to the BCO, and the BCO controls the RPU internally.

This file is a placeholder for architectural stage 1.1.
