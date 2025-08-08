package json.ext;

import org.jcodings.Encoding;
import org.jruby.util.ByteList;

import java.io.IOException;
import java.io.OutputStream;

public class SegmentedByteListDirectOutputStream extends OutputStream {
    private Segment head;
    private int length;
    private Segment current;
    private int numSegments;

    private static class Segment {
        static final int DEFAULT_SEGMENT_SIZE = 1024;
        byte[] buffer;
        int length;
        Segment next;

        Segment() {
            this(DEFAULT_SEGMENT_SIZE);
        }

        Segment(int size) {
            buffer = new byte[size];
        }
    }

    SegmentedByteListDirectOutputStream(int size) {
        head = new Segment(size);
        current = head;
    }

    public ByteList toByteListDirect(Encoding encoding) {
        byte[] buffer = new byte[length];
        Segment segment = head;
        int pos = 0;
        while (segment != null) {
            System.arraycopy(segment.buffer, 0, buffer, pos, segment.length);
            pos += segment.length;
            segment = segment.next;
        }
        return new ByteList(buffer, 0, length, encoding, false);
    }

    @Override
    public void write(int b) throws IOException {
        Segment c = current;
        if (c.length == c.buffer.length) {
            if (c.next == null) {
                numSegments++;
                c.next = new Segment(Segment.DEFAULT_SEGMENT_SIZE * numSegments);
            }
            c = c.next;
            current = c;
        }
        c.buffer[c.length++] = (byte)b;
        length++;
    }

    @Override
    public void write(byte[] bytes, int start, int length) throws IOException {
        Segment c = current;
        int remaining = length;

        // TODO handle the case where we might overflow Java.INT_MAX_VALUE as we wouldn't
        // be able to allocate a byte[] of that size.

        while (remaining > 0) {
            int currentLength = c.length;
            int currentCapacity = c.buffer.length;
            int copyLength = Math.min(remaining, currentCapacity - currentLength);
            System.arraycopy(bytes, start, c.buffer, currentLength, copyLength);
            c.length += copyLength;
            this.length += copyLength;
            start += copyLength;
            remaining -= copyLength;
            if (remaining == 0) {
                return;
            }
            if (c.next == null) {
                numSegments++;
                c.next = new Segment(Math.max(Segment.DEFAULT_SEGMENT_SIZE*numSegments, remaining));
            }
            c = c.next;
            current = c;
        }
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        write(bytes, 0, bytes.length);
    }
}
