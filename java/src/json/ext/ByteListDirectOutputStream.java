package json.ext;

import org.jcodings.Encoding;
import org.jruby.util.ByteList;

import java.io.IOException;
import java.io.OutputStream;
import java.util.Arrays;

public class ByteListDirectOutputStream extends OutputStream {
    private byte[] buffer;
    private int length;

    ByteListDirectOutputStream(int size) {
        buffer = new byte[size];
    }

    public ByteList toByteListDirect(Encoding encoding) {
        return new ByteList(buffer, 0, length, encoding, false);
    }

    @Override
    public void write(int b) throws IOException {
        int currentLength = this.length;
        int newLength = currentLength + 1;
        ensureBuffer(newLength);
        buffer[currentLength] = (byte) b;
        this.length = newLength;
    }

    @Override
    public void write(byte[] bytes, int start, int length) throws IOException {
        int currentLength = this.length;
        int newLength = currentLength + length;
        ensureBuffer(newLength);
        System.arraycopy(bytes, start, buffer, currentLength, length);
        this.length = newLength;
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        write(bytes, 0, bytes.length);
    }

    private void ensureBuffer(int minimumLength) {
        int myCapacity = this.buffer.length;
        if (minimumLength > myCapacity) {
            // this.buffer = grow(buffer, myCapacity, minimumLength - myCapacity);
            this.buffer = grow(this.buffer, minimumLength);
        }
    }

    private static byte[] grow(byte[] oldBuffer, int required) {
        int capacity = oldBuffer.length;
        
        while (capacity < required) {
            capacity <<= 1;
            if (capacity < 0) {
                throw new OutOfMemoryError();
            }
        }

        return Arrays.copyOf(oldBuffer, capacity);
    }

    // private static byte[] grow(byte[] oldBuffer, int myCapacity, int diff) {
    //     // grow to double current buffer length or capacity + diff, whichever is greater
    //     int newLength = myCapacity + Math.max(myCapacity, diff);
    //     // check overflow
    //     if (newLength < 0) {
    //         // try just diff length in case it can fit
    //         newLength = myCapacity + diff;
    //         if (newLength < 0) {
    //             // throw new ArrayIndexOutOfBoundsException("cannot allocate array of size " + myCapacity + "+" + diff);
    //             throw new OutOfMemoryError();
    //         }
    //     }
    //     return Arrays.copyOf(oldBuffer, newLength);
    // }
}
