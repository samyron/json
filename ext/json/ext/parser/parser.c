#include <ruby.h>
#include <ruby/encoding.h>

typedef struct {
    const uint8_t *cursor;
    const uint8_t *end;
} j2_parser_t;

static inline void
j2_eat_whitespace(j2_parser_t *parser) {
    while (parser->cursor < parser->end) {
        switch (*parser->cursor) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
                parser->cursor++;
                break;
            default:
                return;
        }
    }
}

static VALUE
j2_parse_element(j2_parser_t *parser) {
    j2_eat_whitespace(parser);
    if (parser->cursor >= parser->end) {
        rb_raise(rb_eRuntimeError, "unexpected end of input");
    }

    switch (*parser->cursor) {
        case 'n':
            if ((parser->end - parser->cursor >= 4) && (memcmp(parser->cursor, "null", 4) == 0)) {
                parser->cursor += 4;
                return Qnil;
            }

            rb_raise(rb_eRuntimeError, "unexpected character");
            break;
        case 't':
            if ((parser->end - parser->cursor >= 4) && (memcmp(parser->cursor, "true", 4) == 0)) {
                parser->cursor += 4;
                return Qtrue;
            }

            rb_raise(rb_eRuntimeError, "unexpected character");
            break;
        case 'f':
            if ((parser->end - parser->cursor >= 5) && (memcmp(parser->cursor, "false", 5) == 0)) {
                parser->cursor += 5;
                return Qfalse;
            }

            rb_raise(rb_eRuntimeError, "unexpected character");
            break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': {
            // /\A-?(0|[1-9]\d*)(\.\d+)?([Ee][-+]?\d+)?/
            const uint8_t *start = parser->cursor;
            while ((parser->cursor < parser->end) && (*parser->cursor >= '0') && (*parser->cursor <= '9')) {
                parser->cursor++;
            }

            if ((parser->cursor < parser->end) && (*parser->cursor == '.')) {
                parser->cursor++;
                while ((parser->cursor < parser->end) && (*parser->cursor >= '0') && (*parser->cursor <= '9')) {
                    parser->cursor++;
                }
            }

            if ((parser->cursor < parser->end) && ((*parser->cursor == 'e') || (*parser->cursor == 'E'))) {
                parser->cursor++;
                if ((parser->cursor < parser->end) && ((*parser->cursor == '+') || (*parser->cursor == '-'))) {
                    parser->cursor++;
                }

                while ((parser->cursor < parser->end) && (*parser->cursor >= '0') && (*parser->cursor <= '9')) {
                    parser->cursor++;
                }
            }

            return rb_cstr_to_inum((const char *) start, (int) (parser->cursor - start), 10);
        }
        case '"': {
            // %r{\A"[^"\\\t\n\x00]*(?:\\[bfnrtu\\/"][^"\\]*)*"}
            parser->cursor++;
            const uint8_t *start = parser->cursor;

            while (parser->cursor < parser->end) {
                if (*parser->cursor == '"') {
                    VALUE string = rb_enc_str_new((const char *) start, parser->cursor - start, rb_utf8_encoding());
                    parser->cursor++;
                    return string;
                } else if (*parser->cursor == '\\') {
                    // Parse escape sequence
                    parser->cursor++;
                }

                parser->cursor++;
            }

            rb_raise(rb_eRuntimeError, "unexpected end of input");
            break;
        }
        case '[': {
            VALUE array = rb_ary_new();
            parser->cursor++;

            j2_eat_whitespace(parser);
            if ((parser->cursor < parser->end) && (*parser->cursor == ']')) {
                parser->cursor++;
                return array;
            }

            while (parser->cursor < parser->end) {
                VALUE element = j2_parse_element(parser);
                rb_ary_push(array, element);

                switch (*parser->cursor) {
                    case ',':
                        parser->cursor++;
                        break;
                    case ']':
                        parser->cursor++;
                        return array;
                    default:
                        rb_raise(rb_eRuntimeError, "expected ',' or ']' after array value");
                }
            }

            rb_raise(rb_eRuntimeError, "unexpected end of input");
            break;
        }
        case '{': {
            parser->cursor++;
            j2_eat_whitespace(parser);

            if ((parser->cursor < parser->end) && (*parser->cursor == '}')) {
                parser->cursor++;
                return rb_hash_new();
            }

            VALUE elements = rb_ary_new();
            while (parser->cursor < parser->end) {
                j2_eat_whitespace(parser);
                if (*parser->cursor != '"') {
                    rb_raise(rb_eRuntimeError, "expected object key");
                }

                VALUE key = j2_parse_element(parser);
                j2_eat_whitespace(parser);

                if ((parser->cursor >= parser->end) || (*parser->cursor != ':')) {
                    rb_raise(rb_eRuntimeError, "expected ':' after object key");
                }
                parser->cursor++;

                VALUE value = j2_parse_element(parser);
                VALUE pair[2] = { key, value };
                rb_ary_cat(elements, pair, 2);

                j2_eat_whitespace(parser);
                switch (*parser->cursor) {
                    case ',':
                        parser->cursor++;
                        break;
                    case '}': {
                        parser->cursor++;
                        VALUE value = rb_hash_new_capa(RARRAY_LEN(elements));
                        rb_hash_bulk_insert(RARRAY_LEN(elements), RARRAY_CONST_PTR(elements), value);
                        return value;
                    }
                    default:
                        rb_raise(rb_eRuntimeError, "expected ',' or '}' after object value");
                }
            }

            rb_raise(rb_eRuntimeError, "unexpected end of input");
            break;
        }
        default:
            rb_raise(rb_eRuntimeError, "unexpected character");
            break;
    }

    rb_raise(rb_eRuntimeError, "unexpected character");
}

static VALUE
j2_parse(VALUE self, VALUE value) {
    Check_Type(value, T_STRING);

    const uint8_t *start = (const uint8_t *) RSTRING_PTR(value);
    j2_parser_t parser = {
        .cursor = start,
        .end = start + RSTRING_LEN(value)
    };

    return j2_parse_element(&parser);
}

void
Init_json2(void) {
    VALUE rb_cJSON2 = rb_define_module("JSON2");
    rb_define_singleton_method(rb_cJSON2, "parse", j2_parse, 1);
}
