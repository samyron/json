package json.ext;

import java.nio.ByteBuffer;

import jdk.incubator.vector.ByteVector;
import jdk.incubator.vector.VectorMask;
import jdk.incubator.vector.VectorOperators;
import jdk.incubator.vector.VectorSpecies;

final class VectorizedStringScanner extends StringScanner {
    private static final VectorSpecies<Byte> SP = ByteVector.SPECIES_PREFERRED;
    private static final ByteVector ZERO = ByteVector.zero(SP);
    private static final ByteVector TWO = ByteVector.broadcast(SP, 2);
    private static final ByteVector THIRTY_TWO = ByteVector.broadcast(SP, 32);
    private static final ByteVector THIRTY_THREE = ByteVector.broadcast(SP, 33);
    private static final ByteVector BACKSLASH = ByteVector.broadcast(SP, '\\');
    private static final ByteVector DQUOTE = ByteVector.broadcast(SP, '"');

    @Override
    long scan(byte[] data, ByteBuffer chunks, int start, int end) {
        final int width = SP.length();
        int p = start;
        boolean plain = true;

        // The same structure as the StringEncoder. The logic is 
        // duplicated for maximum inlining.
        outer:
        while (true) {
            boolean hit = false;
            while (p + width <= end) {
                ByteVector chunk = ByteVector.fromArray(SP, data, p);
                VectorMask<Byte> interesting =
                    plain ? interestingLanes(chunk) : quoteOrBackslashLanes(chunk);
                if (interesting.anyTrue()) {
                    p += interesting.firstTrue();
                    hit = true;
                    break;
                }
                p += width;
            }
            if (!hit) {
                // p = swarSkip(chunks, p, end, plain);
                while (p + 8 <= end) {
                    long x = chunks.getLong(p);
                    long m = plain ? stringScanMask(x) : quoteBackslashMask(x);
                    if (m != 0) {
                        p += (Long.numberOfTrailingZeros(m) >>> 3);
                        hit = true;
                        break;
                    }
                    p += 8;
                }
                if (!hit) {
                    if (p + 4 <= end) {
                        int x = chunks.getInt(p);
                        int m = plain ? stringScanMask32(x) : quoteBackslashMask32(x);
                        if (m != 0) {
                            p += (Integer.numberOfTrailingZeros(m) >>> 3);
                        } else {
                            p += 4;
                        }
                    }
                }
            }
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

    // @Override
    // int nextEscapeOrControl(byte[] data, ByteBuffer chunks, int start, int end) {
    //     final int width = SP.length();
    //     int p = start;
    //     while (p + width <= end) {
    //         VectorMask<Byte> stop = escapeOrControlLanes(ByteVector.fromArray(SP, data, p));
    //         if (stop.anyTrue()) {
    //             return p + stop.firstTrue();
    //         }
    //         p += width;
    //     }
    //     while (p < end) {
    //         int b = data[p] & 0xFF;
    //         if (b == '\\' || b < 0x20) {
    //             return p;
    //         }
    //         p++;
    //     }
    //     return end;
    // }

    // Lanes that are a backslash or an ASCII control character (< 0x20); non-ASCII
    // bytes (negative as signed) are excluded.
    private static VectorMask<Byte> escapeOrControlLanes(ByteVector chunk) {
        VectorMask<Byte> negative = chunk.lt(ZERO);
        VectorMask<Byte> control = chunk.lt(THIRTY_TWO).andNot(negative);
        return control.or(chunk.eq(BACKSLASH));
    }

    // Lanes that are control characters, double quotes or backslashes. Non-ASCII
    // bytes (negative as signed) are excluded so they stay on the plain fast path.
    private static VectorMask<Byte> interestingLanes(ByteVector chunk) {
        VectorMask<Byte> negative = chunk.lt(ZERO);
        VectorMask<Byte> lowOrQuote = chunk.lanewise(VectorOperators.XOR, TWO)
                                           .lt(THIRTY_THREE)
                                           .andNot(negative);
        return lowOrQuote.or(chunk.eq(BACKSLASH));
    }

    // Lanes that are a double quote or a backslash (non-plain phase).
    private static VectorMask<Byte> quoteOrBackslashLanes(ByteVector chunk) {
        return chunk.eq(DQUOTE).or(chunk.eq(BACKSLASH));
    }
}
