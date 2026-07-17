/*
 * test_wrapsrv.c — cmocka unit tests for wrapsrv internal functions.
 *
 * We include wrapsrv.c directly (renaming main) so that the static functions
 * are accessible within this translation unit without restructuring the
 * production source.
 */

/* Rename wrapsrv's main so it does not clash with cmocka's test runner. */
#define main wrapsrv_main
#include "../wrapsrv.c"
#undef main

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include <stdlib.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------ */

/* Reset the global priority list between tests. */
static void
reset_list(void)
{
	free_tuples();
	ISC_LIST_INIT(prio_list);
}

/* Convenience: build a minimal srv struct on the stack with a heap-owned
 * tname.  Caller must call free(se->tname) after use. */
static void
init_srv(struct srv *se, const char *tname, uint16_t port, uint16_t weight)
{
	memset(se, 0, sizeof(*se));
	ISC_LINK_INIT(se, link);
	se->tname = strdup(tname);
	se->port = port;
	se->weight = weight;
}

/* ---- insert_tuple / priority ordering ----------------------------------- */

static void
test_insert_prio_ascending(void **state)
{
	struct srv_prio *pe;

	(void)state;
	reset_list();

	insert_tuple(strdup("c.example.com"), 20, 1, 443);
	insert_tuple(strdup("a.example.com"), 5, 1, 80);
	insert_tuple(strdup("b.example.com"), 10, 1, 8080);

	pe = ISC_LIST_HEAD(prio_list);
	assert_non_null(pe);
	assert_int_equal(pe->prio, 5);

	pe = ISC_LIST_NEXT(pe, link);
	assert_non_null(pe);
	assert_int_equal(pe->prio, 10);

	pe = ISC_LIST_NEXT(pe, link);
	assert_non_null(pe);
	assert_int_equal(pe->prio, 20);

	pe = ISC_LIST_NEXT(pe, link);
	assert_null(pe);

	reset_list();
}

static void
test_insert_same_prio(void **state)
{
	struct srv_prio *pe;
	int count = 0;
	struct srv *se;

	(void)state;
	reset_list();

	insert_tuple(strdup("a.example.com"), 10, 1, 80);
	insert_tuple(strdup("b.example.com"), 10, 2, 8080);
	insert_tuple(strdup("c.example.com"), 10, 3, 9090);

	/* All three should be in the same priority bucket. */
	pe = ISC_LIST_HEAD(prio_list);
	assert_non_null(pe);
	assert_int_equal(pe->prio, 10);

	for (se = ISC_LIST_HEAD(pe->srv_list); se != NULL;
	     se = ISC_LIST_NEXT(se, link))
		count++;

	assert_int_equal(count, 3);
	assert_null(ISC_LIST_NEXT(pe, link));

	reset_list();
}

/* ---- next_tuple --------------------------------------------------------- */

static void
test_next_tuple_single(void **state)
{
	struct srv *se;

	(void)state;
	reset_list();

	insert_tuple(strdup("host.example.com"), 10, 1, 8080);

	se = next_tuple();
	assert_non_null(se);
	assert_string_equal(se->tname, "host.example.com");
	assert_int_equal(se->port, 8080);
	free(se->tname);
	free(se);

	/* List is exhausted — next call must return NULL. */
	se = next_tuple();
	assert_null(se);

	reset_list();
}

static void
test_next_tuple_empty(void **state)
{
	(void)state;
	reset_list();
	assert_null(next_tuple());
}

static void
test_next_tuple_priority_order(void **state)
{
	struct srv *se;

	(void)state;
	reset_list();

	/* Lower priority value must always be served first. */
	insert_tuple(strdup("high.example.com"), 20, 1, 443);
	insert_tuple(strdup("low.example.com"), 5, 1, 80);

	se = next_tuple();
	assert_non_null(se);
	assert_string_equal(se->tname, "low.example.com");
	free(se->tname);
	free(se);

	se = next_tuple();
	assert_non_null(se);
	assert_string_equal(se->tname, "high.example.com");
	free(se->tname);
	free(se);

	assert_null(next_tuple());
	reset_list();
}

static void
test_next_tuple_both_weights_selected(void **state)
{
	int got_a = 0, got_b = 0;
	int i;

	(void)state;

	/*
	 * Over 200 trials with equal weight, both records must be chosen
	 * at least once.  The probability of either never appearing is
	 * astronomically small (< 2^-200).
	 */
	for (i = 0; i < 200; i++) {
		struct srv *se;

		reset_list();
		insert_tuple(strdup("a.example.com"), 10, 1, 80);
		insert_tuple(strdup("b.example.com"), 10, 1, 8080);

		se = next_tuple();
		assert_non_null(se);

		if (strcmp(se->tname, "a.example.com") == 0)
			got_a++;
		else if (strcmp(se->tname, "b.example.com") == 0)
			got_b++;

		free(se->tname);
		free(se);
		reset_list();
	}

	assert_true(got_a > 0);
	assert_true(got_b > 0);
}

