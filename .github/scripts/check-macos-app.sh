#!/usr/bin/env bash
#
# Check that a built GPlates .app is something a Mac user can actually open.
#
# CI builds the app on the machine that produced it, so it never carries the 'com.apple.quarantine'
# flag a browser attaches to a download - which means an ordinary build, however green, can never
# reproduce the failure a user hits. This script stamps the app with quarantine exactly as a
# download would, and then asks macOS itself what it makes of it.
#
# Usage:
#
#     check-macos-app.sh <path-to-.dmg>
#     check-macos-app.sh <path-to-.app>
#
# Given a .dmg it mounts it and copies the app out, the way a user dragging the app to
# /Applications would.
#
# Fails if the code signature is invalid, or if the kernel refuses to exec the binary. Those are
# the two conditions that produce "GPlates is damaged and can't be opened" - a dialog which, unlike
# the ordinary unidentified-developer prompt, offers the user no way to proceed.
#
# Gatekeeper's own verdict is reported but is NOT treated as a failure. Without notarisation (which
# requires a paid Apple Developer account) 'spctl' always rejects, and the user is expected to
# approve the app once via Right-click > Open. Failing on that would be failing on a decision the
# fork has already made.

# Note: '-e' is deliberately not set. Each check below records its own result so that one failure
#       still leaves the remaining diagnostics to be printed - a report of everything that is wrong
#       is far more useful here than the first thing that went wrong.
set -uo pipefail

TARGET="${1:?usage: check-macos-app.sh <path-to-.dmg-or-.app>}"

FAILED=0
MOUNT=""

