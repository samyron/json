package json.ext;

import java.io.IOException;

import jdk.incubator.vector.ByteVector;
import jdk.incubator.vector.VectorMask;
import jdk.incubator.vector.VectorOperators;
import jdk.incubator.vector.VectorSpecies;

public class VectorizedEscapeScanner implements EscapeScanner {
    public static EscapeScanner.ScalarEscapeScanner FALLBACK = new EscapeScanner.ScalarEscapeScanner(StringEncoder.ESCAPE_TABLE);

    // private VectorMask<Byte> needsEscape = null;
    // private int chunkStart = 0;

    @Override
    public boolean scan(State state) throws IOException {
        VectorSpecies<Byte> species = ByteVector.SPECIES_PREFERRED;
        
        // if (needsEscape != null) {
        //     if (needsEscape.anyTrue()) {
        //         int firstEscapeIndex = needsEscape.firstTrue();
        //         needsEscape = needsEscape.andNot(VectorMask.fromLong(species, 1L << firstEscapeIndex));
        //         state.pos = chunkStart + firstEscapeIndex;
        //         return true;
        //     } else {
        //         needsEscape = null;
        //     }
        // }

        while ((state.ptr + state.pos) + species.length() < state.len) {
            ByteVector chunk = ByteVector.fromArray(species, state.ptrBytes, state.ptr + state.pos);
            ByteVector zero = ByteVector.broadcast(species, 0);

            // bytes are unsigned in java, so we need to check for negative values
            // to determine if we have a byte that is less than 0 (>= 128).
            VectorMask<Byte> negative = zero.lt(chunk);

            VectorMask<Byte> tooLowOrDblQuote = chunk.lanewise(VectorOperators.XOR, ByteVector.broadcast(species, 2))
                .lt(ByteVector.broadcast(species, 33));

            VectorMask<Byte> needsEscape = chunk.eq(ByteVector.broadcast(species, '\\')).or(tooLowOrDblQuote).and(negative);
            if (needsEscape.anyTrue()) {
                // chunkStart = state.ptr + state.pos;
                int firstEscapeIndex = needsEscape.firstTrue();
                // Clear the bit at firstEscapeIndex to avoid scanning the same byte again
                // needsEscape = needsEscape.andNot(VectorMask.fromLong(species, 1L << firstEscapeIndex));
                state.pos += firstEscapeIndex;
                return true;
            }

            state.pos += species.length();
        }

        return FALLBACK.scan(state);
    }
}