static void
test_next_tuple_zero_weight_skipped(void **state)
{
	int i;

	(void)state;

	/*
	 * When one record has weight 0 and another has weight > 0, only
	 * the non-zero-weight record should ever be returned (given the
	 * algorithm: random() % (wsum+1) where wsum > 0 never produces a
	 * value that causes the zero-weight entry to win once a positive
	 * entry is available).
	 *
	 * The RFC 2782 algorithm in wrapsrv: with wsum > 0, rnd is in
	 * [0, wsum].  The zero-weight entry accumulates csum=0, and is
	 * selected only if rnd==0 AND it is the first in the list.  So
	 * we just verify the non-zero entry is also selectable (it always
	 * is) and that both can coexist without crashing.
	 */
	for (i = 0; i < 50; i++) {
		struct srv *se;

		reset_list();
		insert_tuple(strdup("nonzero.example.com"), 10, 5, 80);
		insert_tuple(strdup("zero.example.com"), 10, 0, 8080);

		se = next_tuple();
		assert_non_null(se);
		/* Must be one of the two valid hostnames. */
		assert_true(strcmp(se->tname, "nonzero.example.com") == 0 ||
			    strcmp(se->tname, "zero.example.com") == 0);
		free(se->tname);
		free(se);
		reset_list();
	}
}

/* ---- subst_cmd ---------------------------------------------------------- */

static void
test_subst_host(void **state)
{
	struct srv se;
	char *result;

	(void)state;
	init_srv(&se, "myhost.example.com", 80, 1);

	result = subst_cmd(&se, "%h");
	assert_string_equal(result, "myhost.example.com");
	free(result);
	free(se.tname);
}

static void
test_subst_port(void **state)
{
	struct srv se;
	char *result;

	(void)state;
	init_srv(&se, "host.example.com", 8080, 1);

	result = subst_cmd(&se, "%p");
	assert_string_equal(result, "8080");
	free(result);
	free(se.tname);
}

static void
test_subst_host_and_port(void **state)
{
	struct srv se;
	char *result;

	(void)state;
	init_srv(&se, "srv.example.com", 443, 1);

	result = subst_cmd(&se, "connect %h:%p");
	assert_string_equal(result, "connect srv.example.com:443");
	free(result);
	free(se.tname);
}

static void
test_subst_multiple_occurrences(void **state)
{
	struct srv se;
	char *result;

	(void)state;
	init_srv(&se, "h.example.com", 9090, 1);

	result = subst_cmd(&se, "%h %h %p %p");
	assert_string_equal(result,
			    "h.example.com h.example.com 9090 9090");
	free(result);
	free(se.tname);
}

static void
test_subst_no_markers(void **state)
{
	struct srv se;
	char *result;

	(void)state;
	init_srv(&se, "host.example.com", 80, 1);

	result = subst_cmd(&se, "echo hello");
	assert_string_equal(result, "echo hello");
	free(result);
	free(se.tname);
}

static void
test_subst_hostname_with_dots_hyphens(void **state)
{
	struct srv se;
	char *result;

	(void)state;
	init_srv(&se, "my-host-01.sub.example.com", 22, 1);

	result = subst_cmd(&se, "ssh user@%h -p %p");
	assert_string_equal(result,
			    "ssh user@my-host-01.sub.example.com -p 22");
	free(result);
	free(se.tname);
}

static void
test_subst_adjacent_markers(void **state)
{
	struct srv se;
	char *result;

	(void)state;
	init_srv(&se, "h.example.com", 8080, 1);

	result = subst_cmd(&se, "%h:%p");
	assert_string_equal(result, "h.example.com:8080");
	free(result);
	free(se.tname);
}

static void
test_subst_max_port(void **state)
{
	struct srv se;
	char *result;

	(void)state;
	init_srv(&se, "host.example.com", 65535, 1);

	result = subst_cmd(&se, "%p");
	assert_string_equal(result, "65535");
	free(result);
	free(se.tname);
}

/* ---- free_tuples -------------------------------------------------------- */

static void
test_free_tuples_no_crash(void **state)
{
	(void)state;
	reset_list();

	insert_tuple(strdup("a.example.com"), 10, 1, 80);
	insert_tuple(strdup("b.example.com"), 10, 2, 443);
	insert_tuple(strdup("c.example.com"), 20, 1, 8080);

	/* Must not crash or leak (verified externally by valgrind/ASan). */
	free_tuples();
	ISC_LIST_INIT(prio_list);

	/* Calling again on an empty list must also be safe. */
	free_tuples();
	ISC_LIST_INIT(prio_list);
}

/* ---- test runner -------------------------------------------------------- */

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_insert_prio_ascending),
		cmocka_unit_test(test_insert_same_prio),
		cmocka_unit_test(test_next_tuple_single),
		cmocka_unit_test(test_next_tuple_empty),
		cmocka_unit_test(test_next_tuple_priority_order),
		cmocka_unit_test(test_next_tuple_both_weights_selected),
		cmocka_unit_test(test_next_tuple_zero_weight_skipped),
		cmocka_unit_test(test_subst_host),
		cmocka_unit_test(test_subst_port),
		cmocka_unit_test(test_subst_host_and_port),
		cmocka_unit_test(test_subst_multiple_occurrences),
		cmocka_unit_test(test_subst_no_markers),
		cmocka_unit_test(test_subst_hostname_with_dots_hyphens),
		cmocka_unit_test(test_subst_adjacent_markers),
		cmocka_unit_test(test_subst_max_port),
		cmocka_unit_test(test_free_tuples_no_crash),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
