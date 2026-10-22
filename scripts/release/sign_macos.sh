#!/usr/bin/env bash
# Signs the editor bundle or its dmg with a Developer ID Application certificate
# (context/infrastructure.md, "Engine CI and releases").
#
#   scripts/release/sign_macos.sh app bin/PandaEditor.app
#   scripts/release/sign_macos.sh dmg dist/PandaEditor-0.1.0-preview.142-macos-arm64.dmg
#
# The certificate comes from the environment of the release workflow: MACOS_CERTIFICATE (a .p12 in
# base64) and MACOS_CERTIFICATE_PASSWORD are imported once into a temporary keychain. Without them
# the identities of the login keychain are used (a developer machine). The identity is the single
# "Developer ID Application" certificate found, or PANDA_MACOS_SIGN_IDENTITY (a name or a SHA-1).
#
# The bundle is signed inside out with the hardened runtime: the tools in Contents/Resources (slangc
# and its libraries, cmake) first, then the bundle itself with native/OSX/PandaEditor.entitlements.
set -euo pipefail

usage() {
    echo "usage: $0 app <PandaEditor.app> | dmg <file.dmg>" >&2
    exit 2
}

KIND="${1:-}"
TARGET="${2:-}"
[ -n "$KIND" ] && [ -n "$TARGET" ] || usage

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENTITLEMENTS="$ROOT/PandaEditor/native/OSX/PandaEditor.entitlements"
KEYCHAIN="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/panda-signing.keychain-db"

import_certificate() {
    [ -n "${MACOS_CERTIFICATE:-}" ] || return 0
    # Imported by an earlier call of this run (the app, then the dmg).
    [ ! -f "$KEYCHAIN" ] || return 0

    local work password
    work="$(mktemp -d)"
    password="$(uuidgen)"
    printf '%s' "$MACOS_CERTIFICATE" | base64 --decode >"$work/certificate.p12"

    security create-keychain -p "$password" "$KEYCHAIN"
    # Stays unlocked for the run; codesign may not prompt.
    security set-keychain-settings -lut 21600 "$KEYCHAIN"
    security unlock-keychain -p "$password" "$KEYCHAIN"
    # Apple's intermediate certificate: codesign needs the complete chain of the Developer ID
    # certificate, and the runners do not carry it.
    if curl -fsSL -o "$work/DeveloperIDG2CA.cer" https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer; then
        security import "$work/DeveloperIDG2CA.cer" -k "$KEYCHAIN" -T /usr/bin/codesign >/dev/null || true
    fi
    security import "$work/certificate.p12" -k "$KEYCHAIN" -P "${MACOS_CERTIFICATE_PASSWORD:-}" \
        -T /usr/bin/codesign -T /usr/bin/security >/dev/null
    security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$password" "$KEYCHAIN" >/dev/null
    # shellcheck disable=SC2046
    security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '" ')
    rm -rf "$work"
}

find_identity() {
    if [ -n "${PANDA_MACOS_SIGN_IDENTITY:-}" ]; then
        IDENTITY="$PANDA_MACOS_SIGN_IDENTITY"
        return
    fi
    local identities
    identities="$(security find-identity -v -p codesigning | grep "Developer ID Application" || true)"
    if [ "$(printf '%s\n' "$identities" | grep -c "Developer ID Application")" -ne 1 ]; then
        echo "Expected exactly one Developer ID Application identity (set PANDA_MACOS_SIGN_IDENTITY to choose):" >&2
        security find-identity -v -p codesigning >&2
        exit 1
    fi
    # The SHA-1 of the certificate names it unambiguously.
    IDENTITY="$(printf '%s\n' "$identities" | awk '{print $2}')"
    echo "Signing identity: $(printf '%s\n' "$identities" | sed -E 's/^[^"]*"([^"]*)".*$/\1/')"
}

sign_app() {
    local app="$1"
    [ -d "$app/Contents/MacOS" ] || { echo "Not an application bundle: $app" >&2; exit 1; }

    # Nested executables first: the signature of the bundle seals everything inside it.
    local file
    while IFS= read -r -d '' file; do
        if file -b "$file" | grep -q "Mach-O"; then
            codesign --force --options runtime --timestamp --sign "$IDENTITY" "$file"
        fi
    done < <(find "$app/Contents/Resources" -type f \( -perm -u+x -o -name "*.dylib" \) -print0)

    codesign --force --options runtime --timestamp --sign "$IDENTITY" \
        --entitlements "$ENTITLEMENTS" "$app"
    codesign --verify --deep --strict --verbose=2 "$app"
    echo "Signed: $app"
}

sign_dmg() {
    local dmg="$1"
    [ -f "$dmg" ] || { echo "Not a file: $dmg" >&2; exit 1; }
    codesign --force --timestamp --sign "$IDENTITY" "$dmg"
    codesign --verify --verbose=2 "$dmg"
    echo "Signed: $dmg"
}

import_certificate
find_identity
case "$KIND" in
    app) sign_app "$TARGET" ;;
    dmg) sign_dmg "$TARGET" ;;
    *) usage ;;
esac
