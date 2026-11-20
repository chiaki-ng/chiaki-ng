#!/usr/bin/env bash
# Puts a Developer ID certificate in a temporary keychain, or deletes that keychain.
# usage: macos-keychain.sh import   (reads MACOS_CERT_P12, base64, and MACOS_CERT_PASSWORD;
#                                    prints the keychain path, then the signing identity)
#        macos-keychain.sh delete <keychain>
set -euo pipefail

case "${1:-}" in
import)
    keychain="$RUNNER_TEMP/signing.keychain-db"
    keychain_password="$(uuidgen)"
    cert="$RUNNER_TEMP/cert.p12"
    trap 'rm -f "$cert"' EXIT
    echo "$MACOS_CERT_P12" | base64 --decode > "$cert"
    security create-keychain -p "$keychain_password" "$keychain"
    security set-keychain-settings -lut 3600 "$keychain"
    security unlock-keychain -p "$keychain_password" "$keychain"
    security import "$cert" -k "$keychain" -P "$MACOS_CERT_PASSWORD" -T /usr/bin/codesign >&2
    security set-key-partition-list -S apple-tool:,apple: -s -k "$keychain_password" "$keychain" > /dev/null
    security list-keychains -d user -s "$keychain" $(security list-keychains -d user | tr -d '"')
    identity="$(security find-identity -v -p codesigning "$keychain" | awk '/Developer ID Application/ { print $2; exit }')"
    test -n "$identity"
    echo "$keychain"
    echo "$identity"
    ;;
delete)
    keychain="${2:?keychain path}"
    if [[ -e "$keychain" ]]; then
        security delete-keychain "$keychain"
    fi
    ;;
*)
    echo "usage: $0 import | delete <keychain>" >&2
    exit 1
    ;;
esac
