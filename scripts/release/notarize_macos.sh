#!/usr/bin/env bash
# Notarizes a signed dmg with Apple and staples the ticket to it
# (context/infrastructure.md, "Engine CI and releases").
#
#   scripts/release/notarize_macos.sh dist/PandaEditor-0.1.0-preview.142-macos-arm64.dmg
#
# Environment: APPLE_NOTARIZATION_KEY (the .p8 of an App Store Connect API key, as text or in
# base64), APPLE_NOTARIZATION_KEY_ID and APPLE_NOTARIZATION_ISSUER_ID. The notarization of the
# image covers the bundle inside it; Gatekeeper finds the ticket of the bundle online, the stapled
# image opens offline.
set -euo pipefail

DMG="${1:-}"
[ -n "$DMG" ] && [ -f "$DMG" ] || { echo "usage: $0 <file.dmg>" >&2; exit 2; }
for variable in APPLE_NOTARIZATION_KEY APPLE_NOTARIZATION_KEY_ID APPLE_NOTARIZATION_ISSUER_ID; do
    [ -n "${!variable:-}" ] || { echo "$variable is not set" >&2; exit 1; }
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
KEY="$WORK/AuthKey_$APPLE_NOTARIZATION_KEY_ID.p8"
if printf '%s' "$APPLE_NOTARIZATION_KEY" | grep -q "BEGIN PRIVATE KEY"; then
    printf '%s\n' "$APPLE_NOTARIZATION_KEY" >"$KEY"
else
    printf '%s' "$APPLE_NOTARIZATION_KEY" | base64 --decode >"$KEY"
fi
chmod 600 "$KEY"

echo "Submitting $DMG for notarization"
xcrun notarytool submit "$DMG" --key "$KEY" --key-id "$APPLE_NOTARIZATION_KEY_ID" \
    --issuer "$APPLE_NOTARIZATION_ISSUER_ID" --wait --timeout 45m --output-format json \
    >"$WORK/submission.json" || true
cat "$WORK/submission.json"
STATUS="$(sed -n 's/.*"status" *: *"\([^"]*\)".*/\1/p' "$WORK/submission.json" | head -n 1)"
SUBMISSION="$(sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' "$WORK/submission.json" | head -n 1)"
if [ "$STATUS" != "Accepted" ]; then
    echo "Notarization was not accepted (status: ${STATUS:-unknown})" >&2
    if [ -n "$SUBMISSION" ]; then
        xcrun notarytool log "$SUBMISSION" --key "$KEY" --key-id "$APPLE_NOTARIZATION_KEY_ID" \
            --issuer "$APPLE_NOTARIZATION_ISSUER_ID" >&2 || true
    fi
    exit 1
fi

xcrun stapler staple "$DMG"
# The assessment Gatekeeper makes when a downloaded image is opened.
spctl --assess --type open --context context:primary-signature --verbose=2 "$DMG"
echo "Notarized and stapled: $DMG"
