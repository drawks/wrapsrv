#!/bin/bash
# run_tests.sh — integration and unit test runner for wrapsrv.
#
# Run from anywhere: this script changes to its own directory so relative paths
# to the built binaries resolve correctly.

set -euo pipefail
cd "$(dirname "$0")"

PASS=0
FAIL=0
WRAPSRV=../wrapsrv
FAKE_LIB=./fake_res_query.so

# ---- helpers --------------------------------------------------------------

ok() {
    echo "ok - $*"
    PASS=$((PASS + 1))
}

not_ok() {
    echo "not ok - $*"
    FAIL=$((FAIL + 1))
}

run_test() {
    local desc="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        ok "$desc"
    else
        not_ok "$desc"
    fi
}

fail_test() {
    local desc="$1"
    shift
    if ! "$@" >/dev/null 2>&1; then
        ok "$desc"
    else
        not_ok "$desc"
    fi
}

# ---- unit tests (cmocka) --------------------------------------------------

echo "# Running unit tests"
if ./test_wrapsrv; then
    ok "unit tests passed"
else
    not_ok "unit tests failed"
fi

# ---- integration tests (LD_PRELOAD fake resolver) -------------------------

echo "# Running integration tests"

# Test: --version flag
run_test "--version prints version" \
    bash -c "$WRAPSRV --version | grep -q '\\.'"

# Test: too few arguments exits non-zero
fail_test "usage: too few args exits non-zero" \
    bash -c "$WRAPSRV _service._tcp.example.com"

# Test: single SRV record — command receives correct host and port
run_test "single SRV record: correct host substitution" \
    bash -c "
        output=\$(FAKE_SRV_RECORDS='srv1.example.com:8080:10:1' \
            LD_PRELOAD='$FAKE_LIB' \
            $WRAPSRV _test._tcp.example.com echo '%h')
        [ \"\$output\" = 'srv1.example.com' ]
    "

run_test "single SRV record: correct port substitution" \
    bash -c "
        output=\$(FAKE_SRV_RECORDS='srv1.example.com:8080:10:1' \
            LD_PRELOAD='$FAKE_LIB' \
            $WRAPSRV _test._tcp.example.com echo '%p')
        [ \"\$output\" = '8080' ]
    "

run_test "single SRV record: host:port substitution" \
    bash -c "
        output=\$(FAKE_SRV_RECORDS='myhost.example.com:9090:10:1' \
            LD_PRELOAD='$FAKE_LIB' \
            $WRAPSRV _svc._tcp.example.com echo '%h:%p')
        [ \"\$output\" = 'myhost.example.com:9090' ]
    "

# Test: multiple priority levels — lower priority tried first;
#       simulate failure at lower priority by running a command that exits 1.
run_test "multi-priority: lower priority tried first" \
    bash -c "
        # The first record (prio=5) runs 'false', second (prio=10) runs 'true'
        # via a dispatch script.
        FAKE_SRV_RECORDS='prio5.example.com:80:5:1,prio10.example.com:80:10:1' \
            LD_PRELOAD='$FAKE_LIB' \
            $WRAPSRV _test._tcp.example.com bash -c \
            '[ \"%h\" = prio5.example.com ] && exit 1; exit 0'
    "

# Test: all records fail — wrapsrv must exit non-zero
fail_test "all records fail: wrapsrv exits non-zero" \
    bash -c "
        FAKE_SRV_RECORDS='a.example.com:80:10:1' \
            LD_PRELOAD='$FAKE_LIB' \
            $WRAPSRV _test._tcp.example.com false
    "

# Test: successful command — wrapsrv exits 0
run_test "successful command exits 0" \
    bash -c "
        FAKE_SRV_RECORDS='ok.example.com:80:10:1' \
            LD_PRELOAD='$FAKE_LIB' \
            $WRAPSRV _test._tcp.example.com true
    "

# ---- summary --------------------------------------------------------------

TOTAL=$((PASS + FAIL))
echo "1..$TOTAL"
echo "# $PASS/$TOTAL tests passed"

if [ "$FAIL" -gt 0 ]; then
    echo "# FAILED: $FAIL test(s)"
    exit 1
fi
exit 0
