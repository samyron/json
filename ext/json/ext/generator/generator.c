#include "ruby.h"
#include "../fbuffer/fbuffer.h"

#include <math.h>
#include <ctype.h>

#include "extconf.h"
#include "simd.h"

/* ruby api and some helpers */

typedef struct JSON_Generator_StateStruct {
    VALUE indent;
    VALUE space;
    VALUE space_before;
    VALUE object_nl;
    VALUE array_nl;

    long max_nesting;
    long depth;
    long buffer_initial_length;

    bool allow_nan;
    bool ascii_only;
    bool script_safe;
    bool strict;
} JSON_Generator_State;

#ifndef RB_UNLIKELY
#define RB_UNLIKELY(cond) (cond)
#endif

static VALUE mJSON, cState, mString_Extend, eGeneratorError, eNestingError, Encoding_UTF_8;

static ID i_to_s, i_to_json, i_new, i_pack, i_unpack, i_create_id, i_extend, i_encode;
static ID sym_indent, sym_space, sym_space_before, sym_object_nl, sym_array_nl, sym_max_nesting, sym_allow_nan,
          sym_ascii_only, sym_depth, sym_buffer_initial_length, sym_script_safe, sym_escape_slash, sym_strict;

static void (*convert_UTF8_to_JSON_impl)(FBuffer *, VALUE, const unsigned char escape_table[256]);

#ifdef ENABLE_SIMD
static void (*convert_UTF8_to_JSON_simd_kernel)(FBuffer *out_buffer, const char * ptr, unsigned long len, unsigned long *_beg, unsigned long *_pos, const char *hexdig, char scratch[12], const unsigned char escape_table[256]);
#endif

#define GET_STATE_TO(self, state) \
    TypedData_Get_Struct(self, JSON_Generator_State, &JSON_Generator_State_type, state)

#define GET_STATE(self)                       \
    JSON_Generator_State *state;              \
    GET_STATE_TO(self, state)

struct generate_json_data;

typedef void (*generator_func)(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);

struct generate_json_data {
    FBuffer *buffer;
    VALUE vstate;
    JSON_Generator_State *state;
    VALUE obj;
    generator_func func;
};

