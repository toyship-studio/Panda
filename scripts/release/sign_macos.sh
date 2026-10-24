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
# The keychain the identity is searched in: the one this script fills from MACOS_CERTIFICATE, or
# empty for the keychains of the system (a developer machine).
SIGNING_KEYCHAIN=""

has_identity() {
    security find-identity -v -p codesigning "$1" 2>/dev/null | grep -q '"Developer ID Application: '
}

import_certificate() {
    if [ -z "${MACOS_CERTIFICATE:-}" ]; then
        echo "MACOS_CERTIFICATE is not set: using the identities of the keychains of the system"
        return 0
    fi
    SIGNING_KEYCHAIN="$KEYCHAIN"
    # An earlier call of this run (the app, then the dmg) filled the keychain already.
    if [ -f "$KEYCHAIN" ] && has_identity "$KEYCHAIN"; then
        echo "Using the signing keychain of this run: $KEYCHAIN"
        return 0
    fi
    security delete-keychain "$KEYCHAIN" >/dev/null 2>&1 || true
    rm -f "$KEYCHAIN"

    local work password certificate
    work="$(mktemp -d)"
    password="$(uuidgen)"
    # A secret pasted from the clipboard may end with a line break.
    certificate="$(printf '%s' "$MACOS_CERTIFICATE" | tr -d '[:space:]')"
    printf '%s' "$certificate" | base64 --decode >"$work/certificate.p12"
    echo "Importing the certificate: ${#certificate} characters of base64, $(wc -c <"$work/certificate.p12" | tr -d ' ') bytes, into $KEYCHAIN"

    security create-keychain -p "$password" "$KEYCHAIN"
    # Stays unlocked for the run; codesign may not prompt.
    security set-keychain-settings -lut 21600 "$KEYCHAIN"
    security unlock-keychain -p "$password" "$KEYCHAIN"
    # Apple's intermediate certificates of Developer ID, the current G2 and the previous one:
    # codesign needs the complete chain of the certificate, and the runners do not carry it.
    local intermediate
    for intermediate in DeveloperIDG2CA DeveloperIDCA; do
        if curl -fsSL -o "$work/$intermediate.cer" "https://www.apple.com/certificateauthority/$intermediate.cer"; then
            security import "$work/$intermediate.cer" -k "$KEYCHAIN" -T /usr/bin/codesign >/dev/null || true
        else
            echo "Could not download the Apple intermediate certificate $intermediate" >&2
        fi
    done
    # The output names what the file held: "1 identity imported" is a certificate with its key.
    security import "$work/certificate.p12" -k "$KEYCHAIN" -P "${MACOS_CERTIFICATE_PASSWORD:-}" \
        -T /usr/bin/codesign -T /usr/bin/security
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
    local identities count
    # shellcheck disable=SC2086
    identities="$(security find-identity -v -p codesigning ${SIGNING_KEYCHAIN:+"$SIGNING_KEYCHAIN"} | grep '"Developer ID Application: ' || true)"
    count="$(printf '%s' "$identities" | grep -c . || true)"
    if [ "$count" -ne 1 ]; then
        echo "Expected exactly one valid Developer ID Application identity, found $count." >&2
        echo "Builds outside the App Store are signed with Developer ID Application: Apple Development and" >&2
        echo "Apple Distribution certificates cannot be notarized. The identities of the keychains, with the" >&2
        echo "reason when one is not valid (PANDA_MACOS_SIGN_IDENTITY chooses one of several):" >&2
        # shellcheck disable=SC2086
        security find-identity -p codesigning ${SIGNING_KEYCHAIN:+"$SIGNING_KEYCHAIN"} >&2
        if [ -n "$SIGNING_KEYCHAIN" ] && ! security find-identity -p codesigning "$SIGNING_KEYCHAIN" | grep -q '"'; then
            echo "MACOS_CERTIFICATE holds no certificate together with its private key. In Keychain Access," >&2
            echo "My Certificates, export the Developer ID Application certificate row itself, not the key" >&2
            echo "under it: a file with only the key imports without a word, a file with only the" >&2
            echo "certificate prints \"1 certificate imported\"." >&2
        fi
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
            codesign --force --options runtime --timestamp --sign "$IDENTITY" ${SIGNING_KEYCHAIN:+--keychain "$SIGNING_KEYCHAIN"} "$file"
        fi
    done < <(find "$app/Contents/Resources" -type f \( -perm -u+x -o -name "*.dylib" \) -print0)

    codesign --force --options runtime --timestamp --sign "$IDENTITY" \
        ${SIGNING_KEYCHAIN:+--keychain "$SIGNING_KEYCHAIN"} --entitlements "$ENTITLEMENTS" "$app"
    codesign --verify --deep --strict --verbose=2 "$app"
    echo "Signed: $app"
}

sign_dmg() {
    local dmg="$1"
    [ -f "$dmg" ] || { echo "Not a file: $dmg" >&2; exit 1; }
    codesign --force --timestamp --sign "$IDENTITY" ${SIGNING_KEYCHAIN:+--keychain "$SIGNING_KEYCHAIN"} "$dmg"
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
