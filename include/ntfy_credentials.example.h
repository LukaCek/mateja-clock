#pragma once

// Copy this file to include/ntfy_credentials.h and fill in the values. The
// access token is embedded in firmware at build time; keep it private.
//
// - MATEJA_NTFY_BASE_URL: the base of your ntfy installation, e.g.
//     "https://ntfy.example.com"  (no trailing slash, HTTPS required).
// - MATEJA_NTFY_INBOX_TOPIC: realtime love-message inbox topic.
// - MATEJA_NTFY_ACK_TOPIC: topic used for the "seen" acknowledgements
//     (leave empty to reuse the inbox topic).
// - MATEJA_NTFY_ACCESS_TOKEN: optional Bearer token; "" when the server is
//     open / no auth is required.
// - MATEJA_NTFY_CA_CERT (optional): leave undefined or "" to validate TLS
//     against the embedded public trust anchor (Google Trust Services "GTS
//     Root R4", the chain used by ntfy.cekluka.com). Define it only when your
//     server uses a different public CA or a custom CA.
//
// TLS is always verified against a CA; there is no setInsecure() fallback.

#define MATEJA_NTFY_BASE_URL ""
#define MATEJA_NTFY_INBOX_TOPIC "your-love-inbox"
#define MATEJA_NTFY_ACK_TOPIC ""
#define MATEJA_NTFY_ACCESS_TOKEN ""