static VALUE cState_from_state_s(VALUE self, VALUE opts);
static VALUE cState_partial_generate(VALUE self, VALUE obj, generator_func, VALUE io);
static void generate_json(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
static void generate_json_object(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
static void generate_json_array(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
static void generate_json_string(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
static void generate_json_null(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
static void generate_json_false(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
static void generate_json_true(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
#ifdef RUBY_INTEGER_UNIFICATION
static void generate_json_integer(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
#endif
static void generate_json_fixnum(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
static void generate_json_bignum(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);
static void generate_json_float(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj);

static int usascii_encindex, utf8_encindex, binary_encindex;

#ifdef RBIMPL_ATTR_NORETURN
RBIMPL_ATTR_NORETURN()
#endif
static void raise_generator_error_str(VALUE invalid_object, VALUE str)
{
    VALUE exc = rb_exc_new_str(eGeneratorError, str);
    rb_ivar_set(exc, rb_intern("@invalid_object"), invalid_object);
    rb_exc_raise(exc);
}

#ifdef RBIMPL_ATTR_NORETURN
RBIMPL_ATTR_NORETURN()
#endif
#ifdef RBIMPL_ATTR_FORMAT
RBIMPL_ATTR_FORMAT(RBIMPL_PRINTF_FORMAT, 2, 3)
#endif
static void raise_generator_error(VALUE invalid_object, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    VALUE str = rb_vsprintf(fmt, args);
    va_end(args);
    raise_generator_error_str(invalid_object, str);
}

// 0 - single byte char that don't need to be escaped.
// (x | 8) - char that needs to be escaped.
static const unsigned char CHAR_LENGTH_MASK = 7;

static const unsigned char escape_table[256] = {
    // ASCII Control Characters
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    // ASCII Characters
     0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // '"'
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, // '\\'
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const unsigned char ascii_only_escape_table[256] = {
    // ASCII Control Characters
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    // ASCII Characters
     0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // '"'
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, // '\\'
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // Continuation byte
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    // First byte of a  2-byte code point
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    // First byte of a 3-byte code point
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    //First byte of a 4+ byte code point
     4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 9, 9,
};

static const unsigned char script_safe_escape_table[256] = {
    // ASCII Control Characters
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    // ASCII Characters
     0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, // '"' and '/'
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, // '\\'
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // Continuation byte
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    // First byte of a 2-byte code point
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    // First byte of a 3-byte code point
     3, 3,11, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, // 0xE2 is the start of \u2028 and \u2029
    //First byte of a 4+ byte code point
     4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 9, 9,
};

/* Converts in_string to a JSON string (without the wrapping '"'
 * characters) in FBuffer out_buffer.
 *
 * Character are JSON-escaped according to:
 *
 * - Always: ASCII control characters (0x00-0x1F), dquote, and
 *   backslash.
 *
 * - If out_ascii_only: non-ASCII characters (>0x7F)
 *
 * - If script_safe: forwardslash (/), line separator (U+2028), and
 *   paragraph separator (U+2029)
 *
 * Everything else (should be UTF-8) is just passed through and
 * appended to the result.
 */
#define FLUSH_POS(bytes) if (pos > beg) { fbuffer_append(out_buffer, &ptr[beg], pos - beg); } pos += bytes; beg = pos;
#define PROCESS_BYTE if (RB_UNLIKELY(ch_len)) { \
                switch (ch_len) { \
                    case 9: { \
                        FLUSH_POS(1); \
                        switch (ch) { \
                            case '"':  fbuffer_append(out_buffer, "\\\"", 2); break; \
                            case '\\': fbuffer_append(out_buffer, "\\\\", 2); break; \
                            case '/':  fbuffer_append(out_buffer, "\\/", 2); break; \
                            case '\b': fbuffer_append(out_buffer, "\\b", 2); break; \
                            case '\f': fbuffer_append(out_buffer, "\\f", 2); break; \
                            case '\n': fbuffer_append(out_buffer, "\\n", 2); break; \
                            case '\r': fbuffer_append(out_buffer, "\\r", 2); break; \
                            case '\t': fbuffer_append(out_buffer, "\\t", 2); break; \
                            default: { \
                                scratch[2] = '0'; \
                                scratch[3] = '0'; \
                                scratch[4] = hexdig[(ch >> 4) & 0xf]; \
                                scratch[5] = hexdig[ch & 0xf]; \
                                fbuffer_append(out_buffer, scratch, 6); \
                                break; \
                            } \
                        } \
                        break; \
                    } \
                    case 11: { \
                        unsigned char b2 = ptr[pos + 1]; \
                        if (RB_UNLIKELY(b2 == 0x80)) { \
                            unsigned char b3 = ptr[pos + 2]; \
                            if (b3 == 0xA8) { \
                                FLUSH_POS(3); \
                                fbuffer_append(out_buffer, "\\u2028", 6); \
                                break; \
                            } else if (b3 == 0xA9) { \
                                FLUSH_POS(3); \
                                fbuffer_append(out_buffer, "\\u2029", 6); \
                                break; \
                            } \
                        } \
                        ch_len = 3;  \
                    } \
                    default: \
                        pos += ch_len; \
                        break; \
                } \
            } else { \
                pos++; \
            }

#ifdef ENABLE_SIMD
static void convert_UTF8_to_JSON_simd(FBuffer *out_buffer, VALUE str, const unsigned char escape_table[256])
{
    const char *hexdig = "0123456789abcdef";
    char scratch[12] = { '\\', 'u', 0, 0, 0, 0, '\\', 'u' };

    const char *ptr = RSTRING_PTR(str);
    unsigned long len = RSTRING_LEN(str);

    unsigned long beg = 0, pos = 0;

    convert_UTF8_to_JSON_simd_kernel(out_buffer, ptr, len, &beg, &pos, hexdig, scratch, escape_table);
    
    while (pos < len) {
        unsigned char ch = ptr[pos];
        unsigned char ch_len = escape_table[ch];
        PROCESS_BYTE;
    }

    if (beg < len) {
        fbuffer_append(out_buffer, &ptr[beg], len - beg);
    }

    RB_GC_GUARD(str);
}
#endif 

#ifdef HAVE_SIMD_NEON

void convert_UTF8_to_JSON_simd_kernel_neon(FBuffer *out_buffer, const char * ptr, unsigned long len, unsigned long *_beg, unsigned long *_pos, const char *hexdig, char scratch[12], const unsigned char escape_table[256]) {
    unsigned long beg = *_beg, pos = *_pos;
        
    const uint8x16_t lower_bound = vdupq_n_u8(' '); 
    const uint8x16_t backslash   = vdupq_n_u8('\\');
    const uint8x16_t dblquote    = vdupq_n_u8('\"');

    if (escape_table == script_safe_escape_table) {
        /*
        * This works almost exactly the same as what is described above. The difference in this case comes after we know
        * there is a byte to be escaped. In the previous case, all bytes were handled the same way. In this case, however,
        * some bytes need to be handled differently. 
        * 
        * Since we know each byte in chunk can only match a single case, we logical AND each of the has_backslash,
        * has_dblquote, and has_forward_slash with a different bit (0x1, 0x2 and 0x4 respectively) and combine
        * the results with a logical OR. 
        * 
        * Now we loop over the result vector and switch on the particular pattern we just created. If we find a 
        * case we don't know, we simply lookup the byte in the script_safe_escape_table to determine the correct
        * action.
        */
        const uint8x16_t upper_bound     = vdupq_n_u8('~');
        const uint8x16_t forward_slash   = vdupq_n_u8('/');

        while (pos+16 < len) {
            uint8x16_t chunk             = vld1q_u8((const uint8_t*)&ptr[pos]);
            uint8x16_t too_low           = vcltq_u8(chunk, lower_bound);
            uint8x16_t too_high          = vcgtq_u8(chunk, upper_bound);

            uint8x16_t has_backslash     = vceqq_u8(chunk, backslash);
            uint8x16_t has_dblquote      = vceqq_u8(chunk, dblquote);
            uint8x16_t has_forward_slash = vceqq_u8(chunk, forward_slash);

            uint8x16_t needs_escape      = vorrq_u8(too_low, too_high);
            uint8x16_t has_escaped_char  = vorrq_u8(has_forward_slash, vorrq_u8(has_backslash, has_dblquote));
            needs_escape                 = vorrq_u8(needs_escape, has_escaped_char);

            if (vmaxvq_u8(needs_escape) == 0) {
                pos += 16;
                continue;
            }

            uint8x16_t tmp = vandq_u8(too_low, vdupq_n_u8(0x1));
            tmp = vorrq_u8(tmp, vandq_u8(has_backslash, vdupq_n_u8(0x2)));
            tmp = vorrq_u8(tmp, vandq_u8(has_dblquote, vdupq_n_u8(0x4)));
            tmp = vorrq_u8(tmp, vandq_u8(has_forward_slash, vdupq_n_u8(0x8)));

            uint8_t arr[16];
            vst1q_u8(arr, tmp);
            
            for (int i = 0; i < 16; ) {
                unsigned long start = pos;
                unsigned char ch = ptr[pos];
                unsigned char ch_len = arr[i];
                switch(ch_len) {
                    case 0x1:
                    case 0x2:
                    case 0x4:
                    case 0x8:
                        ch_len = 9;
                        break;
                    default:
                        ch_len = script_safe_escape_table[ch];
                }
                // This must remain in sync with the array `escape_table`.
                if (RB_UNLIKELY(ch_len)) {
                    PROCESS_BYTE;
                } else {
                    pos++;
                }

                i += (pos - start);
            }
        }
    } else {
        /*
        * The code below implements an SIMD-based algorithm to determine if N bytes at a time
        * need to be escaped. 
        * 
        * Assume the ptr = "Te\sting!" (the double quotes are included in the string)
        * 
        * The explanination will be limited to the first 8 bytes of the string for simplicity. However
        * the vector insructions may work on larger vectors.
        * 
        * First, we load three constants 'lower_bound', 'backslash' and 'dblquote" in vector registers.
        * 
        * lower_bound: [20 20 20 20 20 20 20 20] 
        * backslash:   [5C 5C 5C 5C 5C 5C 5C 5C] 
        * dblquote:    [22 22 22 22 22 22 22 22] 
        * 
        * Next we load the first chunk of the ptr: 
        * [22 54 65 5C 73 74 69 6E] ("  T  e  \  s  t  i  n)
        * 
        * First we check if any byte in chunk is less than 32 (0x20). This returns the following vector
        * as no bytes are less than 32 (0x20):
        * [0 0 0 0 0 0 0 0]
        * 
        * Next, we check if any byte in chunk is equal to a backslash:
        * [0 0 0 FF 0 0 0 0]
        * 
        * Finally we check if any byte in chunk is equal to a double quote:
        * [FF 0 0 0 0 0 0 0] 
        * 
        * Now we have three vectors where each byte indicates if the corresponding byte in chunk
        * needs to be escaped. We combine these vectors with a series of logical OR instructions.
        * This is the needs_escape vector and it is equal to:
        * [FF 0 0 FF 0 0 0 0] 
        * 
        * For ARM Neon specifically, we check if the maximum number in the vector is 0. The maximum of
        * the needs_escape vector is FF. Therefore, we know there is at least one byte that needs to be
        * escaped.
        * 
        * If the maximum of the needs_escape vector is 0, none of the bytes need to be escaped and
        * we advance pos by the width of the vector.
        * 
        * To determine how to escape characters, we look at each value in the needs_escape vector and take
        * the appropriate action.
        */
        while (pos+16 < len) {
            uint8x16_t chunk         = vld1q_u8((const uint8_t*)&ptr[pos]);
            uint8x16_t too_low       = vcltq_u8(chunk, lower_bound);
            uint8x16_t has_backslash = vceqq_u8(chunk, backslash);
            uint8x16_t has_dblquote  = vceqq_u8(chunk, dblquote);
            uint8x16_t needs_escape  = vorrq_u8(too_low, vorrq_u8(has_backslash, has_dblquote));

            if (vmaxvq_u8(needs_escape) == 0) {
                pos += 16;
                continue;
            }

            /*
            * TODO Consider making another type simd_vec_mask. The reason being on x86 we can use _mm_movemask_epi8
            * to get a mask rather than storing the vector to memory. 
            * 
            * We would need another function like simd_vec_mask_position_set(mask, pos) which returns true
            * if the bit/byte (implementation defined) at position 'pos' is non-zero.
            */

            uint8_t arr[16];
            vst1q_u8(arr, needs_escape);

            for (int i = 0; i < 16; i++) {
                unsigned char ch = ptr[pos];
                unsigned char ch_len = arr[i];
                
                // This must remain in sync with the array `escape_table`.
                if (RB_UNLIKELY(ch_len)) {
                    ch_len = 9;
                    PROCESS_BYTE;
                } else {
                    pos++;
                }
            }
        }
    }

    *_beg = beg;
    *_pos = pos;
}

#endif /* HAVE_SIMD_NEON */

#ifdef HAVE_SIMD_X86_64

#ifdef HAVE_TYPE___M128I
#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC target ("sse4")
#endif /* __GNUC__ */

#define _mm_cmpge_epu8(a, b) _mm_cmpeq_epi8(_mm_max_epu8(a, b), a)
#define _mm_cmple_epu8(a, b) _mm_cmpge_epu8(b, a)
#define _mm_cmpgt_epu8(a, b) _mm_xor_si128(_mm_cmple_epu8(a, b), _mm_set1_epi8(-1))
#define _mm_cmplt_epu8(a, b) _mm_cmpgt_epu8(b, a)

#ifdef __clang__
__attribute__((target("sse4.2")))
#endif /* __clang__ */
void convert_UTF8_to_JSON_simd_kernel_sse42(FBuffer *out_buffer, const char * ptr, unsigned long len, unsigned long *_beg, unsigned long *_pos, const char *hexdig, char scratch[12], const unsigned char escape_table[256]) {
    unsigned long beg = *_beg, pos = *_pos;

    if (escape_table == script_safe_escape_table) {
        /*
        * Again, this is basically a straight port of the ARM Neon version.
        */
        const __m128i lower_bound     = _mm_set1_epi8(' '); 
        const __m128i upper_bound     = _mm_set1_epi8('~');
        const __m128i backslash       = _mm_set1_epi8('\\');
        const __m128i dblquote        = _mm_set1_epi8('\"');
        const __m128i forward_slash   = _mm_set1_epi8('/');

        while (pos+16 < len) {
            __m128i chunk             = _mm_loadu_si128((__m128i const*)&ptr[pos]);
            __m128i too_low           = _mm_cmplt_epu8(chunk, lower_bound);
            __m128i too_high          = _mm_cmpgt_epu8(chunk, upper_bound);

            __m128i has_backslash     = _mm_cmpeq_epi8(chunk, backslash);
            __m128i has_dblquote      = _mm_cmpeq_epi8(chunk, dblquote);
            __m128i has_forward_slash = _mm_cmpeq_epi8(chunk, forward_slash);

            __m128i needs_escape      = _mm_or_si128(too_low, too_high);
            __m128i has_escaped_char  = _mm_or_si128(has_forward_slash, _mm_or_si128(has_backslash, has_dblquote));
            needs_escape              = _mm_or_si128(needs_escape, has_escaped_char);

            int needs_escape_mask     = _mm_movemask_epi8(needs_escape);
            if (needs_escape_mask == 0) {
                pos += 16;
                continue;
            }

            __m128i tmp = _mm_and_si128(too_low, _mm_set1_epi8(0x1));
            tmp = _mm_or_si128(tmp, _mm_and_si128(has_backslash, _mm_set1_epi8(0x2)));
            tmp = _mm_or_si128(tmp, _mm_and_si128(has_dblquote, _mm_set1_epi8(0x4)));
            tmp = _mm_or_si128(tmp, _mm_and_si128(has_forward_slash, _mm_set1_epi8(0x8)));

            uint8_t arr[16];
            _mm_storeu_si128((__m128i *) arr, tmp);

            for (int i = 0; i < 16; ) {
                unsigned long start = pos;
                unsigned char ch = ptr[pos];
                unsigned char ch_len = arr[i];
                switch(ch_len) {
                    case 0x1:
                    case 0x2:
                    case 0x4:
                    case 0x8:
                        ch_len = 9;
                        break;
                    default:
                        ch_len = script_safe_escape_table[ch];
                }
                // This must remain in sync with the array `escape_table`.
                if (RB_UNLIKELY(ch_len)) {
                    PROCESS_BYTE;
                } else {
                    pos++;
                }

                i += (pos - start);
            }
        }
    } else {
        /*
        * This is a straight port of the ARM Neon implementation to SSE4. This is 
        * likely not optimal for this instruction set. There is likely table lookup,
        * shuffle, gather, blend, etc. instructions that may perform significantly
        * better than what is implemented here.
        */

        const __m128i lower_bound = _mm_set1_epi8(' '); 
        const __m128i backslash   = _mm_set1_epi8('\\');
        const __m128i dblquote    = _mm_set1_epi8('\"');

        while (pos+16 < len) {
            __m128i chunk         = _mm_loadu_si128((__m128i const*)&ptr[pos]);
            __m128i too_low       = _mm_cmplt_epu8(chunk, lower_bound);
            __m128i has_backslash = _mm_cmpeq_epi8(chunk, backslash);
            __m128i has_dblquote  = _mm_cmpeq_epi8(chunk, dblquote);
            __m128i needs_escape  = _mm_or_si128(too_low, _mm_or_si128(has_backslash, has_dblquote));

            int needs_escape_mask = _mm_movemask_epi8(needs_escape);

            if (needs_escape_mask == 0) {
                pos += 16;
                continue;
            }

            for (int i = 0; i < 16; i++) {
                int bit = needs_escape_mask & (1 << i);
                unsigned char ch = ptr[pos];
                unsigned char ch_len = 0;
                
                // This must remain in sync with the array `escape_table`.
                if (RB_UNLIKELY(bit)) {
                    ch_len = 9;
                    PROCESS_BYTE;
                } else {
                    pos++;
                }   
            }
        }
    }

    *_beg = beg;
    *_pos = pos;
}

#ifdef __GNUC__
#pragma GCC pop_options
#endif /* __GNUC__ */
#endif /* HAVE_TYPE___M128I */

#ifdef HAVE_TYPE___M256I
#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC target ("avx2")
#endif /* __GNUC__ */

#define _mm256_cmpge_epu8(a, b) _mm256_cmpeq_epi8(_mm256_max_epu8(a, b), a)
#define _mm256_cmple_epu8(a, b) _mm256_cmpge_epu8(b, a)
#define _mm256_cmpgt_epu8(a, b) _mm256_xor_si256(_mm256_cmple_epu8(a, b), _mm256_set1_epi8(-1))
#define _mm256_cmplt_epu8(a, b) _mm256_cmpgt_epu8(b, a)

#ifdef __clang__
__attribute__((target("avx2")))
#endif /* __clang__ */
void convert_UTF8_to_JSON_simd_kernel_avx2(FBuffer *out_buffer, const char * ptr, unsigned long len, unsigned long *_beg, unsigned long *_pos, const char *hexdig, char scratch[12], const unsigned char escape_table[256]) {
    unsigned long beg = *_beg, pos = *_pos;

    const __m256i lower_bound = _mm256_set1_epi8(' '); 
    const __m256i backslash   = _mm256_set1_epi8('\\');
    const __m256i dblquote    = _mm256_set1_epi8('\"');

    if (escape_table == script_safe_escape_table) {
        /*
        * Again, this is basically a straight port of the ARM Neon version.
        */
        const __m256i upper_bound     = _mm256_set1_epi8('~');
        const __m256i forward_slash   = _mm256_set1_epi8('/');

        while (pos+32 < len) {
            __m256i chunk             = _mm256_loadu_si256((__m256i const*)&ptr[pos]);
            __m256i too_low           = _mm256_cmplt_epu8(chunk, lower_bound);
            __m256i too_high          = _mm256_cmpgt_epu8(chunk, upper_bound);

            __m256i has_backslash     = _mm256_cmpeq_epi8(chunk, backslash);
            __m256i has_dblquote      = _mm256_cmpeq_epi8(chunk, dblquote);
            __m256i has_forward_slash = _mm256_cmpeq_epi8(chunk, forward_slash);

            __m256i needs_escape      = _mm256_or_si256(too_low, too_high);
            __m256i has_escaped_char  = _mm256_or_si256(has_forward_slash, _mm256_or_si256(has_backslash, has_dblquote));
            needs_escape              = _mm256_or_si256(needs_escape, has_escaped_char);

            int needs_escape_mask     = _mm256_movemask_epi8(needs_escape);
            if (needs_escape_mask == 0) {
                pos += 32;
                continue;
            }

            __m256i tmp = _mm256_and_si256(too_low, _mm256_set1_epi8(0x1));
            tmp = _mm256_or_si256(tmp, _mm256_and_si256(has_backslash, _mm256_set1_epi8(0x2)));
            tmp = _mm256_or_si256(tmp, _mm256_and_si256(has_dblquote, _mm256_set1_epi8(0x4)));
            tmp = _mm256_or_si256(tmp, _mm256_and_si256(has_forward_slash, _mm256_set1_epi8(0x8)));

            uint8_t arr[32];
            _mm256_storeu_si256((__m256i *) arr, tmp);

            for (int i = 0; i < 32; ) {
                unsigned long start = pos;
                unsigned char ch = ptr[pos];
                unsigned char ch_len = arr[i];
                switch(ch_len) {
                    case 0x1:
                    case 0x2:
                    case 0x4:
                    case 0x8:
                        ch_len = 9;
                        break;
                    default:
                        ch_len = script_safe_escape_table[ch];
                }
                // This must remain in sync with the array `escape_table`.
                if (RB_UNLIKELY(ch_len)) {
                    PROCESS_BYTE;
                } else {
                    pos++;
                }

                i += (pos - start);
            }
        }
    } else {
        /*
        * This is a straight port of the ARM Neon implementation to SSE4. This is 
        * likely not optimal for this instruction set. There is likely table lookup,
        * shuffle, gather, blend, etc. instructions that may perform significantly
        * better than what is implemented here.
        */
        while (pos+32 < len) {
            __m256i chunk         = _mm256_loadu_si256((__m256i const*)&ptr[pos]);
            __m256i too_low       = _mm256_cmplt_epu8(chunk, lower_bound);
            __m256i has_backslash = _mm256_cmpeq_epi8(chunk, backslash);
            __m256i has_dblquote  = _mm256_cmpeq_epi8(chunk, dblquote);
            __m256i needs_escape  = _mm256_or_si256(too_low, _mm256_or_si256(has_backslash, has_dblquote));

            int needs_escape_mask = _mm256_movemask_epi8(needs_escape);

            if (needs_escape_mask == 0) {
                pos += 32;
                continue;
            }

            for (int i = 0; i < 32; i++) {
                int bit = needs_escape_mask & (1 << i);
                unsigned char ch = ptr[pos];
                unsigned char ch_len = 0;
                
                // This must remain in sync with the array `escape_table`.
                if (RB_UNLIKELY(bit)) {
                    ch_len = 9;
                    PROCESS_BYTE;
                } else {
                    pos++;
                }   
            }
        }
    }
    *_beg = beg;
    *_pos = pos;
}

#ifdef __GNUC__
#pragma GCC pop_options
#endif /* __GNUC__ */

#endif /* HAVE_TYPE___M256I */

#endif /* x86_64 support */


static void convert_UTF8_to_JSON(FBuffer *out_buffer, VALUE str, const unsigned char escape_table[256])
{
    const char *hexdig = "0123456789abcdef";
    char scratch[12] = { '\\', 'u', 0, 0, 0, 0, '\\', 'u' };

    const char *ptr = RSTRING_PTR(str);
    unsigned long len = RSTRING_LEN(str);

    unsigned long beg = 0, pos = 0;

    while (pos < len) {
        unsigned char ch = ptr[pos];
        unsigned char ch_len = escape_table[ch];
        /* JSON encoding */

        PROCESS_BYTE;
    }

    if (beg < len) {
        fbuffer_append(out_buffer, &ptr[beg], len - beg);
    }

    RB_GC_GUARD(str);
}

#undef PROCESS_BYTE

static void convert_UTF8_to_ASCII_only_JSON(FBuffer *out_buffer, VALUE str, const unsigned char escape_table[256])
{
    const char *hexdig = "0123456789abcdef";
    char scratch[12] = { '\\', 'u', 0, 0, 0, 0, '\\', 'u' };

    const char *ptr = RSTRING_PTR(str);
    unsigned long len = RSTRING_LEN(str);

    unsigned long beg = 0, pos = 0;

    while (pos < len) {
        unsigned char ch = ptr[pos];
        unsigned char ch_len = escape_table[ch];

        if (RB_UNLIKELY(ch_len)) { 
            switch (ch_len) { 
                case 9: { 
                    FLUSH_POS(1); 
                    switch (ch) { 
                        case '"':  fbuffer_append(out_buffer, "\\\"", 2); break; 
                        case '\\': fbuffer_append(out_buffer, "\\\\", 2); break; 
                        case '/':  fbuffer_append(out_buffer, "\\/", 2); break; 
                        case '\b': fbuffer_append(out_buffer, "\\b", 2); break; 
                        case '\f': fbuffer_append(out_buffer, "\\f", 2); break; 
                        case '\n': fbuffer_append(out_buffer, "\\n", 2); break; 
                        case '\r': fbuffer_append(out_buffer, "\\r", 2); break; 
                        case '\t': fbuffer_append(out_buffer, "\\t", 2); break; 
                        default: { 
                            scratch[2] = '0'; 
                            scratch[3] = '0'; 
                            scratch[4] = hexdig[(ch >> 4) & 0xf]; 
                            scratch[5] = hexdig[ch & 0xf]; 
                            fbuffer_append(out_buffer, scratch, 6); 
                            break; 
                        } 
                    } 
                    break; 
                } 
                default: { 
                    uint32_t wchar = 0; 
                    ch_len = ch_len & CHAR_LENGTH_MASK; 
 
                    switch(ch_len) { 
                        case 2: 
                            wchar = ptr[pos] & 0x1F; 
                            break; 
                        case 3: 
                            wchar = ptr[pos] & 0x0F; 
                            break; 
                        case 4: 
                            wchar = ptr[pos] & 0x07; 
                            break; 
                    } 
 
                    for (short i = 1; i < ch_len; i++) { 
                        wchar = (wchar << 6) | (ptr[pos+i] & 0x3F); 
                    } 
 
                    FLUSH_POS(ch_len); 
 
                    if (wchar <= 0xFFFF) { 
                        scratch[2] = hexdig[wchar >> 12]; 
                        scratch[3] = hexdig[(wchar >> 8) & 0xf]; 
                        scratch[4] = hexdig[(wchar >> 4) & 0xf]; 
                        scratch[5] = hexdig[wchar & 0xf]; 
                        fbuffer_append(out_buffer, scratch, 6); 
                    } else { 
                        uint16_t hi, lo; 
                        wchar -= 0x10000; 
                        hi = 0xD800 + (uint16_t)(wchar >> 10); 
                        lo = 0xDC00 + (uint16_t)(wchar & 0x3FF); 
 
                        scratch[2] = hexdig[hi >> 12]; 
                        scratch[3] = hexdig[(hi >> 8) & 0xf]; 
                        scratch[4] = hexdig[(hi >> 4) & 0xf]; 
                        scratch[5] = hexdig[hi & 0xf]; 
 
                        scratch[8] = hexdig[lo >> 12]; 
                        scratch[9] = hexdig[(lo >> 8) & 0xf]; 
                        scratch[10] = hexdig[(lo >> 4) & 0xf]; 
                        scratch[11] = hexdig[lo & 0xf]; 
 
                        fbuffer_append(out_buffer, scratch, 12); 
                    } 
 
                    break; 
                } 
            } 
        } else { 
            pos++; 
        } 
    }

    if (beg < len) {
        fbuffer_append(out_buffer, &ptr[beg], len - beg);
    }

    RB_GC_GUARD(str);
}

#undef FLUSH_POS

/*
 * Document-module: JSON::Ext::Generator
 *
 * This is the JSON generator implemented as a C extension. It can be
 * configured to be used by setting
 *
 *  JSON.generator = JSON::Ext::Generator
 *
 * with the method generator= in JSON.
 *
 */

/* Explanation of the following: that's the only way to not pollute
 * standard library's docs with GeneratorMethods::<ClassName> which
 * are uninformative and take a large place in a list of classes
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::Array
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::Bignum
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::FalseClass
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::Fixnum
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::Float
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::Hash
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::Integer
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::NilClass
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::Object
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::String
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::String::Extend
 * :nodoc:
 */

/*
 * Document-module: JSON::Ext::Generator::GeneratorMethods::TrueClass
 * :nodoc:
 */

/*
 * call-seq: to_json(state = nil)
 *
 * Returns a JSON string containing a JSON object, that is generated from
 * this Hash instance.
 * _state_ is a JSON::State object, that can also be used to configure the
 * produced JSON string output further.
 */
static VALUE mHash_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    VALUE Vstate = cState_from_state_s(cState, argc == 1 ? argv[0] : Qnil);
    return cState_partial_generate(Vstate, self, generate_json_object, Qfalse);
}

/*
 * call-seq: to_json(state = nil)
 *
 * Returns a JSON string containing a JSON array, that is generated from
 * this Array instance.
 * _state_ is a JSON::State object, that can also be used to configure the
 * produced JSON string output further.
 */
static VALUE mArray_to_json(int argc, VALUE *argv, VALUE self) {
    rb_check_arity(argc, 0, 1);
    VALUE Vstate = cState_from_state_s(cState, argc == 1 ? argv[0] : Qnil);
    return cState_partial_generate(Vstate, self, generate_json_array, Qfalse);
}

#ifdef RUBY_INTEGER_UNIFICATION
/*
 * call-seq: to_json(*)
 *
 * Returns a JSON string representation for this Integer number.
 */
static VALUE mInteger_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    VALUE Vstate = cState_from_state_s(cState, argc == 1 ? argv[0] : Qnil);
    return cState_partial_generate(Vstate, self, generate_json_integer, Qfalse);
}

#else
/*
 * call-seq: to_json(*)
 *
 * Returns a JSON string representation for this Integer number.
 */
static VALUE mFixnum_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    VALUE Vstate = cState_from_state_s(cState, argc == 1 ? argv[0] : Qnil);
    return cState_partial_generate(Vstate, self, generate_json_fixnum, Qfalse);
}

/*
 * call-seq: to_json(*)
 *
 * Returns a JSON string representation for this Integer number.
 */
static VALUE mBignum_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    VALUE Vstate = cState_from_state_s(cState, argc == 1 ? argv[0] : Qnil);
    return cState_partial_generate(Vstate, self, generate_json_bignum, Qfalse);
}
#endif

/*
 * call-seq: to_json(*)
 *
 * Returns a JSON string representation for this Float number.
 */
static VALUE mFloat_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    VALUE Vstate = cState_from_state_s(cState, argc == 1 ? argv[0] : Qnil);
    return cState_partial_generate(Vstate, self, generate_json_float, Qfalse);
}

/*
 * call-seq: String.included(modul)
 *
 * Extends _modul_ with the String::Extend module.
 */
static VALUE mString_included_s(VALUE self, VALUE modul) {
    VALUE result = rb_funcall(modul, i_extend, 1, mString_Extend);
    rb_call_super(1, &modul);
    return result;
}

/*
 * call-seq: to_json(*)
 *
 * This string should be encoded with UTF-8 A call to this method
 * returns a JSON string encoded with UTF16 big endian characters as
 * \u????.
 */
static VALUE mString_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    VALUE Vstate = cState_from_state_s(cState, argc == 1 ? argv[0] : Qnil);
    return cState_partial_generate(Vstate, self, generate_json_string, Qfalse);
}

/*
 * call-seq: to_json_raw_object()
 *
 * This method creates a raw object hash, that can be nested into
 * other data structures and will be generated as a raw string. This
 * method should be used, if you want to convert raw strings to JSON
 * instead of UTF-8 strings, e. g. binary data.
 */
static VALUE mString_to_json_raw_object(VALUE self)
{
    VALUE ary;
    VALUE result = rb_hash_new();
    rb_hash_aset(result, rb_funcall(mJSON, i_create_id, 0), rb_class_name(rb_obj_class(self)));
    ary = rb_funcall(self, i_unpack, 1, rb_str_new2("C*"));
    rb_hash_aset(result, rb_utf8_str_new_lit("raw"), ary);
    return result;
}

/*
 * call-seq: to_json_raw(*args)
 *
 * This method creates a JSON text from the result of a call to
 * to_json_raw_object of this String.
 */
static VALUE mString_to_json_raw(int argc, VALUE *argv, VALUE self)
{
    VALUE obj = mString_to_json_raw_object(self);
    Check_Type(obj, T_HASH);
    return mHash_to_json(argc, argv, obj);
}

/*
 * call-seq: json_create(o)
 *
 * Raw Strings are JSON Objects (the raw bytes are stored in an array for the
 * key "raw"). The Ruby String can be created by this module method.
 */
static VALUE mString_Extend_json_create(VALUE self, VALUE o)
{
    VALUE ary;
    Check_Type(o, T_HASH);
    ary = rb_hash_aref(o, rb_str_new2("raw"));
    return rb_funcall(ary, i_pack, 1, rb_str_new2("C*"));
}

/*
 * call-seq: to_json(*)
 *
 * Returns a JSON string for true: 'true'.
 */
static VALUE mTrueClass_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    return rb_utf8_str_new("true", 4);
}

/*
 * call-seq: to_json(*)
 *
 * Returns a JSON string for false: 'false'.
 */
static VALUE mFalseClass_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    return rb_utf8_str_new("false", 5);
}

/*
 * call-seq: to_json(*)
 *
 * Returns a JSON string for nil: 'null'.
 */
static VALUE mNilClass_to_json(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 0, 1);
    return rb_utf8_str_new("null", 4);
}

