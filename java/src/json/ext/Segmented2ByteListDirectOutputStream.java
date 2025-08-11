package json.ext;

import org.jcodings.Encoding;
import org.jruby.util.ByteList;

import java.io.IOException;
import java.io.OutputStream;

public class Segmented2ByteListDirectOutputStream extends OutputStream {
    private static final int[] CAPACITIES = new int[] { 
        1024, 2048, 4096, 8192,
        16384, 3276, 65536 ,131072,
        262144, 524288, 1048576, 2097152,
        4194304, 8388608, 16777216, 33554432,
        67108864, 134217728, 268435456, 536870912, 
        1073741824, 1023};

    private int totalLength;
    private byte[][] segments = new byte[21][];
    private int currentSegmentIndex;
    private int currentSegmentLength;
    private byte[] currentSegment;

    Segmented2ByteListDirectOutputStream() {
        currentSegment = new byte[CAPACITIES[0]];
        segments[0] = currentSegment;
    }

    public ByteList toByteListDirect(Encoding encoding) {
        byte[] buffer = new byte[totalLength];
        int pos = 0;
        // We handle the current segment separately.
        for (int i = 0; i < currentSegmentIndex; i++) {
            byte[] segment = segments[i];
            System.arraycopy(segment, 0, buffer, pos, segment.length);
            pos += segment.length;
        }
        System.arraycopy(currentSegment, 0, buffer, pos, currentSegmentLength);
        return new ByteList(buffer, 0, totalLength, encoding, false);
    }

    @Override
    public void write(int b) throws IOException {
        if (currentSegmentLength == currentSegment.length) {
            if (totalLength + 1 < 0) {
                throw new IOException("Total length exceeds maximum allowed size");
            }
            currentSegmentIndex++;
            int capacity = CAPACITIES[currentSegmentIndex];
            currentSegment = new byte[capacity];
            currentSegmentLength = 0;
            segments[currentSegmentIndex] = currentSegment;
        }
        currentSegment[currentSegmentLength++] = (byte) b;
        totalLength++;
    }

    @Override
    public void write(byte[] bytes, int start, int length) throws IOException {
        int remaining = length;

        while (remaining > 0) {
            if (currentSegmentLength == currentSegment.length) {
                if (totalLength + remaining < 0) {
                    throw new IOException("Total length exceeds maximum allowed size");
                }
                currentSegmentIndex++;
                int capacity = CAPACITIES[currentSegmentIndex];
                currentSegment = new byte[capacity];
                currentSegmentLength = 0;
                segments[currentSegmentIndex] = currentSegment;
            }
            int toWrite = Math.min(remaining, currentSegment.length - currentSegmentLength);
            System.arraycopy(bytes, start, currentSegment, currentSegmentLength, toWrite);
            currentSegmentLength += toWrite;
            start += toWrite;
            remaining -= toWrite;
        }
        totalLength += length;
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        write(bytes, 0, bytes.length);
    }
}
