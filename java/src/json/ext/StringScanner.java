package json.ext;

import java.nio.ByteBuffer;

/**
 * Scans the body of a JSON string for its closing quote, reporting whether the
 * body can be copied verbatim (the "plain" fast path) or must be handed to
 * {@link StringDecoder} for escape expansion and UTF-8/control validation.
 *
 * <p>The default implementation is SWAR (8 bytes per step). A vectorized
 * subclass ({@code VectorizedStringScanner}) is loaded reflectively when the
 * {@code jruby.json.useVectorizedParser} system property is set and the
 * {@code jdk.incubator.vector} module is available; otherwise this SWAR
 * implementation is used. Instances are stateless and therefore shared.
 */
class StringScanner {
    /** Set in the returned bits when the whole body is plain printable ASCII. */
    static final long PLAIN_BIT = 1L << 32;
    /** Returned when no closing quote is found before {@code end}. */
    static final long NOT_FOUND = -1L;

    private static final long HIGH_BITS = 0x8080808080808080L;
    private static final long ONES      = 0x0101010101010101L;
    private static final int  HIGH_BITS_32 = 0x80808080;
    private static final int  ONES_32      = 0x01010101;

    private static final String VECTORIZED_SCANNER_CLASS = "json.ext.VectorizedStringScanner";
    private static final String USE_VECTORIZED_PARSER_PROP = "jruby.json.useVectorizedParser";
    private static final String USE_VECTORIZED_PARSER_DEFAULT = "false";

    private static final StringScanner INSTANCE;

    static {
        StringScanner scanner = new StringScanner();
        String enable = System.getProperty(USE_VECTORIZED_PARSER_PROP, USE_VECTORIZED_PARSER_DEFAULT);
        if ("true".equalsIgnoreCase(enable) || "1".equals(enable)) {
            try {
                Class<?> vectorized = StringScanner.class.getClassLoader()
                    .loadClass(VECTORIZED_SCANNER_CLASS);
                scanner = (StringScanner) vectorized.getDeclaredConstructor().newInstance();
            } catch (Throwable t) {
                // jdk.incubator.vector unavailable (or any load failure):
                // keep the SWAR implementation.
                scanner = new StringScanner();
            }
        }
        INSTANCE = scanner;
    }

    static StringScanner getInstance() {
        return INSTANCE;
    }

    /**
     * Scans {@code data[start..end)} for the closing quote, honouring backslash 
     * escapes.
     *
     * @param chunks a little-endian {@link ByteBuffer} over {@code data}, used
     *               for the 8-byte SWAR reads (the vectorized subclass reads
     *               {@code data} directly and ignores it).
     * @return packed result: the low 32 bits hold the index of the closing
     *         quote, or {@code -1} ({@link #NOT_FOUND}) when none is found
     *         before {@code end}; {@link #PLAIN_BIT} is set when the body can be
     *         copied verbatim, i.e. it contains no backslash escape and no ASCII
     *         control character. Non-ASCII (multi-byte UTF-8) bytes are copied
     *         through unchanged, exactly like the C extension's string fast path,
     *         so they do <em>not</em> clear {@link #PLAIN_BIT}.
     */
    long scan(byte[] data, ByteBuffer chunks, int start, int end) {
        int p = start;
        boolean plain = true;

        outer:
        while (true) {
            // SWAR: skip clean 8- then 4-byte chunks. While plain we stop on
            // control chars too; once non-plain we only need quotes/backslashes.
            p = swarSkip(chunks, p, end, plain);
            // Match from the byte above and/or tail handling for the <4 remainder.
            while (p < end) {
                int b = data[p] & 0xFF;
                if (b == '"') {
                    return ((long) p) | (plain ? PLAIN_BIT : 0L);
                }
                if (b == '\\') {
                    plain = false;
                    p += 2; // skip the backslash and the escaped byte
                    continue outer;
                }
                if (b < 0x20) {
                    plain = false;
                    p++;
                    continue outer;
                }
                // Plain byte: printable ASCII or non-ASCII UTF-8 (copied verbatim).
                p++;
            }
            return NOT_FOUND;
        }
    }

    /**
     * Returns the index of the first byte in {@code data[start..end)} that the
     * decoder must stop on: a backslash (escape) or an ASCII control character
     * (&lt; 0x20). Returns {@code end} when none is found.
     *
     * <p>Non-ASCII bytes are deliberately <em>not</em> flagged: like the C
     * extension's {@code json_string_unescape}, the decoder copies multi-byte
     * UTF-8 verbatim, so the only bytes that interrupt a copy run are escapes
     * (which rewrite the output) and control characters (which must be rejected
     * unless {@code allow_control_characters} is set). This lets the decoder
     * skip long runs of UTF-8 text eight bytes at a time instead of validating
     * each character.
     *
     * @param chunks little-endian {@link ByteBuffer} over {@code data}, used for
     *               the 8-byte SWAR reads.
     */
    int nextEscapeOrControl(byte[] data, ByteBuffer chunks, int start, int end) {
        int p = start;
        while (p + 8 <= end) {
            long m = escapeOrControlMask(chunks.getLong(p));
            if (m == 0) {
                p += 8;
            } else {
                return p + (Long.numberOfTrailingZeros(m) >>> 3);
            }
        }
        if (p + 4 <= end) {
            int m = escapeOrControlMask32(chunks.getInt(p));
            if (m != 0) {
                return p + (Integer.numberOfTrailingZeros(m) >>> 3);
            }
            p += 4;
        }
        while (p < end) {
            int b = data[p] & 0xFF;
            if (b == '\\' || b < 0x20) {
                return p;
            }
            p++;
        }
        return end;
    }