/*
 * call-seq: to_json(*)
 *
 * Converts this object to a string (calling #to_s), converts
 * it to a JSON string, and returns the result. This is a fallback, if no
 * special method #to_json was defined for some object.
 */
static VALUE mObject_to_json(int argc, VALUE *argv, VALUE self)
{
    VALUE state;
    VALUE string = rb_funcall(self, i_to_s, 0);
    rb_scan_args(argc, argv, "01", &state);
    Check_Type(string, T_STRING);
    state = cState_from_state_s(cState, state);
    return cState_partial_generate(state, string, generate_json_string, Qfalse);
}

static void State_mark(void *ptr)
{
    JSON_Generator_State *state = ptr;
    rb_gc_mark_movable(state->indent);
    rb_gc_mark_movable(state->space);
    rb_gc_mark_movable(state->space_before);
    rb_gc_mark_movable(state->object_nl);
    rb_gc_mark_movable(state->array_nl);
}

static void State_compact(void *ptr)
{
    JSON_Generator_State *state = ptr;
    state->indent = rb_gc_location(state->indent);
    state->space = rb_gc_location(state->space);
    state->space_before = rb_gc_location(state->space_before);
    state->object_nl = rb_gc_location(state->object_nl);
    state->array_nl = rb_gc_location(state->array_nl);
}

