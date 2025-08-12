package json.ext;

import org.jcodings.Encoding;
import org.jruby.util.ByteList;

import java.io.IOException;

public class LinkedSegmentedByteListDirectOutputStream extends AbstractByteListDirectOutputStream {
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
            if (size <= 0) {
                size = DEFAULT_SEGMENT_SIZE;
            }
            buffer = new byte[Math.max(size, DEFAULT_SEGMENT_SIZE)];
        }
    }

    LinkedSegmentedByteListDirectOutputStream() {
        this(Segment.DEFAULT_SEGMENT_SIZE);
    }

    LinkedSegmentedByteListDirectOutputStream(int size) {
        current = head = new Segment(size);
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
            // This check is deliberately in the case the current segment is full. We want to 
            // avoid this check in the common case where we have space in the current segment.
            if (this.length + 1 < 0) {
                throw new IOException("Total length exceeds maximum length of an array.");
            }
            if (c.next == null) {
                numSegments++;
                c.next = new Segment(c.buffer.length * 2);
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

        while (remaining > 0) {
            if (c.length == c.buffer.length) {
                // This check is deliberately in the case the current segment is full. We want to 
                // avoid this check in the common case where we have space in the current segment.
                if (this.length + remaining < 0) {
                    throw new IOException("Total length exceeds maximum length of an array.");
                }
                if (c.next == null) {
                    numSegments++;
                    c.next = new Segment(c.buffer.length * 2);
                }
                c = c.next;
                current = c;
            }
            int currentLength = c.length;
            int currentCapacity = c.buffer.length;
            int copyLength = Math.min(remaining, currentCapacity - currentLength);
            System.arraycopy(bytes, start, c.buffer, currentLength, copyLength);
            c.length += copyLength;
            this.length += copyLength;
            start += copyLength;
            remaining -= copyLength;
        }
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        write(bytes, 0, bytes.length);
    }
}
