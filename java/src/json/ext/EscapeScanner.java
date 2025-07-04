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
        static Constructor<?> vectorizedEscapeScannerConstructor = null;

        static {
            Optional<Module> vectorModule = ModuleLayer.boot().findModule("jdk.incubator.vector");
            if (vectorModule.isPresent()) {
                try {
                    Class<?> vectorEscapeScannerClass = EscapeScanner.class.getClassLoader().loadClass("json.ext.VectorizedEscapeScanner");
                    vectorizedEscapeScannerConstructor = vectorEscapeScannerClass.getDeclaredConstructor();
                } catch (ClassNotFoundException | NoSuchMethodException e) {
                    // Fallback to the ScalarEscapeScanner if we cannot load the VectorizedEscapeScanner.
                    System.err.println("Failed to load VectorizedEscapeScanner, falling back to ScalarEscapeScanner: " + e.getMessage());
                }
            }
        }
    }

    boolean scan(EscapeScanner.State state) throws java.io.IOException;

    public static EscapeScanner basicScanner() {
        if (VectorSupport.vectorizedEscapeScannerConstructor != null) {
            try {
                // Attempt to instantiate the vectorized escape scanner if available.
                return (EscapeScanner) VectorSupport.vectorizedEscapeScannerConstructor.newInstance();
            } catch (InstantiationException | IllegalAccessException | InvocationTargetException e) {
                System.err.println("Failed to instantiate VectorizedEscapeScanner, falling back to ScalarEscapeScanner: " + e.getMessage());
            }

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