static void State_free(void *ptr)
{
    JSON_Generator_State *state = ptr;
    ruby_xfree(state);
}

static size_t State_memsize(const void *ptr)
{
    return sizeof(JSON_Generator_State);
}

#ifndef HAVE_RB_EXT_RACTOR_SAFE
#   undef RUBY_TYPED_FROZEN_SHAREABLE
#   define RUBY_TYPED_FROZEN_SHAREABLE 0
#endif

static const rb_data_type_t JSON_Generator_State_type = {
    "JSON/Generator/State",
    {
        .dmark = State_mark,
        .dfree = State_free,
        .dsize = State_memsize,
        .dcompact = State_compact,
    },
    0, 0,
    RUBY_TYPED_WB_PROTECTED | RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_FROZEN_SHAREABLE,
};

static void state_init(JSON_Generator_State *state)
{
    state->max_nesting = 100;
    state->buffer_initial_length = FBUFFER_INITIAL_LENGTH_DEFAULT;
}

static VALUE cState_s_allocate(VALUE klass)
{
    JSON_Generator_State *state;
    VALUE obj = TypedData_Make_Struct(klass, JSON_Generator_State, &JSON_Generator_State_type, state);
    state_init(state);
    return obj;
}

static void vstate_spill(struct generate_json_data *data)
{
    VALUE vstate = cState_s_allocate(cState);
    GET_STATE(vstate);
    MEMCPY(state, data->state, JSON_Generator_State, 1);
    data->state = state;
    data->vstate = vstate;
    RB_OBJ_WRITTEN(vstate, Qundef, state->indent);
    RB_OBJ_WRITTEN(vstate, Qundef, state->space);
    RB_OBJ_WRITTEN(vstate, Qundef, state->space_before);
    RB_OBJ_WRITTEN(vstate, Qundef, state->object_nl);
    RB_OBJ_WRITTEN(vstate, Qundef, state->array_nl);
}