    /**
     * Returns a mask whose high bit (0x80) is set in every lane of {@code x}
     * that is a backslash or an ASCII control character (&lt; 0x20). Non-ASCII
     * bytes are not flagged. Returns 0 when the chunk has neither.
     */
    private static long escapeOrControlMask(long x) {
        long control = (x - 0x2020202020202020L) & ~x; // bytes < 0x20 (ASCII)
        long s       = x ^ 0x5C5C5C5C5C5C5C5CL;
        long bslash  = (s - ONES) & ~s;
        return (control | bslash) & HIGH_BITS;
    }

    /** 32-bit ({@code int}) form of {@link #escapeOrControlMask(long)}. */
    private static int escapeOrControlMask32(int x) {
        int control = (x - 0x20202020) & ~x;
        int s       = x ^ 0x5C5C5C5C;
        int bslash  = (s - ONES_32) & ~s;
        return (control | bslash) & HIGH_BITS_32;
    }

    /**
     * Returns a mask whose high bit (0x80) is set in every lane of {@code x}
     * that needs scalar attention while still on the plain fast path: an ASCII
     * control character (&lt; 0x20), a double quote, or a backslash. Non-ASCII
     * bytes are deliberately not flagged — they are copied verbatim — so a body
     * made only of printable ASCII and multi-byte UTF-8 stays plain.
     */
    static long stringScanMask(long x) {
        long control = (x - 0x2020202020202020L) & ~x; // bytes < 0x20 (ASCII)
        long q       = x ^ 0x2222222222222222L;
        long quote   = (q - ONES) & ~q;
        long s       = x ^ 0x5C5C5C5C5C5C5C5CL;
        long bslash  = (s - ONES) & ~s;
        return (control | quote | bslash) & HIGH_BITS;
    }

    /**
     * Like {@link #stringScanMask} but only flags double quotes and backslashes.
     * Used once a string is known to require the decoder, so the remaining scan
     * for the closing quote still skips clean chunks (including multi-byte
     * UTF-8) eight bytes at a time.
     */
    static long quoteBackslashMask(long x) {
        long q      = x ^ 0x2222222222222222L;
        long quote  = (q - ONES) & ~q;
        long s      = x ^ 0x5C5C5C5C5C5C5C5CL;
        long bslash = (s - ONES) & ~s;
        return (quote | bslash) & HIGH_BITS;
    }

    /**
     * Skips the &lt; vector-width remainder left by the vectorized scanner using
     * SWAR: 8 bytes at a time, then a final 4-byte step. Returns the index of the
     * first byte needing scalar attention (quote/escape/control, or quote/escape
     * once {@code plain} is false), or — if the skipped chunks were all clean —
     * the first index of the trailing &lt; 4 bytes the caller must scan one by one.
     */
    static int swarSkip(ByteBuffer chunks, int p, int end, boolean plain) {
        while (p + 8 <= end) {
            long x = chunks.getLong(p);
            long m = plain ? stringScanMask(x) : quoteBackslashMask(x);
            if (m != 0) {
                return p + (Long.numberOfTrailingZeros(m) >>> 3);
            }
            p += 8;
        }
        if (p + 4 <= end) {
            int x = chunks.getInt(p);
            int m = plain ? stringScanMask32(x) : quoteBackslashMask32(x);
            if (m != 0) {
                return p + (Integer.numberOfTrailingZeros(m) >>> 3);
            }
            p += 4;
        }
        return p;
    }

    /** 32-bit ({@code int}) form of {@link #stringScanMask(long)}. */
    static int stringScanMask32(int x) {
        int control = (x - 0x20202020) & ~x;
        int q       = x ^ 0x22222222;
        int quote   = (q - ONES_32) & ~q;
        int s       = x ^ 0x5C5C5C5C;
        int bslash  = (s - ONES_32) & ~s;
        return (control | quote | bslash) & HIGH_BITS_32;
    }

    /** 32-bit ({@code int}) form of {@link #quoteBackslashMask(long)}. */
    static int quoteBackslashMask32(int x) {
        int q      = x ^ 0x22222222;
        int quote  = (q - ONES_32) & ~q;
        int s      = x ^ 0x5C5C5C5C;
        int bslash = (s - ONES_32) & ~s;
        return (quote | bslash) & HIGH_BITS_32;
    }
}
