#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_cache.h"
#include "json_parser.h"

#define TEST_ASSERT(cond)                                                  \
	do {                                                               \
		if (!(cond)) {                                             \
			fprintf(stderr, "ASSERTION FAILED: %s at %s:%d\n", \
				#cond, __FILE__, __LINE__);                \
			exit(1);                                           \
		}                                                          \
	} while (0)

void
test_json_parse_add(void)
{
	const char *line =
		"{\"action\":\"add\",\"path\":\"/tmp/"
		"test.png\",\"x\":10,\"y\":20,\"width\":100,\"height\":200}";
	Command cmd;

	TEST_ASSERT(parse_json_line(line, &cmd) == 1);
	TEST_ASSERT(cmd.action == ACTION_ADD);
	TEST_ASSERT(strcmp(cmd.path, "/tmp/test.png") == 0);
	TEST_ASSERT(cmd.x == 10);
	TEST_ASSERT(cmd.y == 20);
	TEST_ASSERT(cmd.width == 100);
	TEST_ASSERT(cmd.height == 200);

	printf("[OK] test_json_parse_add passed\n");
}

void
test_json_parse_remove(void)
{
	const char *line = "{\"action\":\"remove\"}";
	Command cmd;

	TEST_ASSERT(parse_json_line(line, &cmd) == 1);
	TEST_ASSERT(cmd.action == ACTION_REMOVE);

	printf("[OK] test_json_parse_remove passed\n");
}

void
test_hash_config(void)
{
	CacheMeta meta1 = { .mtime = 12345678, .width = 100, .height = 200 };
	CacheMeta meta2 = { .mtime = 12345678, .width = 100, .height = 200 };
	CacheMeta meta3 = { .mtime = 12345678, .width = 101, .height = 200 };

	unsigned long h1 = hash_config("/tmp/test.png", &meta1);
	unsigned long h2 = hash_config("/tmp/test.png", &meta2);
	unsigned long h3 = hash_config("/tmp/test.png", &meta3);

	TEST_ASSERT(h1 == h2);
	TEST_ASSERT(h1 != h3);

	printf("[OK] test_hash_config passed\n");
}

void
test_parse_fallback_dimensions(void)
{
	const char *line = "{\"action\":\"add\",\"path\":\"/tmp/"
			   "test.png\",\"max_width\":150,\"max_height\":250}";
	Command cmd;

	TEST_ASSERT(parse_json_line(line, &cmd) == 1);
	TEST_ASSERT(cmd.width == 150);
	TEST_ASSERT(cmd.height == 250);

	printf("[OK] test_parse_fallback_dimensions passed\n");
}

void
test_json_parse_unicode_path(void)
{
	const char *line =
		"{\"action\":\"add\",\"path\":\"/tmp/\\u043a\\u0430\\u0440\\u0442\\u0438\\u043d\\u043a\\u0430.png\",\"width\":100,\"height\":100}";
	Command cmd;

	TEST_ASSERT(parse_json_line(line, &cmd) == 1);
	TEST_ASSERT(cmd.action == ACTION_ADD);
	TEST_ASSERT(strcmp(cmd.path, "/tmp/картинка.png") == 0);

	printf("[OK] test_json_parse_unicode_path passed\n");
}

void
test_json_parse_nested_and_extra(void)
{
	const char *line =
		"{\"action\":\"add\",\"meta\":{\"path\":\"/wrong.png\"},\"path\":\"/real.png\",\"width\":80,\"height\":60}";
	Command cmd;

	TEST_ASSERT(parse_json_line(line, &cmd) == 1);
	TEST_ASSERT(strcmp(cmd.path, "/real.png") == 0);

	printf("[OK] test_json_parse_nested_and_extra passed\n");
}

int
main(void)
{
	printf("=== Running Unit Tests ===\n");
	test_json_parse_add();
	test_json_parse_remove();
	test_hash_config();
	test_parse_fallback_dimensions();
	test_json_parse_unicode_path();
	test_json_parse_nested_and_extra();
	printf("All unit tests passed successfully!\n");
	return 0;
}