static inline VALUE vstate_get(struct generate_json_data *data)
{
    if (RB_UNLIKELY(!data->vstate)) {
        vstate_spill(data);
    }
    return data->vstate;
}

struct hash_foreach_arg {
    struct generate_json_data *data;
    int iter;
};

static int
json_object_i(VALUE key, VALUE val, VALUE _arg)
{
    struct hash_foreach_arg *arg = (struct hash_foreach_arg *)_arg;
    struct generate_json_data *data = arg->data;

    FBuffer *buffer = data->buffer;
    JSON_Generator_State *state = data->state;

    long depth = state->depth;
    int j;

    if (arg->iter > 0) fbuffer_append_char(buffer, ',');
    if (RB_UNLIKELY(state->object_nl)) {
        fbuffer_append_str(buffer, state->object_nl);
    }
    if (RB_UNLIKELY(state->indent)) {
        for (j = 0; j < depth; j++) {
            fbuffer_append_str(buffer, state->indent);
        }
    }

    VALUE key_to_s;
    switch(rb_type(key)) {
        case T_STRING:
            if (RB_LIKELY(RBASIC_CLASS(key) == rb_cString)) {
                key_to_s = key;
            } else {
                key_to_s = rb_funcall(key, i_to_s, 0);
            }
            break;
        case T_SYMBOL:
            key_to_s = rb_sym2str(key);
            break;
        default:
            key_to_s = rb_convert_type(key, T_STRING, "String", "to_s");
            break;
    }

    if (RB_LIKELY(RBASIC_CLASS(key_to_s) == rb_cString)) {
        generate_json_string(buffer, data, state, key_to_s);
    } else {
        generate_json(buffer, data, state, key_to_s);
    }
    if (RB_UNLIKELY(state->space_before)) fbuffer_append_str(buffer, state->space_before);
    fbuffer_append_char(buffer, ':');
    if (RB_UNLIKELY(state->space)) fbuffer_append_str(buffer, state->space);
    generate_json(buffer, data, state, val);

    arg->iter++;
    return ST_CONTINUE;
}

static void generate_json_object(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    long max_nesting = state->max_nesting;
    long depth = ++state->depth;
    int j;

    if (max_nesting != 0 && depth > max_nesting) {
        rb_raise(eNestingError, "nesting of %ld is too deep", --state->depth);
    }

    if (RHASH_SIZE(obj) == 0) {
        fbuffer_append(buffer, "{}", 2);
        --state->depth;
        return;
    }

    fbuffer_append_char(buffer, '{');

    struct hash_foreach_arg arg = {
        .data = data,
        .iter = 0,
    };
    rb_hash_foreach(obj, json_object_i, (VALUE)&arg);

    depth = --state->depth;
    if (RB_UNLIKELY(state->object_nl)) {
        fbuffer_append_str(buffer, state->object_nl);
        if (RB_UNLIKELY(state->indent)) {
            for (j = 0; j < depth; j++) {
                fbuffer_append_str(buffer, state->indent);
            }
        }
    }
    fbuffer_append_char(buffer, '}');
}

static void generate_json_array(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    long max_nesting = state->max_nesting;
    long depth = ++state->depth;
    int i, j;
    if (max_nesting != 0 && depth > max_nesting) {
        rb_raise(eNestingError, "nesting of %ld is too deep", --state->depth);
    }

    if (RARRAY_LEN(obj) == 0) {
        fbuffer_append(buffer, "[]", 2);
        --state->depth;
        return;
    }

    fbuffer_append_char(buffer, '[');
    if (RB_UNLIKELY(state->array_nl)) fbuffer_append_str(buffer, state->array_nl);
    for(i = 0; i < RARRAY_LEN(obj); i++) {
        if (i > 0) {
            fbuffer_append_char(buffer, ',');
            if (RB_UNLIKELY(state->array_nl)) fbuffer_append_str(buffer, state->array_nl);
        }
        if (RB_UNLIKELY(state->indent)) {
            for (j = 0; j < depth; j++) {
                fbuffer_append_str(buffer, state->indent);
            }
        }
        generate_json(buffer, data, state, RARRAY_AREF(obj, i));
    }
    state->depth = --depth;
    if (RB_UNLIKELY(state->array_nl)) {
        fbuffer_append_str(buffer, state->array_nl);
        if (RB_UNLIKELY(state->indent)) {
            for (j = 0; j < depth; j++) {
                fbuffer_append_str(buffer, state->indent);
            }
        }
    }
    fbuffer_append_char(buffer, ']');
}

static inline int enc_utf8_compatible_p(int enc_idx)
{
    if (enc_idx == usascii_encindex) return 1;
    if (enc_idx == utf8_encindex) return 1;
    return 0;
}

static VALUE encode_json_string_try(VALUE str)
{
    return rb_funcall(str, i_encode, 1, Encoding_UTF_8);
}

static VALUE encode_json_string_rescue(VALUE str, VALUE exception)
{
    raise_generator_error_str(str, rb_funcall(exception, rb_intern("message"), 0));
    return Qundef;
}

static inline VALUE ensure_valid_encoding(VALUE str)
{
    int encindex = RB_ENCODING_GET(str);
    VALUE utf8_string;
    if (RB_UNLIKELY(!enc_utf8_compatible_p(encindex))) {
        if (encindex == binary_encindex) {
            utf8_string = rb_enc_associate_index(rb_str_dup(str), utf8_encindex);
            switch (rb_enc_str_coderange(utf8_string)) {
                case ENC_CODERANGE_7BIT:
                    return utf8_string;
                case ENC_CODERANGE_VALID:
                    // For historical reason, we silently reinterpret binary strings as UTF-8 if it would work.
                    // TODO: Raise in 3.0.0
                    rb_warn("JSON.generate: UTF-8 string passed as BINARY, this will raise an encoding error in json 3.0");
                    return utf8_string;
                    break;
            }
        }

        str = rb_rescue(encode_json_string_try, str, encode_json_string_rescue, str);
    }
    return str;
}

