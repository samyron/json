package json.ext;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.util.Optional;

interface EscapeScanner {
    static class State {
        byte[] ptrBytes;
        int ptr;
        int len;
        int pos;
        int beg;
        int ch;
    }

    static class VectorSupport {
        private static String VECTORIZED_ESCAPE_SCANNER_CLASS = "json.ext.vectorized.VectorizedEscapeScanner";
        static final EscapeScanner VECTORIZED_ESCAPE_SCANNER;

        static {
            EscapeScanner scanner = null;
            try {
                Class<?> vectorEscapeScannerClass = EscapeScanner.class.getClassLoader().loadClass(VECTORIZED_ESCAPE_SCANNER_CLASS);
                Constructor<?> vectorizedEscapeScannerConstructor = vectorEscapeScannerClass.getDeclaredConstructor();
                scanner = (EscapeScanner) vectorizedEscapeScannerConstructor.newInstance();
            } catch (ClassNotFoundException | NoSuchMethodException | InstantiationException | IllegalAccessException | InvocationTargetException e) {
                // Fallback to the ScalarEscapeScanner if we cannot load the VectorizedEscapeScanner.
                System.err.println("Failed to load VectorizedEscapeScanner, falling back to ScalarEscapeScanner: " + e.getMessage());
                scanner = null;
            }
            VECTORIZED_ESCAPE_SCANNER = scanner;
        }
    }

    boolean scan(EscapeScanner.State state) throws java.io.IOException;

    default State createState(byte[] ptrBytes, int ptr, int len, int beg) {
        State state = new State();
        state.ptrBytes = ptrBytes;
        state.ptr = ptr;
        state.len = len;
        state.beg = beg;
        state.pos = 0; // Start scanning from the beginning of the segment
        return state;
    }

    public static EscapeScanner basicScanner() {
        if (VectorSupport.VECTORIZED_ESCAPE_SCANNER != null) {
            return VectorSupport.VECTORIZED_ESCAPE_SCANNER;
        }

        return new ScalarEscapeScanner(StringEncoder.ESCAPE_TABLE);
    }

    public static EscapeScanner create(byte[] escapeTable) {
        return new ScalarEscapeScanner(escapeTable);
    }

    public static class ScalarEscapeScanner implements EscapeScanner {
        private final byte[] escapeTable;

        public ScalarEscapeScanner(byte[] escapeTable) {
            this.escapeTable = escapeTable;
        }

        @Override
        public boolean scan(EscapeScanner.State state) throws java.io.IOException {
            while (state.pos < state.len) {
                state.ch = Byte.toUnsignedInt(state.ptrBytes[state.ptr + state.pos]);
                int ch_len = escapeTable[state.ch];
                if (ch_len > 0) {
                    return true;
                }
                state.pos++;
            }
            return false;
        }

    }
}
