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
            while (p + width <= end) {
                ByteVector chunk = ByteVector.fromArray(SP, data, p);
                VectorMask<Byte> interesting =
                    plain ? interestingLanes(chunk) : quoteOrBackslashLanes(chunk);
                if (interesting.anyTrue()) {
                    p += interesting.firstTrue();
                    break;
                }
                p += width;
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
                if (b < 0x20 || b >= 0x80) {
                    plain = false;
                    p++;
                    continue outer;
                }
                p++;
            }
            return NOT_FOUND;
        }
    }

    // Lanes that are control characters, double quotes, backslashes or non-ASCII.
    private static VectorMask<Byte> interestingLanes(ByteVector chunk) {
        VectorMask<Byte> negative = chunk.lt(ZERO);
        VectorMask<Byte> lowOrQuote = chunk.lanewise(VectorOperators.XOR, TWO)
                                           .lt(THIRTY_THREE)
                                           .andNot(negative);
        return lowOrQuote.or(chunk.eq(BACKSLASH)).or(negative);
    }

    // Lanes that are a double quote or a backslash (non-plain phase).
    private static VectorMask<Byte> quoteOrBackslashLanes(ByteVector chunk) {
        return chunk.eq(DQUOTE).or(chunk.eq(BACKSLASH));
    }
}