static void generate_json_string(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    obj = ensure_valid_encoding(obj);

    fbuffer_append_char(buffer, '"');

    switch(rb_enc_str_coderange(obj)) {
        case ENC_CODERANGE_7BIT:
        case ENC_CODERANGE_VALID:
            if (RB_UNLIKELY(state->ascii_only)) {
                convert_UTF8_to_ASCII_only_JSON(buffer, obj, state->script_safe ? script_safe_escape_table : ascii_only_escape_table);
            } else {
                convert_UTF8_to_JSON_impl(buffer, obj, state->script_safe ? script_safe_escape_table : escape_table);
            }
            break;
        default:
            raise_generator_error(obj, "source sequence is illegal/malformed utf-8");
            break;
    }
    fbuffer_append_char(buffer, '"');
}

static void generate_json_null(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    fbuffer_append(buffer, "null", 4);
}

static void generate_json_false(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    fbuffer_append(buffer, "false", 5);
}

static void generate_json_true(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    fbuffer_append(buffer, "true", 4);
}

static void generate_json_fixnum(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    fbuffer_append_long(buffer, FIX2LONG(obj));
}

static void generate_json_bignum(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    VALUE tmp = rb_funcall(obj, i_to_s, 0);
    fbuffer_append_str(buffer, tmp);
}

#ifdef RUBY_INTEGER_UNIFICATION
static void generate_json_integer(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    if (FIXNUM_P(obj))
        generate_json_fixnum(buffer, data, state, obj);
    else
        generate_json_bignum(buffer, data, state, obj);
}
#endif

static void generate_json_float(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    double value = RFLOAT_VALUE(obj);
    char allow_nan = state->allow_nan;
    VALUE tmp = rb_funcall(obj, i_to_s, 0);
    if (!allow_nan) {
        if (isinf(value) || isnan(value)) {
            raise_generator_error(obj, "%"PRIsVALUE" not allowed in JSON", tmp);
        }
    }
    fbuffer_append_str(buffer, tmp);
}

static void generate_json(FBuffer *buffer, struct generate_json_data *data, JSON_Generator_State *state, VALUE obj)
{
    VALUE tmp;
    if (obj == Qnil) {
        generate_json_null(buffer, data, state, obj);
    } else if (obj == Qfalse) {
        generate_json_false(buffer, data, state, obj);
    } else if (obj == Qtrue) {
        generate_json_true(buffer, data, state, obj);
    } else if (RB_SPECIAL_CONST_P(obj)) {
        if (RB_FIXNUM_P(obj)) {
            generate_json_fixnum(buffer, data, state, obj);
        } else if (RB_FLONUM_P(obj)) {
            generate_json_float(buffer, data, state, obj);
        } else {
            goto general;
        }
    } else {
        VALUE klass = RBASIC_CLASS(obj);
        switch (RB_BUILTIN_TYPE(obj)) {
            case T_BIGNUM:
                generate_json_bignum(buffer, data, state, obj);
                break;
            case T_HASH:
                if (klass != rb_cHash) goto general;
                generate_json_object(buffer, data, state, obj);
                break;
            case T_ARRAY:
                if (klass != rb_cArray) goto general;
                generate_json_array(buffer, data, state, obj);
                break;
            case T_STRING:
                if (klass != rb_cString) goto general;
                generate_json_string(buffer, data, state, obj);
                break;
            case T_FLOAT:
                if (klass != rb_cFloat) goto general;
                generate_json_float(buffer, data, state, obj);
                break;
            default:
            general:
                if (state->strict) {
                    raise_generator_error(obj, "%"PRIsVALUE" not allowed in JSON", CLASS_OF(obj));
                } else if (rb_respond_to(obj, i_to_json)) {
                    tmp = rb_funcall(obj, i_to_json, 1, vstate_get(data));
                    Check_Type(tmp, T_STRING);
                    fbuffer_append_str(buffer, tmp);
                } else {
                    tmp = rb_funcall(obj, i_to_s, 0);
                    Check_Type(tmp, T_STRING);
                    generate_json_string(buffer, data, state, tmp);
                }
        }
    }
}

static VALUE generate_json_try(VALUE d)
{
    struct generate_json_data *data = (struct generate_json_data *)d;

    data->func(data->buffer, data, data->state, data->obj);

    return Qnil;
}

static VALUE generate_json_rescue(VALUE d, VALUE exc)
{
    struct generate_json_data *data = (struct generate_json_data *)d;
    fbuffer_free(data->buffer);

    rb_exc_raise(exc);

    return Qundef;
}

static VALUE cState_partial_generate(VALUE self, VALUE obj, generator_func func, VALUE io)
{
    GET_STATE(self);

    char stack_buffer[FBUFFER_STACK_SIZE];
    FBuffer buffer = {
        .io = RTEST(io) ? io : Qfalse,
    };
    fbuffer_stack_init(&buffer, state->buffer_initial_length, stack_buffer, FBUFFER_STACK_SIZE);

    struct generate_json_data data = {
        .buffer = &buffer,
        .vstate = self,
        .state = state,
        .obj = obj,
        .func = func
    };
    rb_rescue(generate_json_try, (VALUE)&data, generate_json_rescue, (VALUE)&data);

    return fbuffer_finalize(&buffer);
}

/* call-seq:
 *   generate(obj) -> String
 *   generate(obj, anIO) -> anIO
 *
 * Generates a valid JSON document from object +obj+ and returns the
 * result. If no valid JSON document can be created this method raises a
 * GeneratorError exception.
 */
static VALUE cState_generate(int argc, VALUE *argv, VALUE self)
{
    rb_check_arity(argc, 1, 2);
    VALUE obj = argv[0];
    VALUE io = argc > 1 ? argv[1] : Qnil;
    VALUE result = cState_partial_generate(self, obj, generate_json, io);
    GET_STATE(self);
    (void)state;
    return result;
}

static VALUE cState_initialize(int argc, VALUE *argv, VALUE self)
{
    rb_warn("The json gem extension was loaded with the stdlib ruby code. You should upgrade rubygems with `gem update --system`");
    return self;
}

/*
 * call-seq: initialize_copy(orig)
 *
 * Initializes this object from orig if it can be duplicated/cloned and returns
 * it.
*/
static VALUE cState_init_copy(VALUE obj, VALUE orig)
{
    JSON_Generator_State *objState, *origState;

    if (obj == orig) return obj;
    GET_STATE_TO(obj, objState);
    GET_STATE_TO(orig, origState);
    if (!objState) rb_raise(rb_eArgError, "unallocated JSON::State");

    MEMCPY(objState, origState, JSON_Generator_State, 1);
    objState->indent = origState->indent;
    objState->space = origState->space;
    objState->space_before = origState->space_before;
    objState->object_nl = origState->object_nl;
    objState->array_nl = origState->array_nl;
    return obj;
}

/*
 * call-seq: from_state(opts)
 *
 * Creates a State object from _opts_, which ought to be Hash to create a
 * new State instance configured by _opts_, something else to create an
 * unconfigured instance. If _opts_ is a State object, it is just returned.
 */
static VALUE cState_from_state_s(VALUE self, VALUE opts)
{
    if (rb_obj_is_kind_of(opts, self)) {
        return opts;
    } else if (rb_obj_is_kind_of(opts, rb_cHash)) {
        return rb_funcall(self, i_new, 1, opts);
    } else {
        return rb_class_new_instance(0, NULL, cState);
    }
}

/*
 * call-seq: indent()
 *
 * Returns the string that is used to indent levels in the JSON text.
 */
static VALUE cState_indent(VALUE self)
{
    GET_STATE(self);
    return state->indent ? state->indent : rb_str_freeze(rb_utf8_str_new("", 0));
}

static VALUE string_config(VALUE config)
{
    if (RTEST(config)) {
        Check_Type(config, T_STRING);
        if (RSTRING_LEN(config)) {
            return rb_str_new_frozen(config);
        }
    }
    return Qfalse;
}

/*
 * call-seq: indent=(indent)
 *
 * Sets the string that is used to indent levels in the JSON text.
 */
static VALUE cState_indent_set(VALUE self, VALUE indent)
{
    GET_STATE(self);
    RB_OBJ_WRITE(self, &state->indent, string_config(indent));
    return Qnil;
}

/*
 * call-seq: space()
 *
 * Returns the string that is used to insert a space between the tokens in a JSON
 * string.
 */
static VALUE cState_space(VALUE self)
{
    GET_STATE(self);
    return state->space ? state->space : rb_str_freeze(rb_utf8_str_new("", 0));
}

/*
 * call-seq: space=(space)
 *
 * Sets _space_ to the string that is used to insert a space between the tokens in a JSON
 * string.
 */
static VALUE cState_space_set(VALUE self, VALUE space)
{
    GET_STATE(self);
    RB_OBJ_WRITE(self, &state->space, string_config(space));
    return Qnil;
}

/*
 * call-seq: space_before()
 *
 * Returns the string that is used to insert a space before the ':' in JSON objects.
 */
