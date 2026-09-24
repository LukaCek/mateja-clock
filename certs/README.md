# GitHub OTA CA bundle

`x509_crt_bundle.bin` is public trust material, not a credential. It uses the
compact certificate-bundle format consumed by Arduino-ESP32 2.0.17
`WiFiClientSecure::setCACertBundle()`.

Generation environment:

- Source: Arch Linux Mozilla TLS bundle
  `/etc/ca-certificates/extracted/tls-ca-bundle.pem`
- Source SHA-256:
  `8c97794a899a32666593979dc7adfaec8bda2b420eb092ff0d987e44ad0bf6ea`
- `cryptography==43.0.3`
- 121 certificates
- Output size: 55,587 bytes
- Output SHA-256:
  `49e7e1ca53f48330b1b507872f1447eb5f333632b6802282ec51aaab5640787c`

The generator follows ESP-IDF 4.4.7 `gen_crt_bundle.py`: certificates are
sorted by DER subject, then encoded as a big-endian certificate count followed
by subject/SPKI length pairs and DER payloads.

Production OTA still applies a strict HTTPS redirect-host allowlist. The bundle
does not by itself authorize arbitrary OTA origins.
