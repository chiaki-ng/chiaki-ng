# Bundled curl Schannel AIA extension

Windows bundled-curl builds copy curl into the build directory and apply
`curl-schannel-aia.patch` to that copy at configuration. The original submodule
is left unchanged. Configuration fails if the source no longer matches the
patch. No private curl fork or new submodule revision is required.

The private `CURLSSLOPT_SCHANNEL_AIA` flag (bit 7 in the pinned curl version)
changes CAINFO_BLOB into untrusted issuer material for Schannel only. The
flag is part of curl's existing TLS configuration comparison, preventing reuse
of connections with different verification modes. Rebase the flag if a curl
update assigns bit 7 another meaning.

Schannel combines the server certificate store and downloaded issuers using a
temporary collection store passed to CertGetCertificateChain's additional-store
argument. It keeps the default Windows chain engine and does not set
hExclusiveRoot, populate the CA trust cache, or change hostname/revocation checks.
An explicit CA file combined with this flag is rejected, not silently ignored.
System-curl builds do not enable this private integration.

## Windows validation

Build with CHIAKI_USE_SYSTEM_CURL=OFF, then run chiaki-unit /chiaki/aia/.
Those unit tests validate recovery/export logic, not Schannel handshakes.

Before release, exercise TLS connections with a controlled server and a test
root installed in an isolated Windows test account or VM:

- A complete trusted chain succeeds without recovery.
- A leaf missing its intermediate succeeds after AIA recovery.
- Reconnect to that server, then to a server under a different trusted root;
  both succeed with the issuer cache populated.
- A cached intermediate without its trusted root must fail validation.
- An unrelated self-signed certificate in the additional store must not make
  a server signed by it trusted.
- Expired roots, wrong hostnames, and revoked certificates must still fail.
- Cancel during recovery and confirm the worker exits before session cleanup.

The application logs `Schannel AIA recovery uses untrusted issuers and native
Windows roots` when the patched path is available.