static VALUE cState_space_before(VALUE self)
{
    GET_STATE(self);
    return state->space_before ? state->space_before : rb_str_freeze(rb_utf8_str_new("", 0));
}

/*
 * call-seq: space_before=(space_before)
 *
 * Sets the string that is used to insert a space before the ':' in JSON objects.
 */
static VALUE cState_space_before_set(VALUE self, VALUE space_before)
{
    GET_STATE(self);
    RB_OBJ_WRITE(self, &state->space_before, string_config(space_before));
    return Qnil;
}

/*
 * call-seq: object_nl()
 *
 * This string is put at the end of a line that holds a JSON object (or
 * Hash).
 */
static VALUE cState_object_nl(VALUE self)
{
    GET_STATE(self);
    return state->object_nl ? state->object_nl : rb_str_freeze(rb_utf8_str_new("", 0));
}

/*
 * call-seq: object_nl=(object_nl)
 *
 * This string is put at the end of a line that holds a JSON object (or
 * Hash).
 */
static VALUE cState_object_nl_set(VALUE self, VALUE object_nl)
{
    GET_STATE(self);
    RB_OBJ_WRITE(self, &state->object_nl, string_config(object_nl));
    return Qnil;
}

/*
 * call-seq: array_nl()
 *
 * This string is put at the end of a line that holds a JSON array.
 */
static VALUE cState_array_nl(VALUE self)
{
    GET_STATE(self);
    return state->array_nl ? state->array_nl : rb_str_freeze(rb_utf8_str_new("", 0));
}

/*
 * call-seq: array_nl=(array_nl)
 *
 * This string is put at the end of a line that holds a JSON array.
 */
static VALUE cState_array_nl_set(VALUE self, VALUE array_nl)
{
    GET_STATE(self);
    RB_OBJ_WRITE(self, &state->array_nl, string_config(array_nl));
    return Qnil;
}


/*
* call-seq: check_circular?
*
* Returns true, if circular data structures should be checked,
* otherwise returns false.
*/
static VALUE cState_check_circular_p(VALUE self)
{
    GET_STATE(self);
    return state->max_nesting ? Qtrue : Qfalse;
}

/*
 * call-seq: max_nesting
 *
 * This integer returns the maximum level of data structure nesting in
 * the generated JSON, max_nesting = 0 if no maximum is checked.
 */
static VALUE cState_max_nesting(VALUE self)
{
    GET_STATE(self);
    return LONG2FIX(state->max_nesting);
}

static long long_config(VALUE num)
{
    return RTEST(num) ? FIX2LONG(num) : 0;
}

/*
 * call-seq: max_nesting=(depth)
 *
 * This sets the maximum level of data structure nesting in the generated JSON
 * to the integer depth, max_nesting = 0 if no maximum should be checked.
 */
static VALUE cState_max_nesting_set(VALUE self, VALUE depth)
{
    GET_STATE(self);
    state->max_nesting = long_config(depth);
    return Qnil;
}

/*
 * call-seq: script_safe
 *
 * If this boolean is true, the forward slashes will be escaped in
 * the json output.
 */
static VALUE cState_script_safe(VALUE self)
{
    GET_STATE(self);
    return state->script_safe ? Qtrue : Qfalse;
}

/*
 * call-seq: script_safe=(enable)
 *
 * This sets whether or not the forward slashes will be escaped in
 * the json output.
 */
static VALUE cState_script_safe_set(VALUE self, VALUE enable)
{
    GET_STATE(self);
    state->script_safe = RTEST(enable);
    return Qnil;
}

/*
 * call-seq: strict
 *
 * If this boolean is false, types unsupported by the JSON format will
 * be serialized as strings.
 * If this boolean is true, types unsupported by the JSON format will
 * raise a JSON::GeneratorError.
 */
static VALUE cState_strict(VALUE self)
{
    GET_STATE(self);
    return state->strict ? Qtrue : Qfalse;
}

/*
 * call-seq: strict=(enable)
 *
 * This sets whether or not to serialize types unsupported by the
 * JSON format as strings.
 * If this boolean is false, types unsupported by the JSON format will
 * be serialized as strings.
 * If this boolean is true, types unsupported by the JSON format will
 * raise a JSON::GeneratorError.
 */
static VALUE cState_strict_set(VALUE self, VALUE enable)
{
    GET_STATE(self);
    state->strict = RTEST(enable);
    return Qnil;
}

/*
 * call-seq: allow_nan?
 *
 * Returns true, if NaN, Infinity, and -Infinity should be generated, otherwise
 * returns false.
 */
static VALUE cState_allow_nan_p(VALUE self)
{
    GET_STATE(self);
    return state->allow_nan ? Qtrue : Qfalse;
}

/*
 * call-seq: allow_nan=(enable)
 *
 * This sets whether or not to serialize NaN, Infinity, and -Infinity
 */
static VALUE cState_allow_nan_set(VALUE self, VALUE enable)
{
    GET_STATE(self);
    state->allow_nan = RTEST(enable);
    return Qnil;
}

/*
 * call-seq: ascii_only?
 *
 * Returns true, if only ASCII characters should be generated. Otherwise
 * returns false.
 */
static VALUE cState_ascii_only_p(VALUE self)
{
    GET_STATE(self);
    return state->ascii_only ? Qtrue : Qfalse;
}

/*
 * call-seq: ascii_only=(enable)
 *
 * This sets whether only ASCII characters should be generated.
 */
static VALUE cState_ascii_only_set(VALUE self, VALUE enable)
{
    GET_STATE(self);
    state->ascii_only = RTEST(enable);
    return Qnil;
}

/*
 * call-seq: depth
 *
 * This integer returns the current depth of data structure nesting.
 */
static VALUE cState_depth(VALUE self)
{
    GET_STATE(self);
    return LONG2FIX(state->depth);
}

/*
 * call-seq: depth=(depth)
 *
 * This sets the maximum level of data structure nesting in the generated JSON
 * to the integer depth, max_nesting = 0 if no maximum should be checked.
 */
static VALUE cState_depth_set(VALUE self, VALUE depth)
{
    GET_STATE(self);
    state->depth = long_config(depth);
    return Qnil;
}

/*
 * call-seq: buffer_initial_length
 *
 * This integer returns the current initial length of the buffer.
 */
static VALUE cState_buffer_initial_length(VALUE self)
{
    GET_STATE(self);
    return LONG2FIX(state->buffer_initial_length);
}

static void buffer_initial_length_set(JSON_Generator_State *state, VALUE buffer_initial_length)
{
    Check_Type(buffer_initial_length, T_FIXNUM);
    long initial_length = FIX2LONG(buffer_initial_length);
    if (initial_length > 0) {
        state->buffer_initial_length = initial_length;
    }
}

/*
 * call-seq: buffer_initial_length=(length)
 *
 * This sets the initial length of the buffer to +length+, if +length+ > 0,
 * otherwise its value isn't changed.
 */
static VALUE cState_buffer_initial_length_set(VALUE self, VALUE buffer_initial_length)
{
    GET_STATE(self);
    buffer_initial_length_set(state, buffer_initial_length);
    return Qnil;
}

static int configure_state_i(VALUE key, VALUE val, VALUE _arg)
{
    JSON_Generator_State *state = (JSON_Generator_State *)_arg;

         if (key == sym_indent)                { state->indent = string_config(val); }
    else if (key == sym_space)                 { state->space = string_config(val); }
    else if (key == sym_space_before)          { state->space_before = string_config(val); }
    else if (key == sym_object_nl)             { state->object_nl = string_config(val); }
    else if (key == sym_array_nl)              { state->array_nl = string_config(val); }
    else if (key == sym_max_nesting)           { state->max_nesting = long_config(val); }
    else if (key == sym_allow_nan)             { state->allow_nan = RTEST(val); }
    else if (key == sym_ascii_only)            { state->ascii_only = RTEST(val); }
    else if (key == sym_depth)                 { state->depth = long_config(val); }
    else if (key == sym_buffer_initial_length) { buffer_initial_length_set(state, val); }
    else if (key == sym_script_safe)           { state->script_safe = RTEST(val); }
    else if (key == sym_escape_slash)          { state->script_safe = RTEST(val); }
    else if (key == sym_strict)                { state->strict = RTEST(val); }
    return ST_CONTINUE;
}

static void configure_state(JSON_Generator_State *state, VALUE config)
{
    if (!RTEST(config)) return;

    Check_Type(config, T_HASH);

    if (!RHASH_SIZE(config)) return;

    // We assume in most cases few keys are set so it's faster to go over
    // the provided keys than to check all possible keys.
    rb_hash_foreach(config, configure_state_i, (VALUE)state);
}

static VALUE cState_configure(VALUE self, VALUE opts)
{
    GET_STATE(self);
    configure_state(state, opts);
    return self;
}