cleanup() {
    if [ -n "$MOUNT" ]; then
        hdiutil detach "$MOUNT" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

#
# Resolve the target down to a writable .app.
#
if [ "${TARGET##*.}" = "dmg" ]; then
    echo "==> Mounting $TARGET"
    MOUNT=$(mktemp -d)
    if ! hdiutil attach "$TARGET" -mountpoint "$MOUNT" -nobrowse -readonly; then
        echo "FAIL: could not mount $TARGET"
        exit 1
    fi

    SRC=$(find "$MOUNT" -maxdepth 1 -name "*.app" | head -1)
    if [ -z "$SRC" ]; then
        echo "FAIL: no .app found in $TARGET"
        exit 1
    fi

    # 'ditto' rather than 'cp -R': it preserves symlinks, permissions and extended attributes.
    # 'cp -R' can flatten the symlink farm inside a bundle enough to invalidate its signature, which
    # would fail this check for a reason no user would ever encounter.
    APP="$(mktemp -d)/$(basename "$SRC")"
    echo "==> Copying $(basename "$SRC") out of the image"
    if ! ditto "$SRC" "$APP"; then
        echo "FAIL: could not copy the app out of $TARGET"
        exit 1
    fi
else
    APP="$TARGET"
fi

if [ ! -d "$APP" ]; then
    echo "FAIL: $APP does not exist"
    exit 1
fi

echo "==> Checking $APP"
echo

#
# 1. Stamp the app the way a download would.
#
# The value is the same four-field form Safari and Chrome write: flags, hex timestamp, the agent
# that did the downloading, and a UUID. 0081 marks a downloaded item that the user has not yet
# approved, which is precisely the state we want to test.
#
echo "==> Applying com.apple.quarantine (simulating a download)"
xattr -w com.apple.quarantine \
    "0081;$(printf '%x' "$(date +%s)");Safari;$(uuidgen)" "$APP"
if xattr -p com.apple.quarantine "$APP" >/dev/null 2>&1; then
    echo "    quarantine: $(xattr -p com.apple.quarantine "$APP")"
else
    echo "    WARNING: quarantine attribute did not stick - the checks below are weaker than intended"
fi
echo

#
# 2. The signature must be valid.
#
# This is the check that would have caught the unsigned dev8 build. '--deep' walks the nested
# frameworks, plugins and dylibs; '--strict' refuses to overlook a bundle whose sealed resources no
# longer match. A bundle that fails here is one whose dependency paths were rewritten by
# 'install_name_tool' without being re-signed afterwards.
#
echo "==> Verifying the code signature"
if codesign --verify --deep --strict --verbose=2 "$APP"; then
    echo "    OK: signature is valid"
else
    echo "    FAIL: signature is invalid or missing - macOS will report this app as damaged"
    FAILED=1
fi
echo

echo "==> Signature details"
codesign --display --verbose=2 "$APP" 2>&1 | sed 's/^/    /'
echo

#
# 3. The kernel must be willing to exec the binary.
#
# On Apple Silicon every Mach-O must carry a valid signature to run at all, and a binary that fails
# that test is killed outright rather than being reported politely. Actually exec'ing it is the only
# way to prove the bundle clears that bar - and it exercises dyld too, so a dependency that was
# bundled with a bad path shows up here rather than on a user's machine.
#
EXECUTABLE=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$APP/Contents/Info.plist" 2>/dev/null)
BIN="$APP/Contents/MacOS/${EXECUTABLE:-gplates}"

echo "==> Running $(basename "$BIN") to confirm it is allowed to start"
if [ ! -x "$BIN" ]; then
    echo "    FAIL: $BIN is missing or not executable"
    FAILED=1
else
    OUT=$(mktemp)
    KILLED=$(mktemp)
    rm -f "$KILLED"

    "$BIN" --help >"$OUT" 2>&1 &
    BIN_PID=$!

    # GPlates is a GUI application and '--help' may well open a window instead of printing to the
    # terminal. That is a perfectly good outcome for this check - it means the process started - so
    # a watchdog stops it and leaves a marker saying the kill was ours, not the kernel's.
    ( sleep 25; if kill -0 "$BIN_PID" 2>/dev/null; then touch "$KILLED"; kill -9 "$BIN_PID" 2>/dev/null; fi ) &
    WATCHDOG_PID=$!

    wait "$BIN_PID"
    RC=$?
    kill "$WATCHDOG_PID" 2>/dev/null
    wait "$WATCHDOG_PID" 2>/dev/null

    if [ -f "$KILLED" ]; then
        echo "    OK: process started and stayed up (stopped by watchdog after 25s)"
    elif [ "$RC" -eq 137 ]; then
        echo "    FAIL: killed by the kernel (SIGKILL) - the signature was rejected at exec time"
        FAILED=1
    elif grep -qiE "Library not loaded|image not found|code signature|Symbol not found" "$OUT"; then
        echo "    FAIL: the binary could not load its own bundled libraries:"
        sed 's/^/        /' "$OUT" | head -20
        FAILED=1
    else
        echo "    OK: process started and exited with code $RC"
        if [ -s "$OUT" ]; then
            head -5 "$OUT" | sed 's/^/        /'
        fi
    fi
fi
echo

#
# 4. Report Gatekeeper's verdict - for information only. See the note at the top of this file.
#
echo "==> Gatekeeper assessment (informational - see the note at the top of this script)"
if spctl --assess --type execute --verbose=4 "$APP" 2>&1 | sed 's/^/    /'; then
    echo "    Gatekeeper accepts this app outright - it opens with no prompt at all."
else
    echo
    echo "    Gatekeeper rejects this app, which is expected for a build that is not notarised."
    echo "    Provided the signature check above passed, the user gets the ordinary"
    echo "    unidentified-developer prompt and can approve it once with Right-click > Open,"
    echo "    or by clearing the quarantine flag:"
    echo
    echo "        xattr -dr com.apple.quarantine /Applications/$(basename "$APP")"
fi
echo

if [ "$FAILED" -ne 0 ]; then
    echo "RESULT: FAILED - this build would land on a Mac user as \"GPlates is damaged and can't be opened\"."
    exit 1
fi

echo "RESULT: PASSED - validly signed and allowed to run; the user approves it once on first launch."
