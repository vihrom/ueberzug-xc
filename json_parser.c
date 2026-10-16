#include "json_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#include "jsmn.h"

typedef enum {
	JSON_MAX_TOKENS = 128,
	JSON_ACTION_BUF_SIZE = 32,
	STRING_BASE_DECIMAL = 10,
	HEX_RADIX = 16
} JsonParserConfig;

static int
json_skip_token(const jsmntok_t *tokens, int i)
{
	int next = i + 1;
	if (tokens[i].type == JSMN_OBJECT) {
		for (int n = 0; n < tokens[i].size; n++) {
			next = json_skip_token(tokens, next);
			next = json_skip_token(tokens, next);
		}
	} else if (tokens[i].type == JSMN_ARRAY) {
		for (int n = 0; n < tokens[i].size; n++) {
			next = json_skip_token(tokens, next);
		}
	}
	return next;
}

static char
decode_simple_escape(char c)
{
	switch (c) {
	case '"':
		return '"';
	case '\\':
		return '\\';
	case '/':
		return '/';
	case 'b':
		return '\b';
	case 'f':
		return '\f';
	case 'n':
		return '\n';
	case 'r':
		return '\r';
	case 't':
		return '\t';
	default:
		return c;
	}
}

static size_t
encode_utf8(unsigned int codepoint, char *dest, size_t dest_idx,
	    size_t dest_len)
{
	if (dest_idx + 3 >= dest_len)
		return 0;

	if (codepoint < 0x80) {
		dest[dest_idx++] = (char)codepoint;
	} else if (codepoint < 0x800) {
		dest[dest_idx++] = (char)(0xC0 | (codepoint >> 6));
		dest[dest_idx++] = (char)(0x80 | (codepoint & 0x3F));
	} else if (codepoint < 0x10000) {
		dest[dest_idx++] = (char)(0xE0 | (codepoint >> 12));
		dest[dest_idx++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
		dest[dest_idx++] = (char)(0x80 | (codepoint & 0x3F));
	} else {
		dest[dest_idx++] = (char)(0xF0 | (codepoint >> 18));
		dest[dest_idx++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
		dest[dest_idx++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
		dest[dest_idx++] = (char)(0x80 | (codepoint & 0x3F));
	}
	return dest_idx;
}

static const char *
decode_unicode_escape(const char *p, const char *end, char *dest,
		      size_t *dest_idx, size_t dest_len)
{
	if (p + 4 >= end)
		return p;

	char hex[5] = { p[1], p[2], p[3], p[4], '\0' };
	unsigned int codepoint = (unsigned int)strtol(hex, NULL, HEX_RADIX);
	p += 4;

	/* Handle UTF-16 surrogate pairs for codepoints above U+FFFF */
	if (codepoint >= 0xD800 && codepoint <= 0xDBFF && p + 6 < end &&
	    p[1] == '\\' && p[2] == 'u') {
		p += 2;
		char hex2[5] = { p[1], p[2], p[3], p[4], '\0' };
		unsigned int codepoint2 =
			(unsigned int)strtol(hex2, NULL, HEX_RADIX);
		p += 4;
		if (codepoint2 >= 0xDC00 && codepoint2 <= 0xDFFF) {
			codepoint = 0x10000 + ((codepoint - 0xD800) << 10) +
				    (codepoint2 - 0xDC00);
		}
	}

	*dest_idx = encode_utf8(codepoint, dest, *dest_idx, dest_len);
	return p;
}

static void
decode_json_string(const char *json, const jsmntok_t *tok, char *dest,
		   size_t dest_len)
{
	if (tok->type != JSMN_STRING || dest_len == 0) {
		if (dest_len > 0)
			dest[0] = '\0';
		return;
	}

	const char *p = json + tok->start;
	const char *end = json + tok->end;
	size_t dest_idx = 0;

	while (p < end && dest_idx < dest_len - 1) {
		if (*p == '\\' && p + 1 < end) {
			p++;
			if (*p == 'u') {
				p = decode_unicode_escape(p, end, dest,
							  &dest_idx, dest_len);
			} else {
				dest[dest_idx++] = decode_simple_escape(*p);
			}
		} else {
			dest[dest_idx++] = *p;
		}
		p++;
	}
	dest[dest_idx] = '\0';
}

static int
token_to_int(const char *json, const jsmntok_t *tok, int *val_out)
{
	if (tok->type != JSMN_PRIMITIVE && tok->type != JSMN_STRING)
		return 0;

	char temp[32] = { 0 };
	int len = tok->end - tok->start;
	if (len <= 0 || len >= (int)sizeof(temp))
		return 0;

	memcpy(temp, json + tok->start, len);
	temp[len] = '\0';

	char *endptr = NULL;
	long val = strtol(temp, &endptr, STRING_BASE_DECIMAL);
	if (endptr == temp)
		return 0;

	*val_out = (int)val;
	return 1;
}

static void
parse_field_value(const char *json, const jsmntok_t *val_tok, const char *key,
		  Command *cmd, char *action_buf)
{
	if (strcmp(key, "action") == 0) {
		decode_json_string(json, val_tok, action_buf,
				   JSON_ACTION_BUF_SIZE);
	} else if (strcmp(key, "path") == 0) {
		decode_json_string(json, val_tok, cmd->path, sizeof(cmd->path));
	} else if (strcmp(key, "x") == 0 || strcmp(key, "max_x") == 0 ||
		   strcmp(key, "dx") == 0) {
		token_to_int(json, val_tok, &cmd->x);
	} else if (strcmp(key, "y") == 0 || strcmp(key, "max_y") == 0 ||
		   strcmp(key, "dy") == 0) {
		token_to_int(json, val_tok, &cmd->y);
	} else if (strcmp(key, "width") == 0 || strcmp(key, "max_width") == 0 ||
		   strcmp(key, "dw") == 0) {
		token_to_int(json, val_tok, &cmd->width);
	} else if (strcmp(key, "height") == 0 ||
		   strcmp(key, "max_height") == 0 || strcmp(key, "dh") == 0) {
		token_to_int(json, val_tok, &cmd->height);
	}
}

int
parse_json_line(const char *line, Command *cmd)
{
	jsmn_parser parser;
	jsmntok_t tokens[JSON_MAX_TOKENS] = { 0 };
	char action[JSON_ACTION_BUF_SIZE] = { 0 };

	if (!line || !cmd)
		return 0;

	memset(cmd, 0, sizeof(Command));
	cmd->action = ACTION_UNKNOWN;

	jsmn_init(&parser);
	int token_count = jsmn_parse(&parser, line, strlen(line), tokens,
				     sizeof(tokens) / sizeof(tokens[0]));
	if (token_count < 0 || tokens[0].type != JSMN_OBJECT)
		return 0;

	int i = 1;
	for (int r = 0; r < tokens[0].size && i < token_count; r++) {
		jsmntok_t *key_tok = &tokens[i];
		jsmntok_t *val_tok = &tokens[i + 1];

		if (key_tok->type == JSMN_STRING) {
			char key_str[64] = { 0 };
			int key_len = key_tok->end - key_tok->start;
			if (key_len > 0 && key_len < (int)sizeof(key_str)) {
				memcpy(key_str, line + key_tok->start, key_len);
				key_str[key_len] = '\0';
				parse_field_value(line, val_tok, key_str, cmd,
						  action);
			}
		}

		i = json_skip_token(tokens, i);
		i = json_skip_token(tokens, i);
	}

	if (strcmp(action, "add") == 0) {
		cmd->action = ACTION_ADD;
		if (cmd->path[0] == '\0')
			return 0;
	} else if (strcmp(action, "remove") == 0) {
		cmd->action = ACTION_REMOVE;
		return 1;
	} else {
		return 0;
	}

	return 1;
}

void
free_command(Command *cmd)
{
	(void)cmd;
}