static VALUE cState_m_generate(VALUE klass, VALUE obj, VALUE opts, VALUE io)
{
    JSON_Generator_State state = {0};
    state_init(&state);
    configure_state(&state, opts);

    char stack_buffer[FBUFFER_STACK_SIZE];
    FBuffer buffer = {
        .io = RTEST(io) ? io : Qfalse,
    };
    fbuffer_stack_init(&buffer, state.buffer_initial_length, stack_buffer, FBUFFER_STACK_SIZE);

    struct generate_json_data data = {
        .buffer = &buffer,
        .vstate = Qfalse,
        .state = &state,
        .obj = obj,
        .func = generate_json,
    };
    rb_rescue(generate_json_try, (VALUE)&data, generate_json_rescue, (VALUE)&data);

    return fbuffer_finalize(&buffer);
}

/*
 *
 */
void Init_generator(void)
{
#ifdef HAVE_RB_EXT_RACTOR_SAFE
    rb_ext_ractor_safe(true);
#endif

#undef rb_intern
    rb_require("json/common");

    mJSON = rb_define_module("JSON");
    VALUE mExt = rb_define_module_under(mJSON, "Ext");
    VALUE mGenerator = rb_define_module_under(mExt, "Generator");

    rb_global_variable(&eGeneratorError);
    eGeneratorError = rb_path2class("JSON::GeneratorError");

    rb_global_variable(&eNestingError);
    eNestingError = rb_path2class("JSON::NestingError");

    cState = rb_define_class_under(mGenerator, "State", rb_cObject);
    rb_define_alloc_func(cState, cState_s_allocate);
    rb_define_singleton_method(cState, "from_state", cState_from_state_s, 1);
    rb_define_method(cState, "initialize", cState_initialize, -1);
    rb_define_alias(cState, "initialize", "initialize"); // avoid method redefinition warnings
    rb_define_private_method(cState, "_configure", cState_configure, 1);

    rb_define_method(cState, "initialize_copy", cState_init_copy, 1);
    rb_define_method(cState, "indent", cState_indent, 0);
    rb_define_method(cState, "indent=", cState_indent_set, 1);
    rb_define_method(cState, "space", cState_space, 0);
    rb_define_method(cState, "space=", cState_space_set, 1);
    rb_define_method(cState, "space_before", cState_space_before, 0);
    rb_define_method(cState, "space_before=", cState_space_before_set, 1);
    rb_define_method(cState, "object_nl", cState_object_nl, 0);
    rb_define_method(cState, "object_nl=", cState_object_nl_set, 1);
    rb_define_method(cState, "array_nl", cState_array_nl, 0);
    rb_define_method(cState, "array_nl=", cState_array_nl_set, 1);
    rb_define_method(cState, "max_nesting", cState_max_nesting, 0);
    rb_define_method(cState, "max_nesting=", cState_max_nesting_set, 1);
    rb_define_method(cState, "script_safe", cState_script_safe, 0);
    rb_define_method(cState, "script_safe?", cState_script_safe, 0);
    rb_define_method(cState, "script_safe=", cState_script_safe_set, 1);
    rb_define_alias(cState, "escape_slash", "script_safe");
    rb_define_alias(cState, "escape_slash?", "script_safe?");
    rb_define_alias(cState, "escape_slash=", "script_safe=");
    rb_define_method(cState, "strict", cState_strict, 0);
    rb_define_method(cState, "strict?", cState_strict, 0);
    rb_define_method(cState, "strict=", cState_strict_set, 1);
    rb_define_method(cState, "check_circular?", cState_check_circular_p, 0);
    rb_define_method(cState, "allow_nan?", cState_allow_nan_p, 0);
    rb_define_method(cState, "allow_nan=", cState_allow_nan_set, 1);
    rb_define_method(cState, "ascii_only?", cState_ascii_only_p, 0);
    rb_define_method(cState, "ascii_only=", cState_ascii_only_set, 1);
    rb_define_method(cState, "depth", cState_depth, 0);
    rb_define_method(cState, "depth=", cState_depth_set, 1);
    rb_define_method(cState, "buffer_initial_length", cState_buffer_initial_length, 0);
    rb_define_method(cState, "buffer_initial_length=", cState_buffer_initial_length_set, 1);
    rb_define_method(cState, "generate", cState_generate, -1);

    rb_define_singleton_method(cState, "generate", cState_m_generate, 3);

    VALUE mGeneratorMethods = rb_define_module_under(mGenerator, "GeneratorMethods");

    VALUE mObject = rb_define_module_under(mGeneratorMethods, "Object");
    rb_define_method(mObject, "to_json", mObject_to_json, -1);

    VALUE mHash = rb_define_module_under(mGeneratorMethods, "Hash");
    rb_define_method(mHash, "to_json", mHash_to_json, -1);

    VALUE mArray = rb_define_module_under(mGeneratorMethods, "Array");
    rb_define_method(mArray, "to_json", mArray_to_json, -1);

#ifdef RUBY_INTEGER_UNIFICATION
    VALUE mInteger = rb_define_module_under(mGeneratorMethods, "Integer");
    rb_define_method(mInteger, "to_json", mInteger_to_json, -1);
#else
    VALUE mFixnum = rb_define_module_under(mGeneratorMethods, "Fixnum");
    rb_define_method(mFixnum, "to_json", mFixnum_to_json, -1);

    VALUE mBignum = rb_define_module_under(mGeneratorMethods, "Bignum");
    rb_define_method(mBignum, "to_json", mBignum_to_json, -1);
#endif
    VALUE mFloat = rb_define_module_under(mGeneratorMethods, "Float");
    rb_define_method(mFloat, "to_json", mFloat_to_json, -1);

    VALUE mString = rb_define_module_under(mGeneratorMethods, "String");
    rb_define_singleton_method(mString, "included", mString_included_s, 1);
    rb_define_method(mString, "to_json", mString_to_json, -1);
    rb_define_method(mString, "to_json_raw", mString_to_json_raw, -1);
    rb_define_method(mString, "to_json_raw_object", mString_to_json_raw_object, 0);

    mString_Extend = rb_define_module_under(mString, "Extend");
    rb_define_method(mString_Extend, "json_create", mString_Extend_json_create, 1);

    VALUE mTrueClass = rb_define_module_under(mGeneratorMethods, "TrueClass");
    rb_define_method(mTrueClass, "to_json", mTrueClass_to_json, -1);

    VALUE mFalseClass = rb_define_module_under(mGeneratorMethods, "FalseClass");
    rb_define_method(mFalseClass, "to_json", mFalseClass_to_json, -1);

    VALUE mNilClass = rb_define_module_under(mGeneratorMethods, "NilClass");
    rb_define_method(mNilClass, "to_json", mNilClass_to_json, -1);

    rb_global_variable(&Encoding_UTF_8);
    Encoding_UTF_8 = rb_const_get(rb_path2class("Encoding"), rb_intern("UTF_8"));

    i_to_s = rb_intern("to_s");
    i_to_json = rb_intern("to_json");
    i_new = rb_intern("new");
    i_pack = rb_intern("pack");
    i_unpack = rb_intern("unpack");
    i_create_id = rb_intern("create_id");
    i_extend = rb_intern("extend");
    i_encode = rb_intern("encode");

    sym_indent = ID2SYM(rb_intern("indent"));
    sym_space = ID2SYM(rb_intern("space"));
    sym_space_before = ID2SYM(rb_intern("space_before"));
    sym_object_nl = ID2SYM(rb_intern("object_nl"));
    sym_array_nl = ID2SYM(rb_intern("array_nl"));
    sym_max_nesting = ID2SYM(rb_intern("max_nesting"));
    sym_allow_nan = ID2SYM(rb_intern("allow_nan"));
    sym_ascii_only = ID2SYM(rb_intern("ascii_only"));
    sym_depth = ID2SYM(rb_intern("depth"));
    sym_buffer_initial_length = ID2SYM(rb_intern("buffer_initial_length"));
    sym_script_safe = ID2SYM(rb_intern("script_safe"));
    sym_escape_slash = ID2SYM(rb_intern("escape_slash"));
    sym_strict = ID2SYM(rb_intern("strict"));

    usascii_encindex = rb_usascii_encindex();
    utf8_encindex = rb_utf8_encindex();
    binary_encindex = rb_ascii8bit_encindex();

    rb_require("json/ext/generator/state");

       // TODO ADD RUNTIME CHECKS HERE?
    switch(find_simd_implementation()) {
#ifdef HAVE_SIMD_NEON
        case SIMD_NEON:
            convert_UTF8_to_JSON_impl = convert_UTF8_to_JSON_simd;
            convert_UTF8_to_JSON_simd_kernel = convert_UTF8_to_JSON_simd_kernel_neon;
            break;
#endif
#ifdef HAVE_SIMD_X86_64
        case SIMD_SSE42:
            convert_UTF8_to_JSON_impl = convert_UTF8_to_JSON_simd;
            convert_UTF8_to_JSON_simd_kernel = convert_UTF8_to_JSON_simd_kernel_sse42;
            break;
#ifdef HAVE_TYPE___M256I
        case SIMD_AVX2:
            convert_UTF8_to_JSON_impl = convert_UTF8_to_JSON_simd;
            convert_UTF8_to_JSON_simd_kernel = convert_UTF8_to_JSON_simd_kernel_avx2;
            break;
#endif /* HAVE_TYPE___M256I */
#endif
        default:
            convert_UTF8_to_JSON_impl = convert_UTF8_to_JSON;
    }
}