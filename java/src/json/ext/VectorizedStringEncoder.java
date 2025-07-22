package json.ext;

import java.io.IOException;

import org.jruby.util.ByteList;

import jdk.incubator.vector.ByteVector;
import jdk.incubator.vector.VectorMask;
import jdk.incubator.vector.VectorOperators;
import jdk.incubator.vector.VectorSpecies;

class VectorizedStringEncoder extends StringEncoder {
    private static final VectorSpecies<Byte> SP = ByteVector.SPECIES_PREFERRED;
    private static final ByteVector ZERO = ByteVector.zero(SP);
    private static final ByteVector TWO = ByteVector.broadcast(SP, 2);
    private static final ByteVector THIRTY_THREE = ByteVector.broadcast(SP, 33);
    private static final ByteVector BACKSLASH = ByteVector.broadcast(SP, '\\');

    VectorizedStringEncoder() {
        super(StringEncoder.ESCAPE_TABLE);
    }

    @Override
    void encode(ByteList src) throws IOException {
        // VectorizedState state = createState(src.unsafeBytes(), src.begin(), src.realSize(), 0);

        byte[] ptrBytes = src.unsafeBytes();
        int ptr = src.begin();
        int len = src.realSize();
        int beg = 0;
        int pos = 0;

        while (((ptr + pos) + SP.length() < len)) {
            ByteVector chunk = ByteVector.fromArray(SP, ptrBytes, ptr + pos);
            int chunkLength = SP.length();

            // bytes are signed in java, so we need to remove negative values
            VectorMask<Byte> negative = chunk.lt(ZERO);
            VectorMask<Byte> tooLowOrDblQuote = chunk.lanewise(VectorOperators.XOR, TWO).lt(THIRTY_THREE).andNot(negative);
            VectorMask<Byte> needsEscape = chunk.eq(BACKSLASH).or(tooLowOrDblQuote);
            if (needsEscape.anyTrue()) {
                int chunkStart = ptr + pos;
                long mask = needsEscape.toLong();

                while(mask > 0) {
                    // nextMatch inlined
                    int index = SP.length() > 32 ? Long.numberOfTrailingZeros(mask) : Integer.numberOfTrailingZeros((int) mask);
                    mask &= (mask - 1);
                    pos = chunkStart + index;
                    int ch = Byte.toUnsignedInt(ptrBytes[ptr + pos]);
                    
                    beg = pos = flushPos(pos, beg, ptrBytes, ptr, 1);
                    // escapeAscii(ch, aux, HEX);
                    switch (ch) {
                        case '"':  out.write(BACKSLASH_DOUBLEQUOTE, 0, 2); break;
                        case '\\': out.write(BACKSLASH_BACKSLASH, 0, 2); break;
                        case '/':  out.write(BACKSLASH_FORWARDSLASH, 0, 2); break;
                        case '\b': out.write(BACKSLASH_B, 0, 2); break;
                        case '\f': out.write(BACKSLASH_F, 0, 2); break;
                        case '\n': out.write(BACKSLASH_N, 0, 2); break;
                        case '\r': out.write(BACKSLASH_R, 0, 2); break;
                        case '\t': out.write(BACKSLASH_T, 0, 2); break;
                        default: {
                            aux[2] = '0';
                            aux[3] = '0';
                            aux[4] = HEX[(ch >> 4) & 0xf];
                            aux[5] = HEX[ch & 0xf];
                            out.write(aux, 0, 6);
                            break;
                        }
                    }
                }
                
                // Skip over any remaining characters in the current chunk
                pos = chunkStart + chunkLength;
                continue;
            }

            pos += SP.length();
        }

        while (pos < len) {
            int ch = Byte.toUnsignedInt(ptrBytes[ptr + pos]);
            int ch_len = escapeTable[ch];

            if (ch_len > 0) {
                beg = pos = flushPos(pos, beg, ptrBytes, ptr, 1);
                // escapeAscii(ch, aux, HEX);
                switch (ch) {
                    case '"':  out.write(BACKSLASH_DOUBLEQUOTE, 0, 2); break;
                    case '\\': out.write(BACKSLASH_BACKSLASH, 0, 2); break;
                    case '/':  out.write(BACKSLASH_FORWARDSLASH, 0, 2); break;
                    case '\b': out.write(BACKSLASH_B, 0, 2); break;
                    case '\f': out.write(BACKSLASH_F, 0, 2); break;
                    case '\n': out.write(BACKSLASH_N, 0, 2); break;
                    case '\r': out.write(BACKSLASH_R, 0, 2); break;
                    case '\t': out.write(BACKSLASH_T, 0, 2); break;
                    default: {
                        aux[2] = '0';
                        aux[3] = '0';
                        aux[4] = HEX[(ch >> 4) & 0xf];
                        aux[5] = HEX[ch & 0xf];
                        out.write(aux, 0, 6);
                        break;
                    }
                }
                continue;
            }

            pos++;
        }

        if (beg < len) {
            append(ptrBytes, ptr + beg, len - beg);
        }
    }
    
    // @Override
    // void encode(ByteList src) throws IOException {
    //     VectorizedState state = createState(src.unsafeBytes(), src.begin(), src.realSize(), 0);

    //     while(scan(state)) {
    //         state.beg = state.pos = flushPos(state.pos, state.beg, state.ptrBytes, state.ptr, 1);
    //         escapeAscii(state.ch, aux, HEX);
    //     }

    //     if (state.beg < state.len) {
    //         append(state.ptrBytes, state.ptr + state.beg, state.len - state.beg);
    //     }
    // }

    // private boolean scan(VectorizedState state) throws IOException {
    //     if (state.hasMatches) {
    //         if (state.mask > 0) {
    //             // nextMatch inlined
    //             int index = SP.length() > 32 ? Long.numberOfTrailingZeros(state.mask) : Integer.numberOfTrailingZeros((int) state.mask);
    //             state.mask &= (state.mask - 1);
    //             state.pos = state.chunkStart + index;
    //             state.ch = Byte.toUnsignedInt(state.ptrBytes[state.ptr + state.pos]);
    //             return true;
    //         } else {
    //             state.hasMatches = false;
    //             state.pos = state.chunkStart + state.chunkLength;
    //         }
    //     }

    //     while (((state.ptr + state.pos) + SP.length() < state.len)) {
    //         ByteVector chunk = ByteVector.fromArray(SP, state.ptrBytes, state.ptr + state.pos);
    //         state.chunkLength = SP.length();

    //         // bytes are signed in java, so we need to remove negative values
    //         VectorMask<Byte> negative = chunk.lt(ZERO);
    //         VectorMask<Byte> tooLowOrDblQuote = chunk.lanewise(VectorOperators.XOR, TWO).lt(THIRTY_THREE).andNot(negative);
    //         VectorMask<Byte> needsEscape = chunk.eq(BACKSLASH).or(tooLowOrDblQuote);
    //         if (needsEscape.anyTrue()) {
    //             state.hasMatches = true;
    //             state.chunkStart = state.ptr + state.pos;
    //             state.mask = needsEscape.toLong();

    //             // nextMatch - inlined
    //             int index = SP.length() > 32 ? Long.numberOfTrailingZeros(state.mask) : Integer.numberOfTrailingZeros((int) state.mask);
    //             state.mask &= (state.mask - 1);
    //             state.pos = state.chunkStart + index;
    //             state.ch = Byte.toUnsignedInt(state.ptrBytes[state.ptr + state.pos]);
                
    //             return true;
    //         }

    //         state.pos += SP.length();
    //     }

    //     int remaining = state.len - (state.ptr + state.pos);
    //     for (int i = 0; i<remaining; i++) {
    //         state.ch = Byte.toUnsignedInt(state.ptrBytes[state.ptr + state.pos]);
    //         // if (state.ch < 128) {
    //             int ch_len = StringEncoder.ESCAPE_TABLE[state.ch];
    //             if (ch_len > 0) {
    //                 return true;
    //             }
    //         // }
    //         state.pos++;
    //     }
    //     return false;
    // }

    //  public VectorizedState createState(byte[] ptrBytes, int ptr, int len, int beg) {
    //     VectorizedState state = new VectorizedState();
    //     state.ptrBytes = ptrBytes;
    //     state.ptr = ptr;
    //     state.len = len;
    //     state.beg = beg;
    //     state.pos = 0; // Start scanning from the beginning of the segment
    //     return state;
    // }

    // private static class VectorizedState {
    //     private byte[] ptrBytes;
    //     private int ptr;
    //     private int len;
    //     private int pos;
    //     private int beg;
    //     private int ch;
    //     private long mask;
    //     private int chunkStart = 0;
    //     private boolean hasMatches;
    //     private int chunkLength;
    // }
}
